/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "EldenModemGpt.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static uint32_t crc(const uint8_t *p, size_t bytes, int header) {
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < bytes; ++i) {
        value ^= header && i >= 16 && i < 20 ? 0 : p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
int elden_modem_gpt_header(const uint8_t *block, size_t bytes, uint64_t lun_last,
    EldenModemGptHeader *out) {
    if (!block || !out || bytes != 4096 || lun_last < 8 || lun_last >= UINT32_MAX)
        return -1;
    if (memcmp(block, "EFI PART", 8) || le32(block + 8) != 0x10000 ||
        le64(block + 24) != 1 || le64(block + 32) != lun_last || le32(block + 20))
        return -2;
    uint32_t size = le32(block + 12), count = le32(block + 80);
    if (size < 92 || size > bytes || le32(block + 84) != 128 || !count || count > 128 ||
        crc(block, size, 1) != le32(block + 16)) return -3;
    EldenModemGptHeader header = {le64(block + 72), le64(block + 40), le64(block + 48),
        count, le32(block + 88)};
    uint64_t table_blocks = ((uint64_t)count * 128 + 4095) / 4096;
    if (header.entries_lba < 2 || header.entries_lba > lun_last ||
        table_blocks > lun_last - header.entries_lba ||
        header.first_usable < header.entries_lba + table_blocks ||
        header.last_usable < header.first_usable ||
        header.last_usable >= lun_last - table_blocks) return -4;
    *out = header;
    return 0;
}
int elden_modem_gpt_partition(const EldenModemGptHeader *header,
    const uint8_t *entries, size_t bytes, EldenModemPartition *out) {
    static const char name[] = "modem_a";
    if (!header || !entries || !out || !header->entry_count || header->entry_count > 128 ||
        bytes != (size_t)header->entry_count * 128 || crc(entries, bytes, 0) != header->entries_crc)
        return -1;
    EldenModemPartition partition = {0, 0};
    unsigned matches = 0;
    for (unsigned index = 0; index < header->entry_count; ++index) {
        const uint8_t *entry = entries + index * 128;
        unsigned nonzero = 0;
        for (unsigned i = 0; i < 16; ++i) nonzero |= entry[i];
        if (!nonzero) continue;
        unsigned i = 0;
        for (; i < sizeof(name) - 1; ++i)
            if (entry[56 + 2 * i] != (uint8_t)name[i] || entry[57 + 2 * i]) break;
        if (i != sizeof(name) - 1 || entry[56 + 2 * i] || entry[57 + 2 * i]) continue;
        uint64_t first = le64(entry + 32), last = le64(entry + 40);
        if (first < header->first_usable || last < first || last > header->last_usable ||
            last - first + 1 > UINT64_MAX / 4096) return -2;
        partition.first = first;
        partition.blocks = last - first + 1;
        ++matches;
    }
    if (matches != 1) return -3;
    *out = partition;
    return 0;
}
