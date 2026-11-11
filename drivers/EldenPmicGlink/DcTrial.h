/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "PowerPathRead.h"
#ifdef __cplusplus
extern "C" {
#endif
enum EldenDcTrialPhase {EldenDcTrialOff,EldenDcTrialPre,EldenDcTrialOpen,EldenDcTrialLower,
    EldenDcTrialCheckLow,EldenDcTrialHold,EldenDcTrialRestore,EldenDcTrialFinal,
    EldenDcTrialDone,EldenDcTrialFailed};
enum {EldenDcTrialUnsafe=1,EldenDcTrialProtocol,EldenDcTrialIoError,EldenDcTrialTimeout,
    EldenDcTrialOverridden,EldenDcTrialStopped,EldenDcTrialEpoch};
/* check(expected):0 exact,1 original10 while expecting9,negative error.
 * write(value):0 hardware completion,1 fence expired before command,negative error. */
typedef struct {void *context;int (*open)(void *);int (*check)(void *,uint8_t);int (*write)(void *,uint8_t);} EldenDcTrialIo;
typedef struct {
    EldenPowerPathRead snapshot;
    enum EldenDcTrialPhase phase;
    uint32_t epoch,error,write_attempts,write_completions;
    uint64_t expires,hold_until,sample_at;
    bool up,started,lower_seen,restored,held_full,overridden;
} EldenDcTrial;
void elden_dc_trial_init(EldenDcTrial *);
void elden_dc_trial_link(EldenDcTrial *,bool,uint32_t);
int elden_dc_trial_begin(EldenDcTrial *,uint64_t);
int elden_dc_trial_next_read(EldenDcTrial *,uint8_t[28]);
void elden_dc_trial_unsent(EldenDcTrial *);
int elden_dc_trial_receive(EldenDcTrial *,const uint8_t *,size_t);
void elden_dc_trial_fail(EldenDcTrial *,unsigned);
void elden_dc_trial_step(EldenDcTrial *,uint64_t,bool,bool,bool,const EldenDcTrialIo *);
EldenDcTrialIo elden_dc_native_io(void);
void elden_dc_native_close(void);
void elden_dc_native_fence_writes(void);
#ifdef __cplusplus
}
#endif
