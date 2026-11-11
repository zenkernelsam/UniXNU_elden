/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "Layout.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    EldenDataRange window;
    uint32_t mode,closed,poisoned,inflight;
    uint64_t issued,durable;
} EldenDataFence;
/* One host command owner serializes begin/end. reset_ready is lock-free and
 * does not allocate or submit I/O; it never trusts a mere success callback.
 */
bool elden_data_fence_open(EldenDataFence *,unsigned mode,EldenDataRange);
bool elden_data_fence_begin(EldenDataFence *,uint64_t first,uint32_t blocks);
void elden_data_fence_end(EldenDataFence *,bool fua_verified);
void elden_data_fence_poison(EldenDataFence *);
bool elden_data_fence_close(EldenDataFence *);
bool elden_data_fence_reset_ready(const EldenDataFence *);
#ifdef __cplusplus
}
#endif
