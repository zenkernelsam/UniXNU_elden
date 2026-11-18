/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_BUS_H
#define ELDEN_HAPTICS_BUS_H
#include "GeniCore.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenHapticsBusArgument=-22,EldenHapticsBusBusy=-16,EldenHapticsBusError=-5,
      EldenHapticsBusTimeout=-110,EldenHapticsBusHeld=-125};
typedef struct {
    void *context;
    int (*read)(void *,uint32_t reg,uint32_t *);
    int (*write)(void *,uint32_t reg,uint32_t);
    int (*admit)(void *,int cleanup);
    uint64_t (*now_us)(void *);
    int (*delay_us)(void *,unsigned);
} EldenHapticsBusIo;
typedef struct {
    EldenHapticsBusIo io;EldenHapticsGeni core;
    uint32_t saved[12],configuration;uint32_t transfers,split_transfers,cancelled;
    int error;
    uint8_t acquired,held,transferring,cleanup;
} EldenHapticsBus;
/* Zero-initialize once before acquire; never overwrite an acquired/held lease.
 * Caller exclusively owns SE6/clock/pins and serializes BOTH motor instances.
 * Ready FIFO I2C firmware/clock is required. No SE firmware, DMA or GSI takeover.
 * Poll-only: core status bits are enabled, external SE IRQ output is suppressed.
 * All changed configuration is captured/restored. Failure retains the lease. */
int elden_haptics_bus_acquire(EldenHapticsBus *,const EldenHapticsBusIo *);
int elden_haptics_bus_read(EldenHapticsBus *,uint8_t address,uint8_t reg,uint8_t *,size_t);
int elden_haptics_bus_write(EldenHapticsBus *,uint8_t address,uint8_t reg,const uint8_t *,size_t);
int elden_haptics_bus_release(EldenHapticsBus *);
#ifdef __cplusplus
}
#endif
#endif
