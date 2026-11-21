/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_WAVE_FEED_H
#define ELDEN_HAPTICS_WAVE_FEED_H
#include "RtpStream.h"
#include "WaveCompiler.h"
#ifdef __cplusplus
extern "C" {
#endif
/* effect_id namespace tag marking a source-bound compiled recipe, distinct
 * from a resident native-catalog effect id (<2^31). Used by the broker's
 * submit_recipe path; catalog effect ids may never set this bit. */
#define EldenWaveRecipeTag 0x80000000u
/* One build-time source-bound recipe entry: a caller-qualified parsed recipe
 * (ordered events + automation over qualified carriers). Identity/shas are the
 * caller manifest's contract, not re-derived here; never userspace PCM. */
typedef struct {uint32_t id;const EldenWaveRecipe *recipe;} EldenWaveSource;
/* Bounded chunk feed: owns the armed WaveCompiler plus ONE resident compiled
 * chunk (2x480 signed samples) and serves the existing RtpStream pull source.
 * One chunk is held at a time — a motor that out-consumes its sibling waits, so
 * at most ~64 staged in-flight frames separate the two FIFOs and the moving
 * dual-motor budget is preflighted per produced chunk before any is played.
 * pulled[] counts frames staged into the worker; confirmed[] counts frames the
 * worker acked as written to the FIFO. A chunk stays resident until BOTH are
 * at chunk.frames for both motors — only confirmed writes feed the compiler. */
typedef struct {
    EldenWaveCompiler compiler;
    EldenWaveChunk chunk;
    EldenWaveCell *history;uint32_t history_cells;
    uint64_t generation;
    uint32_t pulled[2];          /* frames staged to each motor in current chunk */
    uint32_t confirmed[2];       /* frames FIFO-write-acked for each motor */
    int error;uint8_t enabled,have_chunk,exhausted;
} EldenWaveFeed;
const EldenWaveSource *elden_wave_source_find(const EldenWaveSource *,size_t count,uint32_t id);
/* Arms the compiler with a source recipe + measured dual-motor profile and
 * primes the first bounded chunk. The production/default profile stays disabled:
 * absent/disabled/stale/out-of-range/single-motor coverage all refuse. */
int elden_wave_feed_begin(EldenWaveFeed *,const EldenWaveRecipe *,const EldenWaveProfile *,
    uint32_t accepted_revision,EldenWaveCell *history,uint32_t history_cells,uint64_t *generation);
/* Builds the pull-source clip for the armed run: frames=recipe.total_frames,
 * rate24k, both motors, measured per-motor gain, the verified worst
 * moving-window bound for the single up-front reservation. pcm[] stays NULL. */
void elden_wave_feed_clip(EldenWaveFeed *,EldenRtpClip *,uint64_t clip_id);
/* RtpStream pull callback: stages the next bounded window for one motor into
 * out[] at the exact expected source position (advances pulled[] only, never
 * the confirmed cursor), EldenAwBusy while the chunk is fully staged or the
 * sibling is owed, or <0 on producer failure. Staged is NOT consumed. */
int elden_wave_feed_pull(void *,unsigned motor,uint32_t first_frame,int8_t *out,size_t want);
/* RtpStream consume ack: invoked strictly after elden_aw_rtp_feed confirmed
 * 'count' staged frames at first_frame reached this motor's FIFO. Advances the
 * confirmed cursor; the resident chunk is fed+retired only when BOTH motors
 * confirm every frame. Staged-but-unwritten bytes never arrive here. */
int elden_wave_feed_consumed(void *,unsigned motor,uint32_t first_frame,size_t count);
/* Drops the produced-but-unfed remainder so a stopped/cancelled run can never
 * report false-complete; the existing worker owns actual STOP/settle. */
int elden_wave_feed_stop(EldenWaveFeed *);
#ifdef __cplusplus
}
#endif
#endif
