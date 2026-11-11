/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "GeniSpi.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenTouchDmaCheck {EldenTouchDmaBefore=1,EldenTouchDmaAfter=2};
typedef struct {
    void *context;
    /* A real route owner must verify its current mapping/lease for both
     * separate spans. No permissive default is provided. Addresses are DMA
     * bus addresses, not necessarily CPU physical addresses. */
    int (*check)(void *,enum EldenTouchDmaCheck,uint64_t epoch,unsigned sid,
                 uint64_t tx_address,uint64_t rx_address,size_t span);
    uint64_t epoch;
    unsigned sid;
    /* Private CPU staging prevents unverified RX bytes escaping to caller. */
    uint8_t *staging;
    size_t staging_bytes;
    /* Native cache-line size; DMA buffers own whole aligned cache lines. */
    size_t cache_line_bytes;
} EldenTouchDmaContract;
typedef struct {
    struct geni_spi *engine;
    struct geni_spi qualified_engine;
    EldenTouchDmaContract contract;
    unsigned completed;
    int last_error;
    bool initialized,active,quarantined;
} EldenTouchDmaTransfer;
/* Zero-initialize before first attach. Single serialized executor; this adds
 * no engine/clock/pin/SMMU setup. The lease must exclude all other engine
 * users and retain clocks, cache attributes and mappings for the full call.
 * Caller first qualifies and initializes SE-DMA, owns all maps and buffers,
 * supplies cache maintenance, then uses this wrapper. Failures after start
 * retain every buffer/mapping/lease until a separately verified reset. */
int elden_touch_dma_attach(EldenTouchDmaTransfer *,struct geni_spi *,const EldenTouchDmaContract *);
int elden_touch_dma_transfer(EldenTouchDmaTransfer *,const uint8_t *tx,uint8_t *rx,size_t count);
#ifdef __cplusplus
}
#endif
