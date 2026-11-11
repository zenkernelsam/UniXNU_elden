/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uintptr_t first,end;size_t line;} EldenDmaCacheSpan;
/* Compute cache lines only inside an exclusively owned, whole-line buffer.
 * No callbacks or hardware access on invalid input; output stays unchanged. */
int elden_dma_cache_span(uintptr_t base,size_t capacity,uintptr_t address,
                         size_t bytes,size_t line,EldenDmaCacheSpan *out);
#ifdef __cplusplus
}
#endif
