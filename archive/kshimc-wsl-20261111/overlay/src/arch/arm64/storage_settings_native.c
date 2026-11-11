/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KSHIM_HOST_TEST
#include <config.h>
#endif
#include <storage_settings_native.h>
#include <storage_settings_export.h>
#include <storage_probe_io.h>
#include <storage_ufs_dma.h>
#include <storage_ufs_native_io.h>
#include <timer.h>
#include <kshim_mmu.h>
#include <string.h>

static _Alignas(4096) uint8_t Arena[KSHIM_UFS_DMA_BYTES];
static struct kshim_ufs_settings Backend;
static struct storage_probe_snapshot Fresh;
static uint32_t TestPerformed;
static int TestStatus;
static uint64_t now_ms(void *ctx) { (void)ctx;return get_current_time(); }
static void delay_us(void *ctx,uint32_t us) { (void)ctx;(void)delay(us); }
static const struct kshim_ufs_ops Ops={
    .read32=storage_ufs_native_read32,.write32=storage_ufs_native_write32,
    .sync_device=storage_ufs_native_sync_device,.sync_cpu=storage_ufs_native_sync_cpu,
    .now_ms=now_ms,.delay_us=delay_us,.manage_link=true
};
static int preflight(void *ctx,uint64_t *physical)
{
    const struct kshim_ufs_settings *b=ctx;
    if ((b->board!=DEVICE_BOARD_TB322FC && b->board!=DEVICE_BOARD_TB323FU) ||
        arm64_read_cntfrq()==0) return KSHIM_UFS_UNSUPPORTED;
    int rc=storage_ufs_native_dma_address(Arena,sizeof(Arena),physical);
    if (rc) return rc;
    storage_probe_native_capture((enum device_board)b->board,&Fresh);
    return kshim_ufs_dma_is_identity(&Fresh,*physical,sizeof(Arena)) ?
        0 : KSHIM_UFS_UNSUPPORTED;
}
int storage_settings_native_open(enum device_board actual_board,
                                  const struct kshim_config_values *defaults)
{
    if (!kshim_ufs_settings_handoff_safe(&Backend)) return KSHIM_UFS_QUARANTINED;
    TestPerformed=0;TestStatus=0;
    if (actual_board!=DEVICE_BOARD_TB322FC && actual_board!=DEVICE_BOARD_TB323FU) {
        Backend.board=(uint32_t)actual_board;
        Backend.load_status=KSHIM_UFS_UNSUPPORTED;
        Backend.settings.source=KSHIM_SETTINGS_NONE;
        return KSHIM_UFS_UNSUPPORTED;
    }
    int rc=kshim_ufs_settings_open(&Backend,&Ops,Arena,(uint32_t)actual_board,
                                     preflight,&Backend,defaults);
#if defined(CONFIG_KSHIM_SETTINGS_SAVE_TEST) && CONFIG_KSHIM_SETTINGS_SAVE_TEST
    /* Explicit Gen4 test profile only. Never overwrite existing settings or
     * provision an old backup. Seed a verified EMPTY pair with internal Linux
     * and Chinese; later boots must load that record without another write. */
    if (actual_board==DEVICE_BOARD_TB322FC && rc==KSHIM_STORAGE_EMPTY) {
        const struct kshim_config_values initial={0,2,0, 0, 0};
        TestPerformed=1;
        TestStatus=kshim_settings_save(&Backend.settings,&initial);
        if (TestStatus) return TestStatus;
    }
#endif
    return rc;
}
int storage_settings_native_note_boot(uint32_t target)
{
    struct kshim_settings_session *s=&Backend.settings;
    if (target<1 || target>3) return KSHIM_STORAGE_ARG;
    if (!kshim_ufs_settings_handoff_safe(&Backend)) return KSHIM_UFS_QUARANTINED;
    if (s->source!=KSHIM_SETTINGS_INTERNAL) return KSHIM_STORAGE_NOT_FOUND;
    if (s->values.last_target==target && !s->save_uncertain) return 0;

    /* The menu closes UI/SMP and disables the MMU before returning its
     * selection. Reuse the existing runtime tables only for this transaction;
     * the selected payload's memory remains outside this temporary interval. */
    bool restore_off=!kshim_mmu_is_enabled();
    if (restore_off && (!kshim_mmu_root_table() || kshim_mmu_enable_current_cpu())) {
        Backend.begin_status=KSHIM_UFS_UNSUPPORTED;
        return KSHIM_UFS_UNSUPPORTED;
    }
    int rc=kshim_settings_note_boot(s,target);
    /* Keep mappings alive if DMA ownership could not be safely returned. */
    if (!kshim_ufs_settings_handoff_safe(&Backend)) return KSHIM_UFS_QUARANTINED;
    if (restore_off && kshim_mmu_prepare_handoff()) {
        Backend.unsafe=1;
        return KSHIM_UFS_QUARANTINED;
    }
    return rc;
}
struct kshim_settings_session *storage_settings_native_session(void)
{
    return &Backend.settings;
}
bool storage_settings_native_handoff_safe(void)
{
    return kshim_ufs_settings_handoff_safe(&Backend);
}
int storage_settings_native_export(void *fdt)
{
    return storage_settings_export(fdt,&Backend,TestPerformed,TestStatus);
}


