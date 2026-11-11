/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint64_t first,blocks;} EldenDataRange;
typedef struct {
    EldenDataRange partition,root,root_guard,metadata,stage[2],data,spare,log;
    uint64_t logical_max,window_max;
    bool valid;
} EldenDataLayout;
enum {EldenDataLayoutOk=0,EldenDataLayoutArgument=-22,EldenDataLayoutGeometry=-34};
/* Stable allocations do not move when the initial logical Data disk is small.
 * All ranges are absolute 4096-byte LBAs on LUN0; no GPT mutation is implied.
 */
int elden_data_layout(EldenDataLayout *,uint64_t first,uint64_t last,uint64_t root_bytes);
bool elden_data_contains(EldenDataRange,uint64_t first,uint64_t blocks);
bool elden_data_overlap(EldenDataRange,EldenDataRange);
bool elden_data_window_blocks(uint64_t disk_bytes,uint64_t *blocks);
#ifdef __cplusplus
}
#endif
