/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_CLOCK_H
#define ELDEN_CAMERA_CLOCK_H
#include <stdint.h>
#include "ClockIds.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenCamCamcc=0,EldenCamBist=1,EldenCamTlmm=2};
enum {EldenCamClockAcquire=0,EldenCamClockObserve=1,EldenCamClockCleanup=2};
enum {EldenCamDependencyRelease=0,EldenCamDependencyAcquire=1,EldenCamDependencyObserve=2};
typedef struct {
    uint32_t clock,root,selector,hz,m,n,hid,original_cfg,original_m,original_n,original_d;
} EldenCameraClockRequest;
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t,uint64_t,unsigned operation);
    /* Actual original shared camera clock/TLMM owner lock, sleepable.
     * Serializes peers' RMW/ownership changes; no independent guessed owner.
     * Dependencies/unused/admit callbacks must not reenter it or this core. */
    int (*lock)(void *,uint64_t,uint64_t,int acquire);
    int (*read)(void *,unsigned space,uint32_t reg,uint32_t *);
    int (*write)(void *,unsigned space,uint32_t reg,uint32_t value);
    int (*delay_us)(void *,unsigned);
    uint64_t (*now_us)(void *);
    /* Original GCC camera interface/NoC and RPMh MMCX+MXC/MX held before
     * any mapped controller read. Must retain original parents/safeXO through
     * all restores; release only after physical domain/clock/pin proof.
     * Observe2 confirms the same live lease and original safe-source parents. */
    int (*controllers)(void *,uint64_t,uint64_t,int action);
    /* Real parent/leaf acquisition and observation. Output is actual parent
     * rate (external GCC clock has root=UINT32_MAX and returns leaf rate).
     * No PLL guessed programming/default rates. Acquire failure may execute;
     * release must retire that attempted reference in the same epoch. */
    int (*dependency)(void *,uint64_t,uint64_t,const EldenCameraClockRequest *,int action,uint32_t *actual_hz);
    /* Actual sensor/CCI/receiver/DMA inactivity before dropping its reference.
     * Remaining consumers/lens/other clients must be accounted independently. */
    int (*consumer_idle)(void *,uint64_t,uint64_t,unsigned sensor,unsigned clock);
    int (*sensor_idle)(void *,uint64_t,uint64_t,unsigned sensor);
    int (*root_unused)(void *,uint64_t,uint64_t,unsigned root);
    int (*domain_unused)(void *,uint64_t,uint64_t,unsigned domain);
    int (*pins_unused)(void *,uint64_t,uint64_t,unsigned sensor);
} EldenCameraClockIo;
typedef union {uint64_t alignment;uint8_t bytes[32768];} EldenCameraClocks;
typedef struct {
    uint32_t ready,stopped,controller_attempted,pins_ready,domain_users[4];
    uint32_t clock_users[EldenCamClockCount],active_users[EldenCamClockCount];
    uint64_t changed_roots,changed_branches;uint32_t changed_domains,changed_pins;
    int error;
} EldenCameraClockStatus;
/* One retained camera graph, no allocation/MMIO defaults/automatic startup.
 * Original owner serializes callbacks. Raw register access is not public. */
int elden_camera_clocks_open(EldenCameraClocks *,const EldenCameraClockIo *);
int elden_camera_clock_resource(EldenCameraClocks *,uint64_t,uint64_t,unsigned sensor,unsigned clock,uint32_t hz,int on);
/* Actual leaf+root+parent observation without taking a second reference. */
int elden_camera_clock_observe(EldenCameraClocks *,uint64_t,uint64_t,unsigned clock,uint32_t *hz);
/* Read actual retained active leaf/parent under cleanup authority after a
 * different clock/pin fault. Does not restore ready or permit new work. */
int elden_camera_clock_observe_cleanup(EldenCameraClocks *,uint64_t,uint64_t,unsigned clock,uint32_t *hz);
int elden_camera_domain_resource(EldenCameraClocks *,uint64_t,uint64_t,unsigned sensor,unsigned domain,int on);
int elden_camera_pins_prepare(EldenCameraClocks *,uint64_t,uint64_t);
int elden_camera_sensor_reset(EldenCameraClocks *,uint64_t,uint64_t,unsigned sensor,int asserted);
int elden_camera_clocks_status(EldenCameraClocks *,EldenCameraClockStatus *);
int elden_camera_clocks_stop(EldenCameraClocks *);
/* Exact generated register membership used by the native MMIO adapter.
 * No device access or user register entrypoint is exposed by this predicate. */
int elden_camera_clock_register_allowed(unsigned space,uint32_t reg,int writing);
int elden_camera_clock_rate_supported(unsigned clock,uint32_t hz);
#ifdef __cplusplus
}
#endif
#endif