/* BCB fallback for MENU_RECOVERY/MENU_FASTBOOT: the PMIC restart_reason SPMI
 * channel belongs to a foreign EE on TB322FC, so kshim_qcom_reboot_request
 * returns KSHIM_QCOM_REBOOT_IO_FAILED for non-EDL targets. Android's own
 * reboot-recovery/bootloader contract is the BCB command field inside misc
 * (LUN0) plus a plain reset; reproduce it exactly, touching only the 32-byte
 * command field. */
static uint64_t bcb_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static uint32_t bcb_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static int bcb_find_misc(struct kshim_blockdev *d, uint64_t *first_lba,
                         uint8_t *scratch)
{
    /* Primary GPT on LUN0: header LBA1, entries LBA2+. scratch holds the
     * header sector plus one entry-array sector at a time. */
    int rc = kshim_block_read(d, 1, 1, scratch);
    if (rc) return rc;
    if (memcmp(scratch, "EFI PART", 8) || bcb_u32(scratch + 8) != 0x00010000)
        return KSHIM_STORAGE_FORMAT;
    uint64_t ent_lba = bcb_u64(scratch + 72);
    uint32_t n_ent = bcb_u32(scratch + 80), ent_sz = bcb_u32(scratch + 84);
    if (!ent_lba || !ent_sz || ent_sz < 128 || ent_sz > 4096 ||
        (ent_sz & (ent_sz - 1)) || !n_ent || n_ent > 1024)
        return KSHIM_STORAGE_FORMAT;
    uint8_t *win = scratch + 4096;
    for (uint32_t i = 0; i < n_ent; ++i) {
        uint64_t off = (uint64_t)i * ent_sz;
        if (!(off & 4095)) {
            rc = kshim_block_read(d, ent_lba + off / 4096, 1, win);
            if (rc) return rc;
        }
        const uint8_t *e = win + (off & 4095);
        unsigned t;
        for (t = 0; t < 16 && !e[t]; ++t) {}
        if (t == 16) continue;   /* unused entry: all-zero type GUID */
        char name[37];
        unsigned j;
        for (j = 0; j < 36 && e[56 + 2 * j]; ++j) {
            if (e[56 + 2 * j] & 0x80 || e[56 + 2 * j + 1]) break;
            name[j] = (char)e[56 + 2 * j];
        }
        name[j] = 0;
        if (!strcmp(name, "misc")) {
            *first_lba = bcb_u64(e + 32);
            return 0;
        }
    }
    return KSHIM_STORAGE_NOT_FOUND;
}
static struct kshim_ufs_host BcbHost;
static struct kshim_ufs_lun BcbLun;
/* Self-contained UFS transaction for the misc BCB write. The settings
 * session cannot be borrowed on TB322FC (no internal settings store, so
 * io_begin/io_end are never bound); claim the controller directly with the
 * same preflight/arena/ops contract as the settings backend.
 * Debug encoding: -(stage*100 + |rc|). stages: 1 arg/quar, 3 claim path,
 * 4 find_misc, 5 misc read, 6 misc write, 7 release. */
