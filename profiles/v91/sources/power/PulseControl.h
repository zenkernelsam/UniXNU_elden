/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenPulseState {EldenPulseOff,EldenPulseReady,EldenPulseQueued,EldenPulseHigh,
    EldenPulseRestored,EldenPulseRefused,EldenPulseUncertain};
/* 0 idle,1 queued/running,2 terminal. Stop can close an unstarted worker. */
static inline int elden_thermal_worker_claim(volatile unsigned *state){
    unsigned expected=0;return __atomic_compare_exchange_n(state,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline void elden_thermal_worker_close_idle(volatile unsigned *state){
    unsigned expected=0;__atomic_compare_exchange_n(state,&expected,2u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
unsigned elden_thermal_pulse_state(void);
int elden_thermal_pulse_request(void);
void elden_thermal_pulse_cancel(void);
/* Fresh, completed cold policy cycle. Observation only; no output change. */
int elden_thermal_cold_now(uint32_t battery_epoch);
#ifdef __cplusplus
}
#endif
