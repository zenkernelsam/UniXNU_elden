/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_ORIENTATION_H
#define ELDEN_MOTION_ORIENTATION_H
#include <stddef.h>
#include <stdint.h>
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenConsDisabled=0,EldenConsReady=1,EldenConsRunning=2,EldenConsStopped=3};
enum {EldenOrientationDisabled=-125,EldenOrientationBusy=-16};
/* The guarded J317 orientation-consumer boundary. This is a source-side
 * contract for a provider mainline can later bind to the real J317 SPU /
 * CoreMotion / UI rotation path. It synthesizes NOTHING: gravity vectors,
 * timestamps, calibration, mount, SUID, orientation nodes and any 0x20/0x8a
 * orientation HID device are all explicit provider outputs that must be
 * qualified at admission and re-fenced on every sample. A missing provider
 * leaves the consumer disabled — the conservative default. */
typedef struct {
    void *context;
    /* Qualified input description for one kind: SUID identity, units/ranges
     * and the real board mount matrix. Both EldenMotionAccel and
     * EldenMotionGyro must be described before any output is armed. */
    int (*describe)(void *context,uint32_t kind,EldenMotionSpec *spec);
    /* Current provider owner/epoch; re-checked on every sample so a stale or
     * foreign provider is fenced, not trusted. */
    int (*owner_epoch)(void *context,uint64_t *owner,uint64_t *epoch);
    /* Monotonic source-tick to mach-time mapping (microseconds). Output must
     * strictly increase for strictly increasing source ticks; it is converted
     * into the classifier's documented 19.2 MHz tick domain to drive the hold,
     * so the hold duration tracks real elapsed time, not the source rate. */
    int (*map_time)(void *context,uint64_t source_ticks,uint64_t *mach_us);
    /* Next calibrated sample, sensor axes: 1 produced, 0 none pending,
     * <0 provider failure. The consumer applies the real mount to device
     * axes, never a donor identity or a fixed SPU address. */
    int (*next)(void *context,EldenMotionSample *sample);
} EldenMotionProvider;
/* Owner-supplied rotation sink: receives only the dominant signed gravity
 * axis (±1/±2) after the existing hold — never a UIKit orientation enum or
 * polarity. Returns0 when accepted; nonzero is a callback failure. */
typedef int (*EldenMotionRotate)(void *context,int32_t axis);
typedef struct {
    EldenMotionProvider provider;EldenMotionRotate rotate;void *rotate_context;
    EldenMotionSpec accel,gyro;EldenMotionRotation rotation;
    uint64_t generation,owner,epoch,last_mach,last_ticks,last_ticks19;
    uint32_t hold_us,state,samples,published,rejected,dropped;
    int32_t published_axis;uint8_t busy,enabled,clock_failed;
} EldenMotionConsumer;
typedef struct {
    uint32_t state,samples,published,rejected,dropped;
    uint64_t generation,owner,epoch,last_mach,last_ticks19;
    int32_t published_axis,pending_candidate;
} EldenMotionConsumerStatus;
/* Bind+qualify the provider and arm one generation. Missing callbacks, a zero
 * owner, an undescribed/bad accel-or-gyro spec (SUID, units, range, mount
 * parity) or a nonmonotonic time-map probe refuse before arming. hold_us is
 * the existing bounded hold (1..1000000). Re-arming a live run refuses. */
int elden_motion_consumer_begin(EldenMotionConsumer *,const EldenMotionProvider *,
    EldenMotionRotate,void *rotate_context,uint32_t hold_us,uint64_t *generation);
/* Pull and consume one provider sample. Returns1 when a matured rotation
 * candidate is published to the sink, 2 when the provider had no sample
 * pending, 0 when a sample was consumed without publication. Fences stale
 * owner/epoch, wrong generation, foreign SUID/unit/kind, zero/duplicate/
 * reversed timestamps, nonmonotonic mach mapping, out-of-range axes and
 * flat/poor-quality input; a flat sample still resets the pending candidate
 * through the existing classifier. An owner/epoch check failure or drift is
 * terminal for the generation: the sample refuses before next()/map/sink, the
 * pending candidate clears, the last published axis stays the accepted UI
 * state and no further sample can publish until a fresh begin re-arms —
 * a provider re-reporting the armed identity cannot resume the session. */
int elden_motion_consumer_sample(EldenMotionConsumer *,uint64_t generation);
/* Stop. A stop landing during an in-flight sink callback aborts that
 * publication (never counted); after stop every produce refuses — no
 * publication after stop. */
int elden_motion_consumer_stop(EldenMotionConsumer *,uint64_t generation);
/* Read-only status for tests/status surfaces; no side effects. */
int elden_motion_consumer_status(const EldenMotionConsumer *,EldenMotionConsumerStatus *);
#ifdef __cplusplus
}
#endif
#endif