int storage_settings_native_write_bcb(const char *command)
{
    if (!command || strlen(command) >= 32) return -101;
    if (Backend.unsafe) return -102;
    memset(&BcbHost, 0, sizeof(BcbHost));
    memset(&BcbLun, 0, sizeof(BcbLun));
    uint64_t dma = 0;
    int rc = preflight(&Backend, &dma);
    if (!rc) rc = kshim_ufs_claim(&BcbHost, &Ops, Arena,
                                KSHIM_UFS_DMA_BYTES, dma);
    if (!rc) rc = kshim_ufs_nop(&BcbHost);
    if (!rc) rc = kshim_ufs_prepare_device(&BcbHost);
    if (!rc) rc = kshim_ufs_open_lun(&BcbHost, 0, true, &BcbLun);
    if (rc) {
        if (BcbHost.owned) {
            int rel = kshim_ufs_release(&BcbHost);
            if (rel) return -700 - (rel < 0 ? -rel : rel);
        }
        return -300 - (rc < 0 ? -rc : rc);
    }
    uint64_t lba = 0;
    rc = bcb_find_misc(&BcbLun.block, &lba, Backend.scratch);
    if (rc) {
        rc = -400 - (rc < 0 ? -rc : rc);
    } else {
        rc = kshim_block_read(&BcbLun.block, lba, 1, Backend.scratch);
        if (rc) {
            rc = -500 - (rc < 0 ? -rc : rc);
        } else {
            memset(Backend.scratch, 0, 32);
            memcpy(Backend.scratch, command, strlen(command));
            rc = kshim_block_write(&BcbLun.block, lba, 1, Backend.scratch);
            if (!rc) rc = kshim_block_flush(&BcbLun.block);
            if (rc) rc = -600 - (rc < 0 ? -rc : rc);
        }
    }
    {
        int rel = kshim_ufs_release(&BcbHost);
        if (rel) return -700 - (rel < 0 ? -rel : rel);
    }
    return rc;
}



/* UniXNU AdvancedLogger spill. UniXNU SEC writes a 16-byte 'Alog_Ptr'
 * trampoline at PA 0xC2000000 (below the FreeExec copy window); ptr[8..15]
 * is the physical address of ADVANCED_LOGGER_INFO, whose buffer holds the
 * DXE log up to the exception. The region survives the warm reset back into
 * kShimC but is destroyed once Android's ramoops driver reinitialises
 * 0xC2000000, so it must be lifted here, before any payload is launched.
 *
 * Landing zone: LUN4 GPT free tail (raw sectors no partition owns); Android
 * pulls it with dd if=/dev/block/sde bs=4096 skip=<hdr_lba+1>.
 * Dump header sector: 'KSHIMLOG' + total_len + info_pa + self LBA.
 *
 * Status beacon: second sector of misc (misc_lba+1) always gets 'KSHIMDBG'
 * + rc + info_pa + dest + total + scan_hit + 32B raw[0xC2000000].
 * Pending (-1) is stamped right after the claim; final code before release.
 *   dd if=/dev/block/by-name/misc bs=4096 skip=1 count=1
 * Debug encoding mirrors write_bcb: -(stage*100+|rc|). */
