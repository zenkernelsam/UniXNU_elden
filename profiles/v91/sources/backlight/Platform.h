/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "GeniI2c.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenBlSpace { EldenBlGcc, EldenBlPins, EldenBlEnable, EldenBlSe };
typedef struct {
    void *context;
    uint32_t (*read)(void *, enum EldenBlSpace, uint32_t);
    void (*write)(void *, enum EldenBlSpace, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, unsigned);
    int (*gcc_lock)(void *, bool acquire);
} EldenBlPlatformIo;
enum { EldenBlBadArgument=-22, EldenBlBusy=-16, EldenBlNotReady=-19,
       EldenBlTransport=-5, EldenBlTimedOut=-110 };
typedef struct {
    EldenBlPlatformIo io;
    GeniI2c bus;
    uint32_t vote, rcg, rcg_cfg, pins[3], hwen, engine[12], config;
    uint32_t added_votes, last_status;
    /* Entry snapshot diagnoses refusal without draining or changing the bus. */
    uint32_t entry_status,entry_gsi,entry_dma,entry_tx,entry_rx,entry_secondary;
    uint32_t entry_interface,entry_revision,entry_clock,entry_scl;
    unsigned stage, transfers;
    int error;
    bool root_changed, votes_changed, engine_changed;
    bool acquired, failed, quarantined;
} EldenBlPlatform;
/* Single executor owns SE2 and HWEN's inherited high epoch. No pin, rail,
 * reset, parent/rate, firmware, DMA or SMMU programming. Busy/GPI/nonempty
 * engines are refused. Timed-out ownership is never inferred from a software
 * completion: release also requires hardware idle/empty status. */
int elden_bl_platform_acquire(EldenBlPlatform *);
int elden_bl_platform_release(EldenBlPlatform *);
int elden_bl_platform_read(void *, uint8_t reg, uint8_t *value);
int elden_bl_platform_write(void *, uint8_t reg, uint8_t value);
#ifdef __cplusplus
}
#endif
