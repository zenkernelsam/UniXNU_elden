/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_WAVE_COMPILER_H
#define ELDEN_HAPTICS_WAVE_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#include "SampleMap.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenWaveDisabled=0,EldenWaveReady=1,EldenWaveRunning=2,EldenWaveComplete=3,
      EldenWaveStopped=4,EldenWaveFailed=5};
enum {EldenWaveTransient=1,EldenWaveContinuous=2};
#define ELDEN_WAVE_RATE_HZ 24000u
#define ELDEN_WAVE_MAX_US 60000000u
#define ELDEN_WAVE_MAX_FRAMES (ELDEN_WAVE_RATE_HZ*60u)
#define ELDEN_WAVE_CHUNK 480u /* 20ms at24k; never exceeds SampleMap.max_frames */
/* A qualified source carrier: signed24k samples plus an explicit loop/tail
 * layout. Signed samples stay signed; no invented carrier, waveform type or
 * Taptic equivalence is admitted. Same form as the offline template bank. */
typedef struct {
    const int16_t *samples;uint32_t count;          /* <= ELDEN_WAVE_RATE_HZ */
    uint32_t loop_frames,tail_frames;              /* 0|explicit; loop+tail==count when loop */
} EldenWaveCarrier;
/* One parsed AHAP event (transient or continuous). intensity/sharpness are
 * perceptual conversion inputs, NEVER a measured voltage or gain. carrier is
 * the qualified sharpness0 body; morph is the sharpness1 morph pair or NULL for
 * a single bound carrier. Both must share one count/loop/tail to morph. */
typedef struct {
    const EldenWaveCarrier *carrier,*morph;
    uint32_t start_frame,frames;                   /* source24k position/length */
    uint16_t intensity_q15,sharpness_q15;          /* conversion inputs only */
} EldenWaveEvent;
/* A dynamic parameter item: a step write (EldenWaveStep) or a bounded linear
 * control curve (EldenWaveCurve). Mirrors source Parameter/ParameterCurve. */
enum {EldenWaveStep=1,EldenWaveCurve=2};
typedef struct {
    uint32_t frame;uint8_t kind;
    uint16_t value_q15;                            /* step value (kind==Step) */
    const struct EldenWaveControlPoint *points;uint32_t count; /* curve (kind==Curve) */
} EldenWaveAutomation;
typedef struct EldenWaveControlPoint {uint32_t frame;uint16_t value_q15;} EldenWaveControlPoint;
typedef struct {const EldenWaveAutomation *items;uint32_t count;} EldenWaveAutomationList;
/* A parsed, source-bound recipe. Bounded to60s; source shas/identity are owned
 * by the caller's manifest, not re-derived here. */
typedef struct {
    const EldenWaveEvent *events;uint32_t event_count;
    const EldenWaveAutomationList *intensity,*sharpness;   /* may be NULL */
    uint32_t total_frames;                         /* <= ELDEN_WAVE_MAX_FRAMES */
} EldenWaveRecipe;
/* One produced RTP chunk. first_frame/timestamp_us pin the exact source slot;
 * generation binds the producing run; mask marks covered motors. pcm holds the
 * converted signed drive samples for each masked motor. */
typedef struct {
    uint64_t generation;
    uint32_t first_frame,frames;
    uint64_t timestamp_us;
    uint8_t mask;
    int8_t pcm[2][ELDEN_WAVE_CHUNK];
} EldenWaveChunk;
/* Moving-window accounting cell: one source frame across both motors. */
typedef struct {uint64_t squares[2];uint8_t on_mask;} EldenWaveCell;
/* A measured dual-motor calibration profile. REQUIRED for any output: absent,
 * stale (revision behind the caller's accepted revision), out-of-range or
 * single-motor coverage all refuse. enabled must be set explicitly; the
 * conservative default is disabled. Budget fields are moving-window worst
 * gain^2*sample^2 and motor-on/dual-on bounds, an accounting proxy only. */
typedef struct {
    const EldenHapticsCalibration *motor[2];
    uint8_t  gain[2];
    uint64_t max_window_squares[2];
    uint32_t max_window_on_frames[2],max_dual_on_frames,window_frames;
    uint32_t measured_min_payload_bps;
    uint32_t revision,enabled;
} EldenWaveProfile;
typedef struct {
    EldenWaveRecipe recipe;EldenWaveProfile calibration;
    EldenWaveCell *history;uint32_t history_count; /* caller ring, window_frames cells */
    uint64_t generation;
    uint32_t produced,drained,pending,history_at,history_used;
    uint64_t rolling_squares[2];uint32_t rolling_on[2],rolling_dual;
    uint32_t state,dropped;int error;uint8_t enabled;
} EldenWaveCompiler;
/* Validate recipe+profile and arm one generation. A valid measured profile is
 * mandatory: absent/disabled/stale/out-of-range/single-motor coverage refuse,
 * leaving output disabled. accepted_revision is the caller's qualified floor;
 * a profile older than it is stale. history is a caller ring of at least
 * profile.window_frames cells for the rolling dual-motor budget. Refuses to
 * re-arm a live run. Returns0 and stamps *generation. */
int elden_wave_compiler_begin(EldenWaveCompiler *,const EldenWaveRecipe *,
    const EldenWaveProfile *,uint32_t accepted_revision,
    EldenWaveCell *history,uint32_t history_cells,uint64_t *generation);
/* Produce the next bounded RTP chunk for the armed run. generation must match
 * the armed run (a stale generation refuses). expected_first_frame must equal
 * the next emitted source position: a skipped/gapped or already-served position
 * is a timing/underrun refusal. Enforces the dual-motor moving budget and the
 * exact source timestamp/rate; the chunk is left unchanged on any refusal.
 * Returns1 once the whole recipe drained,0 while producing. */
int elden_wave_compiler_next(EldenWaveCompiler *,uint64_t generation,
    uint32_t expected_first_frame,EldenWaveChunk *out);
/* Consumer acknowledges 'frames' of the pending chunk were actually fed to the
 * original FIFO owner. Produces no output itself. */
int elden_wave_compiler_fed(EldenWaveCompiler *,uint64_t generation,uint32_t frames);
/* Stop during a pending feed drops the un-fed remainder: the run becomes
 * Stopped, never Complete, and every later produce refuses. dropped records the
 * samples that were produced but not confirmed fed. */
int elden_wave_compiler_stop(EldenWaveCompiler *,uint64_t generation);
#ifdef __cplusplus
}
#endif
#endif