#define ADVLOG_PTR_PA    0xC2000000ull
#define ADVLOG_SCAN_END  0xC2100000ull
#define ADVLOG_SCAN_LO   0x80000000ull
#define ADVLOG_SCAN_HI   0x200000000ull
#define ADVLOG_HDR_MAGIC "KSHIMLOG"
#define ADVLOG_DBG_MAGIC "KSHIMDBG"
#define ADVLOG_MAX_BYTES (8u * 1024u * 1024u)
static _Alignas(4096) uint8_t AdvLogBuf[32768];
static uint64_t advlog_rd64(const volatile uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static uint32_t advlog_rd32(const volatile uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static void advlog_wr64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
/* Largest used end_lba on a GPT blockdev (primary header at LBA1). */
static int advlog_gpt_tail(struct kshim_blockdev *d, uint64_t *max_end,
                           uint8_t *scratch)
{
    int rc = kshim_block_read(d, 1, 1, scratch);
    if (rc) return rc;
    if (memcmp(scratch, "EFI PART", 8) || bcb_u32(scratch + 8) != 0x00010000)
        return KSHIM_STORAGE_FORMAT;
    uint64_t ent_lba = bcb_u64(scratch + 72);
    uint32_t n_ent = bcb_u32(scratch + 80), ent_sz = bcb_u32(scratch + 84);
    if (!ent_lba || !ent_sz || ent_sz < 128 || ent_sz > 4096 ||
        (ent_sz & (ent_sz - 1)) || !n_ent || n_ent > 1024)
        return KSHIM_STORAGE_FORMAT;
    uint8_t *win = scratch + 4096;
    uint64_t last = 0;
    for (uint32_t i = 0; i < n_ent; ++i) {
        uint64_t off = (uint64_t)i * ent_sz;
        if (!(off & 4095)) {
            rc = kshim_block_read(d, ent_lba + off / 4096, 1, win);
            if (rc) return rc;
        }
        const uint8_t *e = win + (off & 4095);
        unsigned t;
        for (t = 0; t < 16 && !e[t]; ++t) {}
        if (t == 16) continue;
        uint64_t end = bcb_u64(e + 40);
        if (end > last) last = end;
    }
    if (!last) return KSHIM_STORAGE_NOT_FOUND;
    *max_end = last;
    return 0;
}
static struct kshim_ufs_host AdvHost;
static struct kshim_ufs_lun AdvLun0, AdvLun4;
static uint64_t AdvMiscLba, AdvInfoPa, AdvDest, AdvTotal, AdvScanHit;
static int advlog_is_ptr_sig(const volatile uint8_t *p)
{
    static const char sig[8] = {'A','l','o','g','_','P','t','r'};
    for (unsigned i = 0; i < 8; ++i) if (p[i] != (uint8_t)sig[i]) return 0;
    return 1;
}
static int advlog_is_info_sig(const volatile uint8_t *p)
{
    return p[0] == 'A' && p[1] == 'L' && p[2] == 'O' && p[3] == 'G';
}
/* Stamp the status beacon into misc sector 2. rc is stored signed. */
static int advlog_status(struct kshim_blockdev *lun0, int rc, uint8_t *scratch)
{
    int r = kshim_block_read(lun0, AdvMiscLba + 1, 1, scratch);
    if (r) return r;
    memset(scratch, 0, 80);
    memcpy(scratch, ADVLOG_DBG_MAGIC, 8);
    advlog_wr64(scratch + 8, (uint64_t)(int64_t)rc);
    advlog_wr64(scratch + 16, AdvInfoPa);
    advlog_wr64(scratch + 24, AdvDest);
    advlog_wr64(scratch + 32, AdvTotal);
    advlog_wr64(scratch + 40, AdvScanHit);
    memcpy(scratch + 48, (const void *)(uintptr_t)ADVLOG_PTR_PA, 32);
    r = kshim_block_write(lun0, AdvMiscLba + 1, 1, scratch);
    if (!r) r = kshim_block_flush(lun0);
    return r;
}
int storage_settings_native_export_advlog(void)
{
    if (Backend.unsafe) return -202;
    memset(&AdvHost, 0, sizeof(AdvHost));
    memset(&AdvLun0, 0, sizeof(AdvLun0));
    memset(&AdvLun4, 0, sizeof(AdvLun4));
    AdvMiscLba = 0; AdvInfoPa = 0; AdvDest = 0; AdvTotal = 0;
    AdvScanHit = (uint64_t)-1;
    /* Forensics: scan [base, base+1MB) for either signature. hit is an
     * absolute PA; the beacon also carries the raw 32B at the base. */
    uint64_t tramp_pa = 0;
    const volatile uint8_t *base =
        (const volatile uint8_t *)(uintptr_t)ADVLOG_PTR_PA;
    /* Pass 1: page-strided sweep of the mapped 1MiB window [base, SCAN_END).
     * 0xC2000000..0xC2100000 is proven mapped (ramoops region). A wider DRAM
     * sweep hits unmapped holes and hangs at the logo screen (v15). */
    for (uint64_t pa = ADVLOG_PTR_PA; pa < ADVLOG_SCAN_END; pa += 4096) {
        const volatile uint8_t *p =
            (const volatile uint8_t *)(uintptr_t)pa;
        if (advlog_is_ptr_sig(p)) {
            tramp_pa = pa;
            AdvScanHit = pa;
            AdvInfoPa = advlog_rd64(p + 8);
            break;
        }
        if (advlog_is_info_sig(p)) {
            AdvScanHit = pa;
            AdvInfoPa = pa;
            break;
        }
    }
    (void)base;
    uint64_t dma = 0;
    int rc = preflight(&Backend, &dma);
    if (!rc) rc = kshim_ufs_claim(&AdvHost, &Ops, Arena,
                                KSHIM_UFS_DMA_BYTES, dma);
    if (!rc) rc = kshim_ufs_nop(&AdvHost);
    if (!rc) rc = kshim_ufs_prepare_device(&AdvHost);
    if (!rc) rc = kshim_ufs_open_lun(&AdvHost, 0, true, &AdvLun0);
    if (rc) {
        if (AdvHost.owned) {
            int rel = kshim_ufs_release(&AdvHost);
            if (rel) return -700 - (rel < 0 ? -rel : rel);
        }
        return -300 - (rc < 0 ? -rc : rc);
    }
    rc = bcb_find_misc(&AdvLun0.block, &AdvMiscLba, Backend.scratch);
    if (rc) {
        rc = -400 - (rc < 0 ? -rc : rc);
        goto done;
    }
    rc = -110;
    if (advlog_status(&AdvLun0.block, -1, Backend.scratch))
        goto done;
    if (AdvScanHit == (uint64_t)-1) goto done;   /* nothing at/near base */
    if (AdvInfoPa < 0x80000000ull || AdvInfoPa >= 0x100000000ull ||
        (AdvInfoPa & 4095)) {
        rc = -120; goto done;
    }
    {
        uint8_t hdr[0x80];
        for (unsigned hop = 0; hop < 2; ++hop) {
            memcpy(hdr, (const void *)(uintptr_t)AdvInfoPa, sizeof(hdr));
            if (!memcmp(hdr, "ALOG", 4)) break;
            uint64_t next = advlog_rd64(hdr + 0x50);
            if (!hop && next >= 0x80000000ull && next < 0x100000000ull &&
                !(next & 4095)) {
                AdvInfoPa = next;
                continue;
            }
            rc = -130; goto done;
        }
        uint32_t buf_size = advlog_rd32(hdr + 0x1C);
        uint32_t cur = advlog_rd32(hdr + 0x14);
        if (buf_size > ADVLOG_MAX_BYTES || buf_size < 0x58) {
            if (cur > 0x58 && cur <= ADVLOG_MAX_BYTES)
                buf_size = cur;
            else {
                rc = -140; goto done;
            }
        }
        AdvTotal = (uint64_t)buf_size + 4096;
        if (cur > 0x58 && (uint64_t)cur + 64 > AdvTotal &&
            cur <= ADVLOG_MAX_BYTES)
            AdvTotal = (uint64_t)cur + 64;
        if (AdvTotal > ADVLOG_MAX_BYTES) AdvTotal = ADVLOG_MAX_BYTES;
    }
    rc = kshim_ufs_open_lun(&AdvHost, 4, true, &AdvLun4);
    if (rc) { rc = -300 - (rc < 0 ? -rc : rc); goto done; }
    {
        uint64_t max_end = 0;
        rc = advlog_gpt_tail(&AdvLun4.block, &max_end, Backend.scratch);
        if (rc) { rc = -400 - (rc < 0 ? -rc : rc); goto done; }
        AdvDest = (max_end + 256) & ~255ull;
        memset(Backend.scratch, 0, 64);
        memcpy(Backend.scratch, ADVLOG_HDR_MAGIC, 8);
        uint64_t *f = (uint64_t *)(Backend.scratch + 16);
        f[0] = AdvTotal; f[1] = AdvInfoPa; f[2] = AdvDest;
        rc = kshim_block_write(&AdvLun4.block, AdvDest, 1,
                               Backend.scratch);
        uint64_t done = 0;
        const volatile uint8_t *src =
            (const volatile uint8_t *)(uintptr_t)AdvInfoPa;
        while (!rc && done < AdvTotal) {
            uint32_t chunk = (uint32_t)(AdvTotal - done);
            if (chunk > sizeof(AdvLogBuf)) chunk = sizeof(AdvLogBuf);
            memcpy(AdvLogBuf, (const void *)src + done, chunk);
            rc = kshim_block_write(&AdvLun4.block,
                                   AdvDest + 1 + done / 4096,
                                   chunk / 4096, AdvLogBuf);
            done += chunk;
        }
        if (!rc) rc = kshim_block_flush(&AdvLun4.block);
        if (!rc) {
            volatile uint8_t *clr =
                (volatile uint8_t *)(uintptr_t)ADVLOG_PTR_PA;
            for (unsigned i = 0; i < 16; ++i) clr[i] = 0;
            rc = (int)(AdvDest & 0x7fffffff);
        } else {
            rc = -500 - (rc < 0 ? -rc : rc);
        }
    }
done:
    if (AdvMiscLba)
        (void)advlog_status(&AdvLun0.block, rc, Backend.scratch);
    {
        int rel = kshim_ufs_release(&AdvHost);
        if (rel) return -700 - (rel < 0 ? -rel : rel);
    }
    return rc;
}
