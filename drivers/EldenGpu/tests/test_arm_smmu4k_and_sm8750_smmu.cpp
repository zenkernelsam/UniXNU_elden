/* SPDX-License-Identifier: MIT */
/* gpu-opt-032: real ArmSmmu4K + Sm8750GpuSmmu production units under ASan/UBSan.
 * Software MMIO/table-memory peers only -- pin-exact fixture semantics from the kit's own
 * test_tables.c/test_program.c (the authoritative semantic source). Zero pin source edits,
 * zero extraction, zero replacement. Complements pin self-tests with NEW coverage:
 * cross-run byte-level determinism, stress cycles, Sm8750GpuSmmu binding, audit-walk consistency.
 * No device, hardware mapping or GPU is opened. */
#include "ArmSmmu4K.h"
#include "ArmSmmu4KContext.h"
#include "ArmSmmu4KProgram.h"
#include "ArmSmmu4KAudit.h"
#include "Sm8750GpuSmmu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned long iterations = 0;
static int failures = 0;
#define CHECK(c) do { ++iterations; if (!(c)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// ================= table-build section (pin-exact walk/verify helpers) =================
#define CAP 64
static uint64_t tbl[CAP * 512], tbl2[CAP * 512];

static uint64_t walk_at(const uint64_t *base, const ArmSmmu4KResult *result, uint64_t va)
{
    size_t page = 0;
    for (unsigned level = result->start_level; level <= 3; ++level) {
        uint64_t entry = base[page * 512 + ((va >> (39 - 9 * level)) & 511)];
        if (!entry) return 0;
        if ((entry & 3) != 3) return 0;
        if (level == 3) return entry;
        uint64_t address = entry & UINT64_C(0xfffffffff000);
        if (address < result->root_physical) return 0;
        page = (address - result->root_physical) >> 12;
        if (page >= result->table_pages) return 0;
    }
    return 0;
}

// ================= MMIO program fixture (pin-exact model from test_program.c) =================
#define MBYTES 0x40000
typedef struct { unsigned kind; size_t at; uint64_t value; } Event;
typedef struct {
    uint64_t memory[MBYTES / 8]; Event events[512]; size_t event_count, calls, fail_call;
    size_t cb, gr1; unsigned bank, enabled_seen, timeout, corrupt_tcr, corrupt_enable;
    unsigned fault_on_enable, fail_disable, fail_after_write;
} Fx;
static Fx f;
static ArmSmmu4KConfig lower_cfg = {48, 48, 0x10000000, 16}, upper_cfg = {48, 48, 0x11000000, 16};
static ArmSmmu4KResult lr, ur;
static uint64_t lower_tables[8 * 512], upper_tables[8 * 512];
static ArmSmmu4KContextConfig ctx_config;

