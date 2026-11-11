/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included in the existing NativeUfs translation unit: all DMA goes through
 * hostRead4k and its existing lock, deadline, protocol checks and quarantine.
 * This adds no controller owner, SMMU writes, or storage write operation.
 */
#if defined(ELDEN_MODEM_READER) && ELDEN_MODEM_READER
extern "C" {
#include "EldenModemGpt.h"
}
namespace {
static EldenModemPartition gEldenModem;
static unsigned gEldenModemState; /* 0 untried,1 reading,2 valid,3 metadata refused */
static bool eldenModemOpen() {
    unsigned state = __atomic_load_n(&gEldenModemState, __ATOMIC_ACQUIRE);
    if (state == 2) return true;
    if (state != 0 || !gHost.ready || gHost.poisoned || gLuns[4].blockBytes != 4096) return false;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&gEldenModemState, &expected, 1, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    uint8_t *bytes = static_cast<uint8_t *>(IOMalloc(16384));
    EldenModemGptHeader header = {};
    int result = bytes ? 0 : -1000;
    if (!result && hostRead4k(4, 1, 1, bytes) != kIOReturnSuccess) result = -2;
    if (!result) result = elden_modem_gpt_header(bytes, 4096, gLuns[4].last, &header);
    const unsigned count = (header.entry_count * 128 + 4095) / 4096;
    if (!result && hostRead4k(4, header.entries_lba, count, bytes) != kIOReturnSuccess) result = -2;
    if (!result) result = elden_modem_gpt_partition(&header, bytes, header.entry_count * 128, &gEldenModem);
    if (bytes) IOFree(bytes, 16384);
    __atomic_store_n(&gEldenModemState, !result ? 2u : result == -1000 ? 0u : 3u, __ATOMIC_RELEASE);
    return result == 0;
}
}
/* Canonical sheng firmware-reader APIs, restricted to the validated LUN4 modem_a. */
extern "C" int native_ufs_part_find(const char *name, uint32_t *lun, uint64_t *first, uint64_t *blocks) {
    if (!name || strcmp(name, "modem_a") || !lun || !first || !blocks || !eldenModemOpen()) return 0;
    *lun = 4; *first = gEldenModem.first; *blocks = gEldenModem.blocks;
    return 1;
}
extern "C" int native_ufs_part_read(uint32_t lun, uint64_t lba, uint32_t blocks, void *destination) {
    if (!destination || !blocks || lun != 4 || !eldenModemOpen() || lba < gEldenModem.first) return 0;
    uint64_t relative = lba - gEldenModem.first;
    if (relative >= gEldenModem.blocks || blocks > gEldenModem.blocks - relative) return 0;
    while (blocks) {
        unsigned count = blocks > kDataMax / 4096 ? kDataMax / 4096 : blocks;
        if (hostRead4k(4, lba, count, destination) != kIOReturnSuccess) return 0;
        destination = static_cast<uint8_t *>(destination) + count * 4096;
        lba += count; blocks -= count;
    }
    return 1;
}
#endif
