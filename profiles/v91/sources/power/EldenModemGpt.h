/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t entries_lba, first_usable, last_usable;
    uint32_t entry_count, entries_crc;
} EldenModemGptHeader;
typedef struct { uint64_t first, blocks; } EldenModemPartition;

/* Pure parser for the current 4KiB GPT; no reads/writes and no supplied-name
 * namespace. Only the modem_a firmware partition can be returned. */
int elden_modem_gpt_header(const uint8_t *block, size_t bytes, uint64_t lun_last,
    EldenModemGptHeader *out);
int elden_modem_gpt_partition(const EldenModemGptHeader *header,
    const uint8_t *entries, size_t bytes, EldenModemPartition *out);
#ifdef __cplusplus
}
#endif
