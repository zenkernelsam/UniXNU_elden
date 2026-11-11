/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    uint32_t (*read)(void *,unsigned);
    int (*write)(void *,unsigned,uint32_t); /* ordered; nonzero may have written */
    int (*claim)(void *,uint64_t); /* complete SMMU config + SE master lease */
    void (*drop)(void *,uint64_t);
    int (*idle)(void *); /* no queued/in-flight master/DMA work */
} EldenTouchRouteIo;
typedef struct {
    EldenTouchRouteIo io;
    uint64_t epoch,tx,rx;
    size_t bytes;
    unsigned stage,writes;
    uint32_t cb0_sctlr;
    int error;
    bool attempted,claimed,attached,detached,poisoned,cb0_seen;
} EldenTouchDmaRouteOwner;
/* One attempt per zero-initialized owner. Exact live baseline and physical
 * address spans; never configures a context bank, TLB, IRQ or global policy.
 * No default claim. The caller is responsible for the real global lease. */
int elden_touch_route_attach(EldenTouchDmaRouteOwner *,const EldenTouchRouteIo *,
                            uint64_t epoch,uint64_t tx,uint64_t rx,size_t bytes);
int elden_touch_route_observe(EldenTouchDmaRouteOwner *,uint64_t epoch);
/* Invalidates only the owned SMR. Leaves safe S2CR bank0 behind that invalid
 * entry; the firmware's opaque reset readout must never be written back. */
int elden_touch_route_detach(EldenTouchDmaRouteOwner *,uint64_t epoch);
#ifdef __cplusplus
}
#endif