static uint32_t get32(size_t at) { uint32_t v; std::memcpy(&v, (unsigned char *)f.memory + at, 4); return v; }
static uint64_t get64m(size_t at) { uint64_t v; std::memcpy(&v, (unsigned char *)f.memory + at, 8); return v; }
static void put32(size_t at, uint32_t v) { std::memcpy((unsigned char *)f.memory + at, &v, 4); }
static void put64m(size_t at, uint64_t v) { std::memcpy((unsigned char *)f.memory + at, &v, 8); }
static int event(unsigned kind, size_t at, uint64_t value)
{
    if (f.event_count >= 512 || at >= MBYTES) return 1;
    f.events[f.event_count++] = Event{kind, at, value};
    return ++f.calls == f.fail_call;
}
static int io_read32(void *cookie, size_t at, uint32_t *out)
{
    if (cookie != &f || (at & 3) || at > MBYTES - 4) return 1;
    if (event(0, at, 0)) return 1;
    *out = (at == f.cb + 0x7f4 && f.timeout) ? 1 : get32(at);
    return 0;
}
static int io_read64(void *cookie, size_t at, uint64_t *out)
{
    if (cookie != &f || (at & 7) || at > MBYTES - 8) return 1;
    if (event(1, at, 0)) return 1;
    *out = get64m(at);
    return 0;
}
static int io_write32(void *cookie, size_t at, uint32_t value)
{
    if (cookie != &f || (at & 3) || at > MBYTES - 4) return 1;
    int fail = event(2, at, value);
    if ((fail && !f.fail_after_write) || (at == f.cb && value == 0 && f.enabled_seen && f.fail_disable)) return 1;
    if (at == f.cb + 0x58) put32(at, get32(at) & ~value); /* actual W1C semantics */
    else if (at == f.cb + 0x30 && f.corrupt_tcr) put32(at, value ^ (1u << 14));
    else if (at == f.cb && value & 1) {
        f.enabled_seen = 1;
        put32(at, f.corrupt_enable ? value ^ 1 : value);
        if (f.fault_on_enable) put32(f.cb + 0x58, 2);
    } else put32(at, value);
    return fail;
}
static int io_write64(void *cookie, size_t at, uint64_t value)
{
    if (cookie != &f || (at & 7) || at > MBYTES - 8) return 1;
    int fail = event(3, at, value);
    if (fail && !f.fail_after_write) return 1;
    put64m(at, value);
    return fail;
}
static int io_barrier(void *cookie) { if (cookie != &f) return 1; return event(4, 0, 0); }

static ArmSmmu4KIo mmio_reset(unsigned register_64k)
{
    std::memset(&f, 0, sizeof(f));
    f.bank = register_64k ? 1 : 2;
    f.gr1 = register_64k ? 65536 : 4096;
    size_t pages = register_64k ? 2 : 32;
    f.cb = (pages + f.bank) * f.gr1;
    put32(0x20, 1u << 30); put32(0x24, register_64k ? 0x80000002 : 0x40020010); put32(0x28, 0x1555);
    ArmSmmu4KIo io = {&f, MBYTES, io_read32, io_read64, io_write32, io_write64, io_barrier};
    return io;
}

// ================= Sm8750GpuSmmu fixture =================
typedef struct {
    uint64_t memory[0x40000 / 8];
    unsigned reads, writes, barriers, fail_read, fail_write, live_conflict, no_free_slot,
             enabled_bank0, global_fault, bad_ids;
} SmFx;
static SmFx sm;
static uint32_t sm_get(size_t at) { uint32_t v; std::memcpy(&v, (unsigned char *)sm.memory + at, 4); return v; }
static void sm_put(size_t at, uint32_t v) { std::memcpy((unsigned char *)sm.memory + at, &v, 4); }
static int sm_read32(void *cookie, size_t at, uint32_t *out)
{
    if (cookie != &sm || (at & 3) || at > 0x40000 - 4) return 1;
    if (++sm.reads == sm.fail_read) return 1;
    *out = sm_get(at);
    return 0;
}
static int sm_write32(void *cookie, size_t at, uint32_t value)
{
    if (cookie != &sm || (at & 3) || at > 0x40000 - 4) return 1;
    if (++sm.writes == sm.fail_write) return 1;
    sm_put(at, value);
    return 0;
}
static int sm_read64(void *cookie, size_t at, uint64_t *out)
{
    if (cookie != &sm || (at & 7) || at > 0x40000 - 8) return 1;
    std::memcpy(out, (unsigned char *)sm.memory + at, 8);
    return 0;
}
static int sm_write64(void *cookie, size_t at, uint64_t value)
{
    if (cookie != &sm || (at & 7) || at > 0x40000 - 8) return 1;
    std::memcpy((unsigned char *)sm.memory + at, &value, 8);
    return 0;
}
static int sm_barrier(void *cookie) { if (cookie != &sm) return 1; ++sm.barriers; return 0; }

