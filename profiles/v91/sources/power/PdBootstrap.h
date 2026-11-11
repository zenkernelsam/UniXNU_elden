/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "PulseControl.h"
#include "ChargePoll.h"
#include "UcsiQuery.h"
#ifdef __cplusplus
extern "C" {
#endif
enum EldenPdBootPhase {EldenPdBootOff,EldenPdBootWait,EldenPdBootPulse,
    EldenPdBootContract,EldenPdBootSuspend,EldenPdBootRestore,EldenPdBootDone,EldenPdBootFailed};
enum EldenPdBootError {EldenPdBootOk,EldenPdBootStopped,EldenPdBootExpired,
    EldenPdBootLinkLost,EldenPdBootUnsafe,EldenPdBootPulseFailed,EldenPdBootIclUnknown,EldenPdBootNoZero,
    EldenPdBootInterrupted};
typedef struct {
    uint64_t now_ms;
    uint32_t epoch,cycle,deadline_seconds,icl_applied,icl_observed;
    unsigned pulse;
    bool link_up,qualified,raw_fresh,high_voltage,icl_owned,icl_pending,icl_quarantined,stop;
} EldenPdBootInput;
typedef struct {bool request_pulse,cancel_pulse,suspend_icl,stop_ready;} EldenPdBootAction;
typedef struct {
    enum EldenPdBootPhase phase;
    enum EldenPdBootError error;
    uint64_t expires,last_ms;
    uint32_t epoch,last_cycle,restore_cycle;
    unsigned mode,stable;
    bool saw_zero,restored;
} EldenPdBootstrap;
bool elden_pd_boot_qualified(const EldenChargeRawSnapshot *,const EldenUcsiSnapshot *,bool fresh,bool ucsi_fresh,bool notified,uint32_t budget);
void elden_pd_boot_init(EldenPdBootstrap *,unsigned mode,uint64_t now_ms);
void elden_pd_boot_step(EldenPdBootstrap *,const EldenPdBootInput *,EldenPdBootAction *);
#ifdef __cplusplus
}
#endif
