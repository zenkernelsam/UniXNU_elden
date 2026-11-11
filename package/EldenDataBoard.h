/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Board layout contract, from the complete post-partition LUN0 readback.
 * Revalidate the GPT and root header on device; this is not permission to write.
 */
#pragma once
#include <stdint.h>
#define ELDEN_DATA_LUN 0u
#define ELDEN_DATA_PARTITION "ipados"
#define ELDEN_DATA_FIRST UINT64_C(215283456)
#define ELDEN_DATA_LAST UINT64_C(248838138)
#define ELDEN_DATA_ROOT_BYTES UINT64_C(9381609472)
#define ELDEN_DATA_BLOCK 4096u
#define ELDEN_DATA_LOGICAL_MAX (UINT64_C(112)*1024*1024*1024)
#define ELDEN_DATA_STAGE_BYTES (UINT64_C(2)*1024*1024*1024)
#define ELDEN_DATA_LOG_BYTES (UINT64_C(256)*1024*1024)
static const uint8_t elden_data_partition_guid[16]={
    0xb8,0xd4,0x04,0x2c,0x5c,0xe9,0x6a,0x46,0x94,0x36,0x30,0x83,0x18,0xea,0x82,0xc8};