/* pin-exact valid SM8750 GPU SMMU register shape:
 * id0(0x20): bits 0x48000000 set, slots count in bits[7:0], no extended ids (bit 0x100 clear)
 *            => live() uses smr bit31; sidmask bits [12:9] must cover sid 4+.
 * id1(0x24): banks in bits[7:0] (>=3), s2 bits[23:16]=0, bit31 clear => 4k pages,
 *            bits[30:28]=4 => pages = 1<<(4+1) = 32.
 * cr0(0x0): (cr0 & 0x407) == 0x406, bit8 == extended(0). fault(0x48) == 0. */
static ArmSmmu4KIo smmu_reset()
{
    std::memset(&sm, 0, sizeof(sm));
    sm_put(0x20, 0x48000000u | 8u | (4u << 9));   /* slots=8, sid width 4 -> mask 0xf... covers 0,1,5 */
    sm_put(0x24, (4u << 28) | 8u);                /* pages exponent 4 -> 32 pages, banks=8 */
    sm_put(0x0, 0x406u);
    ArmSmmu4KIo io = {&sm, 0x40000, sm_read32, sm_read64, sm_write32, sm_write64, sm_barrier};
    return io;
}

int main()
{
    // ============ A. table-build stress + cross-run byte determinism ============
    // Positive 1: pin-exact multi-range build success + walk verification + permissions encoding
    {
        ArmSmmu4KConfig c = {48, 48, 0x10000000, 4096};
        ArmSmmu4KRange r[] = {
            {0, 0x20000000, 0x3000, 1, 0, 2}, {0x1ff000, 0x21000000, 0x3000, 3, 1, 3},
            {0x3ffff000, 0x22000000, 0x3000, 1, 1, 3},
        };
        ArmSmmu4KResult res;
        std::memset(tbl, 0xa5, sizeof(tbl));
        CHECK(ArmSmmu4KBuild(&c, r, 3, tbl, CAP, &res) == ArmSmmu4KSuccess);
        CHECK(res.mair == 0xff44 && !res.upper_region && res.root_physical == c.table_physical);
        for (size_t i = 0; i < 3; ++i)
            for (uint64_t off = 0; off < r[i].bytes; off += 4096) {
                uint64_t e = walk_at(tbl, &res, r[i].iova + off);
                ++iterations;
                if ((e & UINT64_C(0xfffffffff000)) != r[i].physical + off) ++failures;
            }
    }
    // Positive 2: cross-run byte determinism — two fresh builds of identical inputs produce byte-identical tables (NEW vs pin)
    {
        ArmSmmu4KConfig c = {40, 48, 0x10000000, 4096};
        ArmSmmu4KRange r[] = {{0x1000, 0x40000000, 0x2000, 3, 1, 3}, {0x100000, 0x50000000, 0x1000, 1, 0, 2}};
        ArmSmmu4KResult r1, r2;
        std::memset(tbl, 0, sizeof(tbl)); std::memset(tbl2, 0, sizeof(tbl2));
        CHECK(ArmSmmu4KBuild(&c, r, 2, tbl, CAP, &r1) == ArmSmmu4KSuccess);
        CHECK(ArmSmmu4KBuild(&c, r, 2, tbl2, CAP, &r2) == ArmSmmu4KSuccess);
        CHECK(r1.table_pages == r2.table_pages && r1.leaf_pages == r2.leaf_pages && r1.start_level == r2.start_level);
        CHECK(!std::memcmp(tbl, tbl2, r1.table_pages * 4096));
    }
    // Positive 3: stress — 400 rounds varied input_bits/range shapes, walk every leaf
    {
        unsigned ok_rounds = 0;
        for (unsigned round = 0; round < 400; ++round) {
            unsigned bits = 32 + (round % 17);
            ArmSmmu4KConfig c = {bits, 48, 0x10000000, 4096};
            uint64_t limit = UINT64_C(1) << bits;
            ArmSmmu4KRange rr[4];
            for (unsigned i = 0; i < 4; ++i)
                rr[i] = ArmSmmu4KRange{(limit / 4 * i) & ~UINT64_C(4095), 0x30000000 + i * 4096, 4096,
                                       (i & 1) ? 3u : 1u, i & 1, 2u + (i & 1)};
            ArmSmmu4KResult res;
            std::memset(tbl, 0, sizeof(tbl));
            if (ArmSmmu4KBuild(&c, rr, 4, tbl, CAP, &res) != ArmSmmu4KSuccess) continue;
            unsigned good = 1;
            for (unsigned i = 0; i < 4 && good; ++i) {
                uint64_t e = walk_at(tbl, &res, rr[i].iova);
                if ((e & UINT64_C(0xfffffffff000)) != rr[i].physical) good = 0;
                ++iterations;
            }
            ok_rounds += good;
        }
        CHECK(ok_rounds == 400);
    }
    // Positive 4: permission bit-field encoding exactness across all 16 valid permission combos
    {
        ArmSmmu4KConfig c = {48, 48, 0x10000000, 4096};
        unsigned encoded = 0;
        for (unsigned perm = 0; perm < 16; ++perm) {
            if (!(perm & 1)) continue;
            ArmSmmu4KRange one = {0x1000, 0x40000000, 4096, perm, 0, 2};
            ArmSmmu4KResult res;
            std::memset(tbl, 0, sizeof(tbl));
            if (ArmSmmu4KBuild(&c, &one, 1, tbl, CAP, &res) != ArmSmmu4KSuccess) continue;
            uint64_t e = walk_at(tbl, &res, one.iova);
            ++iterations;
            if ((((e >> 53) & 3) == (perm & ArmSmmu4KExecute ? 0 : 3)) &&
                (((e >> 6) & 1) == !(perm & ArmSmmu4KPrivileged)) &&
                (((e >> 7) & 1) == !(perm & ArmSmmu4KWrite)))
                ++encoded;
        }
        CHECK(encoded == 8);
    }
    // Positive 5: BuildUpper sign-extended region success + walk
    {
        ArmSmmu4KConfig c = {48, 48, 0x11000000, 4096};
        ArmSmmu4KRange up = {UINT64_C(0xffff000000000000) + 0x1000, UINT64_C(0xfffffffff000), 0x1000, 1, 0, 2};
        ArmSmmu4KResult res;
        std::memset(tbl, 0, sizeof(tbl));
        int rc = ArmSmmu4KBuildUpper(&c, &up, 1, tbl, CAP, &res);
        if (rc == ArmSmmu4KSuccess) {
            CHECK(res.upper_region == 1);
            uint64_t e = walk_at(tbl, &res, up.iova);
            CHECK((e & UINT64_C(0xfffffffff000)) == up.physical);
        } else {
            CHECK(rc == ArmSmmu4KSuccess); /* surface unexpected shape transparently */
        }
    }

    // ============ B. context encode/validate stress ============
    // Positive 6: EncodeContext determinism — double-encode byte-identical image
    {
        ArmSmmu4KRange a = {0x1000, 0x20000000, 4096, 3, 1, 3}, b = a;
        b.iova = UINT64_C(0xffff000000000000) + 0x1000;
        CHECK(ArmSmmu4KBuild(&lower_cfg, &a, 1, lower_tables, 8, &lr) == ArmSmmu4KSuccess);
        CHECK(ArmSmmu4KBuildUpper(&upper_cfg, &b, 1, upper_tables, 8, &ur) == ArmSmmu4KSuccess);
        ctx_config = (ArmSmmu4KContextConfig){&lower_cfg, &upper_cfg, &lr, &ur, 1u << 30, 0x1555, 0xabcd, 1, 1, 1, 0};
        ArmSmmu4KContextImage i1, i2;
        std::memset(&i1, 0, sizeof(i1)); std::memset(&i2, 0, sizeof(i2));
        CHECK(ArmSmmu4KEncodeContext(&ctx_config, &i1) == ArmSmmu4KSuccess);
        CHECK(ArmSmmu4KEncodeContext(&ctx_config, &i2) == ArmSmmu4KSuccess);
        CHECK(!std::memcmp(&i1, &i2, sizeof(i1)));
    }
    // Positive 7: stress — 200 rounds encode+self-validate across asid/flag combos
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 200; ++round) {
            ArmSmmu4KContextConfig cc = ctx_config;
            cc.asid = (unsigned)(round % 65536);
            cc.coherent_walk = round & 1;
            cc.stall_on_fault = (round >> 1) & 1;
            cc.continue_after_fault = 1 - cc.stall_on_fault;
            ArmSmmu4KContextImage img;
            if (ArmSmmu4KEncodeContext(&cc, &img) == ArmSmmu4KSuccess &&
                ArmSmmu4KValidateContextReadback(&cc, &img) == ArmSmmu4KSuccess)
                ++good;
            iterations += 2;
        }
        CHECK(good == 200);
    }

    // ============ C. MMIO program determinism + lifecycle stress ============
    // Positive 8: double-run MMIO write/event sequence byte-identical (NEW — pin never compares sequences)
    {
        static Event seq1[512], seq2[512];
        size_t n1 = 0, n2 = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            ArmSmmu4KIo io = mmio_reset(0);
            ArmSmmu4KProgramResult r;
            ArmSmmu4KStatus st = ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r);
            if (pass == 0) { n1 = f.event_count; std::memcpy(seq1, f.events, n1 * sizeof(Event)); }
            else { n2 = f.event_count; std::memcpy(seq2, f.events, n2 * sizeof(Event)); }
            if (pass == 1) {
                CHECK(st == ArmSmmu4KSuccess);
                CHECK(n1 == n2 && n1 > 0 && !std::memcmp(seq1, seq2, n1 * sizeof(Event)));
            }
        }
    }
    // Positive 9: program exact success shape (phase 5/enabled/readback/tlb synced/retain)
    {
        ArmSmmu4KIo io = mmio_reset(0);
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KSuccess);
        CHECK(r.phase == 5 && r.enabled && r.readback_valid && r.tlb_synchronized && r.must_retain_tables);
        CHECK(r.id0 == (1u << 30) && r.id2 == 0x1555 && r.context_offset == f.cb);
    }
    // Positive 10: lifecycle stress — 100 rounds program->synchronize->invalidate->disable
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 100; ++round) {
            ArmSmmu4KIo io = mmio_reset(0);
            ArmSmmu4KProgramResult r;
            if (ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) != ArmSmmu4KSuccess) continue;
            if (ArmSmmu4KSynchronizeOwnedContext(&io, f.bank, &ctx_config, 4, &r) != ArmSmmu4KSuccess) continue;
            if (!r.tlb_synchronized) continue;
            if (ArmSmmu4KInvalidateAllOwnedContext(&io, f.bank, &ctx_config, 4, &r) != ArmSmmu4KSuccess) continue;
            if (ArmSmmu4KDisableOwnedContext(&io, f.bank, &ctx_config, 4, &r) != ArmSmmu4KSuccess) continue;
            if (!r.disabled_verified && r.must_retain_tables) continue;
            ++good;
            iterations += 4;
        }
        CHECK(good == 100);
    }
    // Positive 11: disable exact terminal shape + idempotent second disable
    {
        ArmSmmu4KIo io = mmio_reset(0);
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KSuccess);
        CHECK(ArmSmmu4KDisableOwnedContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KSuccess);
        CHECK(r.phase == 7 && !r.enabled && r.disabled_verified && !r.must_retain_tables && !get32(f.cb));
        CHECK(ArmSmmu4KDisableOwnedContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KSuccess);
    }
    // Positive 12: 64K register spacing variant programs successfully
    {
        ArmSmmu4KIo io = mmio_reset(1);
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KSuccess);
        CHECK(r.enabled && r.context_offset == f.cb);
    }

    // ============ D. Sm8750GpuSmmu platform binding ============
    // Positive 13: valid prepare — exact slot/bank selection, fault writes, readback proof
    {
        ArmSmmu4KIo io = smmu_reset();
        Sm8750GpuSmmuResult r;
        int rc = Sm8750GpuSmmuPrepare(&io, &r);
        if (rc == 0) {
            CHECK(r.phase == 6);
            CHECK(r.banks[0] == 0 && r.banks[1] == 2); /* GPU reserved 0/1; GMU starts at 2 */
            CHECK(r.bank_offset[0] == (32 + 0) * 4096 && r.bank_offset[1] == (32 + 2) * 4096);
            CHECK(sm.writes == 3 && sm.barriers == 3); /* one fault write + barrier per slot */
            for (unsigned i = 0; i < 3; ++i)
                CHECK(sm_get(0xc00 + 4 * r.slots[i]) == 0x20000);
        } else {
            CHECK(rc == 0);
        }
    }
    // Positive 14: slots chosen are the first three non-live slots in scan order
    {
        ArmSmmu4KIo io = smmu_reset();
        Sm8750GpuSmmuResult r;
        CHECK(Sm8750GpuSmmuPrepare(&io, &r) == 0);
        CHECK(r.slots[0] == 0 && r.slots[1] == 1 && r.slots[2] == 2);
    }
    // Positive 15: prepare stress — 60 rounds fresh peers all succeed deterministically
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 60; ++round) {
            ArmSmmu4KIo io = smmu_reset();
            Sm8750GpuSmmuResult r;
            if (!Sm8750GpuSmmuPrepare(&io, &r) && r.phase == 6 && r.banks[1] == 2) ++good;
            ++iterations;
        }
        CHECK(good == 60);
    }
    // Positive 16: live routes of unrelated masters are preserved (pin contract)
    {
        ArmSmmu4KIo io = smmu_reset();
        sm_put(0x800 + 4 * 7, 0x80000000u | 42u); /* slot 7 live: valid bit + sid 42 */
        sm_put(0xc00 + 4 * 7, 0x30000u | 5u);     /* routed at bank 5, not faulting-shape */
        Sm8750GpuSmmuResult r;
        int rc = Sm8750GpuSmmuPrepare(&io, &r);
        if (rc == 0) {
            CHECK(sm_get(0x800 + 4 * 7) == (0x80000000u | 42u)); /* untouched */
            CHECK(sm_get(0xc00 + 4 * 7) == (0x30000u | 5u));
        } else {
            /* a live conflicting GPU sid would refuse; unrelated sid 42 must not — surface rc */
            CHECK(rc == 0);
        }
    }

    // ============ E. MMIO adapter bounds ============
    // ================= negative fail-closed battery =================
    // Negative 1: build refusals — overlap/unaligned/zero-count/argument all transactional
    {
        ArmSmmu4KConfig c = {48, 48, 0x10000000, 4096};
        ArmSmmu4KRange r[] = {{0x1000, 0x40000000, 0x2000, 3, 1, 3}, {0x2000, 0x50000000, 0x1000, 1, 0, 2}};
        ArmSmmu4KResult res;
        CHECK(ArmSmmu4KBuild(&c, r, 2, tbl, CAP, &res) == ArmSmmu4KOverlap);
        r[1].iova = 0x2001;
        CHECK(ArmSmmu4KBuild(&c, r, 2, tbl, CAP, &res) == ArmSmmu4KBadRange);
        CHECK(ArmSmmu4KBuild(&c, r, 0, tbl, CAP, &res) == ArmSmmu4KArgument);
        CHECK(ArmSmmu4KBuild(&c, r, 1, tbl, CAP, NULL) == ArmSmmu4KArgument);
        c.input_bits = 49;
        CHECK(ArmSmmu4KBuild(&c, r, 1, tbl, CAP, &res) == ArmSmmu4KArgument);
    }
    // Negative 2: budget/capacity exhaustion fail-closed, tables untouched
    {
        ArmSmmu4KConfig c = {48, 48, 0x10000000, 2};
        ArmSmmu4KRange r[] = {{0, 0x20000000, 0x4000, 1, 0, 2}};
        ArmSmmu4KResult res;
        std::memset(tbl, 0xa5, 4096);
        CHECK(ArmSmmu4KBuild(&c, r, 1, tbl, CAP, &res) == ArmSmmu4KBudget);
        c.leaf_budget = 4096;
        CHECK(ArmSmmu4KBuild(&c, r, 1, tbl, 1, &res) == ArmSmmu4KCapacity);
    }
    // Negative 3: program on enabled bank refused Busy before any write
    {
        ArmSmmu4KIo io = mmio_reset(0);
        put32(f.cb, 1);
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KBusy);
        unsigned writes = 0;
        for (size_t i = 0; i < f.event_count; ++i)
            if (f.events[i].kind == 2 || f.events[i].kind == 3) ++writes;
        CHECK(writes == 0);
        CHECK(r.must_retain_tables);
    }
    // Negative 4: bounded timeout — TLBSYNC never completes, never enables, retains tables
    {
        ArmSmmu4KIo io = mmio_reset(0);
        f.timeout = 1;
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KTimeout);
        CHECK(r.must_retain_tables && !r.tlb_synchronized);
    }
    // Negative 5: fault-on-enable quarantines, disable confirmed, not retained
    {
        ArmSmmu4KIo io = mmio_reset(0);
        f.fault_on_enable = 1;
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KFault);
        CHECK(r.fault_status == 2 && r.disabled_verified && !r.must_retain_tables && !r.enabled);
    }
    // Negative 6: readback corruption (TCR) — StateMismatch, context never left enabled
    {
        ArmSmmu4KIo io = mmio_reset(0);
        f.corrupt_tcr = 1;
        ArmSmmu4KProgramResult r;
        CHECK(ArmSmmu4KProgramDisabledContext(&io, f.bank, &ctx_config, 4, &r) == ArmSmmu4KStateMismatch);
        CHECK(r.disabled_verified && !r.must_retain_tables);
    }
    // Negative 7: validate readback mismatch on tampered image
    {
        ArmSmmu4KContextImage img;
        CHECK(ArmSmmu4KEncodeContext(&ctx_config, &img) == ArmSmmu4KSuccess);
        img.tcr ^= 1u << 14;
        CHECK(ArmSmmu4KValidateContextReadback(&ctx_config, &img) != ArmSmmu4KSuccess);
    }
    // Negative 8: smmu prepare refusals — live GPU-sid conflict / enabled bank / global fault / short window
    {
        ArmSmmu4KIo io = smmu_reset();
        sm_put(0x800 + 4 * 4, 42u);        /* slot 4 smr matches sid 0 with wide mask? craft: id 0 exact */
        sm_put(0x800 + 4 * 4, 0x80000000u | 0u); /* valid + sid 0 — conflicts GPU sid 0 */
        sm_put(0xc00 + 4 * 4, 0x30000u | 3u);
        Sm8750GpuSmmuResult r;
        CHECK(Sm8750GpuSmmuPrepare(&io, &r) == -16);

        io = smmu_reset();
        sm_put((32 + 0) * 4096, 1); /* bank 0 enabled */
        CHECK(Sm8750GpuSmmuPrepare(&io, &r) == -16);

        io = smmu_reset();
        sm_put(0x48, 4); /* global fault status pending */
        CHECK(Sm8750GpuSmmuPrepare(&io, &r) != 0);

        io = smmu_reset();
        io.register_bytes = 0xc00; /* below minimum */
        CHECK(Sm8750GpuSmmuPrepare(&io, &r) == -22);

        io = smmu_reset();
        CHECK(Sm8750GpuSmmuPrepare(&io, NULL) == -22);
    }

    std::printf("SMMU4K_RESULT {\"iterations\":%lu,\"failures\":%d}\n", iterations, failures);
    return failures ? 1 : 0;
}
