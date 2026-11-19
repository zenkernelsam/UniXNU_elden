/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_WAKE_FLOW_H
#define ELDEN_WAKE_FLOW_H
#include <stddef.h>
#include <stdint.h>
#include "Gesture.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenWakeFlowNormal=0,EldenWakeFlowPausing=1,EldenWakeFlowGesture=2,EldenWakeFlowDeep=3,
    EldenWakeFlowRequested=4,EldenWakeFlowRestoring=5,EldenWakeFlowStopped=6,EldenWakeFlowHeld=7};
enum {EldenWakeAdmissionNormal=0,EldenWakeAdmissionPaused=1,EldenWakeAdmissionResume=2,EldenWakeAdmissionCleanup=3};
typedef struct {
    void *context;
    /* Actual same-worker/DMA/root/power checks. Resume requires actual panel
     * rail/transport readiness, not merely an accepted wake-request callback. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned phase);
    int (*pause)(void *,uint64_t owner,uint64_t epoch);
    int (*release_input)(void *); // real finger AND pen HID result, no terminal destroy
    int (*irq_acquire)(void *,uint64_t owner,uint64_t epoch);
    int (*irq_arm)(void *,uint64_t owner,uint64_t epoch);
    int (*irq_disarm)(void *,uint64_t owner,uint64_t epoch);
    int (*irq_stop)(void *,uint64_t owner,uint64_t epoch);
    int (*irq_take)(void *,uint64_t owner,uint64_t epoch,uint32_t *);
    int (*command)(void *,int gesture); // exact original50:13/50:11 via real SPI
    int (*packet)(void *,uint8_t out[109]); // owning SPI worker, never IRQ
    int (*request_wake)(void *,uint64_t owner,uint64_t epoch,unsigned source);
    int (*restore)(void *); // actual verified original firmware/transport/PID/HID
    int (*resume_worker)(void *,uint64_t owner,uint64_t epoch);
} EldenWakeFlowIo;
typedef struct {
    EldenWakeFlowIo io;uint64_t owner,epoch;
    uint32_t state,initialized,irq_owned,paused,command_delivered,wake_requested,packets,gestures;int error;
} EldenWakeFlow;
/* All methods run SERIALIZED on the ORIGINAL touch worker. Policy/PM threads
 * must post a bounded command and await its acknowledgment; this library does
 * not run concurrent SPI or pretend a missing provider succeeded. */
int elden_wake_flow_init(EldenWakeFlow *,const EldenWakeFlowIo *,uint64_t owner,uint64_t epoch);
int elden_wake_flow_suspend(EldenWakeFlow *,int double_tap_enabled);
/* Invoked only after the native IRQ wakes the worker. Takes a verified gesture
 * packet, filters15 and submits a real power request. Return0 is accepted request,
 * not screen/system awake. Missing/unrecognized packets never become power keys. */
int elden_wake_flow_irq(EldenWakeFlow *);
/* Actual display/system power provider calls this AFTER power becomes ready.
 * Restore/release checks must finish before normal finger/pen sampling resumes. */
int elden_wake_flow_resume(EldenWakeFlow *);
int elden_wake_flow_stop(EldenWakeFlow *);
int elden_wake_flow_normal_input(const EldenWakeFlow *);
#ifdef __cplusplus
}
#endif
#endif
