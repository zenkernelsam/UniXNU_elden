/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_ACTUATOR_LIVE_H
#define ELDEN_ACTUATOR_LIVE_H
#include "Abi.h"
#include "AwRtp.h"
#include "SampleMap.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenActuatorIdle=0,EldenActuatorPreparing=1,EldenActuatorPlaying=2,EldenActuatorTerminal=3,EldenActuatorUncertain=4};
typedef struct {
    EldenHapticsCalibration sample_map[2];
    uint32_t window_frames,max_on_frames[2],max_run_us,max_gap_us,max_missing_frames,silence_frames;
    uint32_t measured_min_payload_bps,max_startup_lateness_us;
    uint64_t max_squares[2];uint8_t gain[2],mask;
} EldenActuatorLimits;
typedef struct {uint32_t squares[2];uint8_t on_mask;uint64_t run;} EldenActuatorHistory;
typedef struct {
    void *context;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    /* Actual retained native broker holds exclusive session, shared RAM/RTP
     * budget and ring. Do not supply success placeholders for missing broker.
     * Worst moving-window capacities remain reserved while live output runs. */
    int (*reserve)(void *,uint64_t owner,uint64_t epoch,const uint64_t squares[2],const uint32_t on[2]);
    int (*finish)(void *,uint64_t owner,uint64_t epoch,int stop_confirmed,
        const uint64_t observed_squares[2],const uint32_t observed_on_frames[2]);
    uint64_t (*host)(void *); /* actual19.2MHz clock, same as ring timestamp */
} EldenActuatorLiveIo;
typedef struct {
    EldenActuatorRing *ring;EldenAwChip *chips[2];EldenAwRtp rtp[2];
    EldenActuatorLiveIo io;EldenActuatorLimits limits;
    EldenActuatorHistory *history;size_t history_bytes,history_at;
    uint64_t rolling_squares[2],last_host,next_frame,run_start_host,first_frame;
    uint64_t run_sequence,run_squares[2],run_peak_squares[2];
    uint32_t rolling_on[2],history_count,state,missing,quiet;
    int8_t pending[2][64];uint32_t pending_frames;uint8_t pending_mask,reserved,has_clock;
    int error;
} EldenActuatorLive;
/* Caller owns zero-initialized state, immutable limits/LUTs and wired history
 * memory. No IOKit/native publication occurs in this portable bridge. Ready
 * requires actual driver/profile/admission, not mere existence of plugin.
 * FIR box2 decimation48→24 is an explicit profile contract, not a Taptic model.
 * All calibration, firmware/motor/frequency and timing need actual measurement. */
int elden_actuator_live_init(EldenActuatorLive *,EldenActuatorRing *,EldenAwChip *[2],const EldenActuatorLimits *,
    EldenActuatorHistory *,size_t,const EldenActuatorLiveIo *);
int elden_actuator_live_step(EldenActuatorLive *,int requested);
/* Real stop/standby/restore BEFORE finish. Error pins original state/ring/budget
 * until reset. Success does not erase recent moving-window history. */
int elden_actuator_live_stop(EldenActuatorLive *);
/* Worker-only, stopped hardware. Keeps moving-window history/cost while
 * preventing a prior run's tagged frames from being replayed after StartIO. */
int elden_actuator_live_new_run(EldenActuatorLive *,uint64_t host);
#ifdef __cplusplus
}
#endif
#endif
