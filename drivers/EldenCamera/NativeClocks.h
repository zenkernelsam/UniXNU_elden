/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_NATIVE_CLOCKS_H
#define ELDEN_CAMERA_NATIVE_CLOCKS_H
#include "Clock.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t,uint64_t,unsigned);
    int (*lock)(void *,uint64_t,uint64_t,int);
    int (*controllers)(void *,uint64_t,uint64_t,int);
    int (*dependency)(void *,uint64_t,uint64_t,const EldenCameraClockRequest *,int,uint32_t *);
    int (*consumer_idle)(void *,uint64_t,uint64_t,unsigned,unsigned);
    int (*sensor_idle)(void *,uint64_t,uint64_t,unsigned);
    int (*root_unused)(void *,uint64_t,uint64_t,unsigned);
    int (*domain_unused)(void *,uint64_t,uint64_t,unsigned);
    int (*pins_unused)(void *,uint64_t,uint64_t,unsigned);
} EldenCameraNativeClockOwner;
typedef struct EldenCameraNativeClocks EldenCameraNativeClocks;
/* Real shared original clock/TLMM/RPMh/parent lease is mandatory. Maps exactly
 * CAMCC/BIST/TLMM once, scalar MMIO, no competing shared-engine owner. Retained
 * context/maps/core stay to reset, including failed initialization. No auto-start. */
int elden_camera_native_clocks_open(const EldenCameraNativeClockOwner *,EldenCameraNativeClocks **);
int elden_camera_native_clock(EldenCameraNativeClocks *,uint64_t,uint64_t,unsigned,unsigned,uint32_t,int);
int elden_camera_native_domain(EldenCameraNativeClocks *,uint64_t,uint64_t,unsigned,unsigned,int);
int elden_camera_native_pins_prepare(EldenCameraNativeClocks *,uint64_t,uint64_t);
int elden_camera_native_sensor_reset(EldenCameraNativeClocks *,uint64_t,uint64_t,unsigned,int);
int elden_camera_native_clock_observe(EldenCameraNativeClocks *,uint64_t,uint64_t,unsigned,uint32_t *);
int elden_camera_native_clock_observe_cleanup(EldenCameraNativeClocks *,uint64_t,uint64_t,unsigned,uint32_t *);
int elden_camera_native_clocks_status(EldenCameraNativeClocks *,EldenCameraClockStatus *);
int elden_camera_native_clocks_stop(EldenCameraNativeClocks *,uint64_t,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
