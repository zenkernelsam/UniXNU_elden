/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_PLATFORM_H
#define ELDEN_HAPTICS_PLATFORM_H
#include "Board.h"
#include "Bus.h"
#ifdef __cplusplus
extern "C" {
#endif
enum EldenHapticsSpace {EldenHapticsGcc=0,EldenHapticsSe=1,EldenHapticsPins=2,
                       EldenHapticsLeftReset=3,EldenHapticsRightReset=4,
                       EldenHapticsLeftIrq=5,EldenHapticsRightIrq=6};
enum {EldenHapticsChangedRoot=1,EldenHapticsChangedVote=2,EldenHapticsChangedRate=4,
      EldenHapticsChangedPins=8,EldenHapticsChangedSerialClock=16};
typedef struct {
    void *context;
    int (*read)(void *,enum EldenHapticsSpace,uint32_t,uint32_t *);
    int (*write)(void *,enum EldenHapticsSpace,uint32_t,uint32_t);
    int (*admit)(void *,int cleanup);
    int (*gcc_lock)(void *,int acquire);
    uint32_t (*touch_gcc_contract)(void *); /* actual linked source contract */
    uint64_t (*now_us)(void *);
    int (*delay_us)(void *,unsigned);
} EldenHapticsPlatformIo;
typedef struct {
    EldenHapticsPlatformIo io;
    uint32_t command,cfg,vote,pins[2],serial[3],applied_serial[3],bus_hz;
    unsigned changed,phase;
    int error;
    uint8_t owned,ready,held,cleanup;
} EldenHapticsPlatform;
/* Caller zero-initializes and owns both chips/SE6 for the whole lifetime.
 * No SE firmware/DMA/GSI takeover, reset pulse, IRQ routing or PMIC writes.
 * Uses only the exact 19.2MHz Linux I2C table. DFS is refused, not guessed.
 * Existing reset lines must already be GPIO outputs high/deasserted. */
int elden_haptics_platform_acquire(EldenHapticsPlatform *,const EldenHapticsPlatformIo *,uint32_t bus_hz);
int elden_haptics_platform_bus_io(EldenHapticsPlatform *,EldenHapticsBusIo *);
/* The caller has actually restored the bus metadata before calling release.
 * Failed prerequisite/readback retains platform/maps to physical reset. */
int elden_haptics_platform_release(EldenHapticsPlatform *,int bus_restored);
#ifdef __cplusplus
}
#endif
#endif
