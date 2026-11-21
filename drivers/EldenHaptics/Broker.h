/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_BROKER_H
#define ELDEN_HAPTICS_BROKER_H
#include "Catalog.h"
#include "Mailbox.h"
#include "Budget.h"
#include "WaveFeed.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    int (*admit)(void *,int cleanup); /*actual native recovery/temperature/session scope*/
    void (*lock)(void *,int acquire); /*real native IOLock; never held over hardware*/
    uint64_t (*now_us)(void *);
    void *(*allocate)(void *,size_t);
    void (*release)(void *,void *,size_t);
} EldenHapticsBrokerIo;
typedef struct {
    EldenHapticsBrokerIo io;EldenAwChip *chips[2];
    EldenHapticsMailbox mail;EldenHapticsQueue ram;EldenSharedBudget budget;EldenRtpStream rtp;
    const EldenHapticsEffect *effects;size_t effect_count;EldenRtpLimits rtp_limits;
    EldenHapticsRequest current;uint64_t next_owner,live_owner,epoch,clock_us;
    int8_t *pcm[2];size_t pcm_bytes;uint32_t effect_id,current_kind;
    /* Source-bound compiled-recipe path: the worker's SAME b->rtp stream plays
     * bounded WaveFeed chunks through one live session. Unbound by default —
     * a recipe request refuses until a measured manifest+profile is bound. */
    EldenWaveFeed wave;
    const EldenWaveSource *recipes;size_t recipe_count;
    const EldenWaveProfile *wave_profile;EldenWaveCell *wave_history;uint32_t wave_history_cells;
    uint32_t wave_revision;
    uint16_t current_intensity;uint8_t initialized,has_clock,ram_ready,rtp_ready,held,stopping,wave_ready;
    int error;
} EldenHapticsBroker;
/* Same boot-lifetime broker holds RAM and RTP budgets across client reconnects.
 * Default absent limits/catalog initialize with NO output; zero-gain inspection
 * remains native's separate controller setup. Host synthetic limits never enter
 * the production generator. Only worker calls step/stop; API methods lock mail. */
int elden_broker_init(EldenHapticsBroker *,const EldenHapticsBrokerIo *,EldenAwChip *[2],
    const EldenHapticsQueueLimits *,const EldenSharedBudgetLimits *,const EldenRtpLimits *,const EldenHapticsEffect *,size_t count);
int elden_broker_open(EldenHapticsBroker *,uint64_t *owner,uint64_t *epoch);
int elden_broker_close(EldenHapticsBroker *,uint64_t owner,uint64_t epoch);
int elden_broker_submit_ram(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay_us,
    uint32_t duration_us,uint8_t mask,uint8_t wave,uint16_t intensity_q15);
int elden_broker_submit_effect(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,uint32_t effect_id,uint64_t delay_us,uint16_t intensity_q15);
/* Binds the build-time source-bound recipe manifest plus the runtime measured
 * dual-motor profile and the caller's rolling-budget history ring. Refuses a
 * second bind or a manifest/profile that cannot admit; leaves the compiled
 * recipe path disabled when no measured profile is supplied. */
int elden_broker_bind_recipes(EldenHapticsBroker *,const EldenWaveSource *,size_t count,
    const EldenWaveProfile *,uint32_t accepted_revision,EldenWaveCell *history,uint32_t history_cells);
/* Queues a source-bound compiled recipe by id through the same serialized
 * mailbox and RTP worker as a catalog effect. Never accepts userspace PCM,
 * gain, boost, calibration, register or voltage values. */
int elden_broker_submit_recipe(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,uint32_t recipe_id,uint64_t delay_us,uint16_t intensity_q15);
int elden_broker_cancel(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id);
int elden_broker_result(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *);
int elden_broker_step(EldenHapticsBroker *);
int elden_broker_stop(EldenHapticsBroker *);
int elden_broker_work(EldenHapticsBroker *);
/* Same native worker's AW playback admission; never asks userspace for gains. */
int elden_broker_output_admit(EldenHapticsBroker *);
/* Native shared-memory Actuator owns the same session. Claim is metadata only;
 * only the retained worker reserves/finishes physical runs and releases after
 * its actual STOP/standby proof. No mailbox request can bypass this ownership. */
int elden_broker_actuator_claim(EldenHapticsBroker *,uint64_t owner,uint64_t epoch);
int elden_broker_actuator_reserve(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t run_id,
    uint32_t lease_us,const uint64_t squares[2],const uint32_t on_frames[2]);
int elden_broker_actuator_finish(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t run_id,int confirmed);
int elden_broker_actuator_settle(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t run_id,int confirmed,
    const uint64_t observed_squares[2],const uint32_t observed_on_frames[2]);
int elden_broker_actuator_release(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,int confirmed);
#ifdef __cplusplus
}
#endif
#endif
