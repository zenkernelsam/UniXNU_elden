/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_CORE_H
#define ELDEN_MOTION_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenMotionAccel=1,EldenMotionGyro=2};
enum {EldenMotionG=1,EldenMotionMetersPerSecond2=2,EldenMotionRadiansPerSecond=3};
enum {EldenMotionArgument=-22,EldenMotionMalformed=-84,EldenMotionPeer=-13,EldenMotionClock=-110,EldenMotionStale=-116};
typedef struct {uint64_t low,high;} EldenMotionSuid;
typedef struct {
    EldenMotionSuid suid;uint32_t kind,unit;int32_t max_abs_q16;
    /* One signed input-axis selector per device axis, each of ±1/±2/±3 exactly
     * once, determinant+1. Caller obtains actual units/range/SUID from firmware
     * attributes and qualifies the board mount, not a donor's SM8250 wiring. */
    int8_t mount[3];
} EldenMotionSpec;
typedef struct {EldenMotionSuid suid;uint64_t timestamp_ticks;int32_t q16[3];uint32_t kind,unit,accuracy;} EldenMotionSample;
/* No native transport, hardware access or HID publication exists in this core.
 * The consumer validates original firmware/session/epoch/route before parsing.
 * Scalar QMI/SSC codecs are source-derived; failed output remains unchanged. */
size_t elden_motion_discovery_request(uint8_t *,size_t,uint16_t transaction,uint32_t kind);
size_t elden_motion_sampling_request(uint8_t *,size_t,uint16_t transaction,EldenMotionSuid,uint32_t hz,int disable);
int elden_motion_client_response(const uint8_t *,size_t,uint16_t transaction,uint64_t *client);
int elden_motion_discovery_report(const uint8_t *,size_t,uint64_t client,uint32_t kind,EldenMotionSuid *);
int elden_motion_samples(const uint8_t *,size_t,uint64_t client,const EldenMotionSpec *,EldenMotionSample *,size_t capacity);
/* Gravity dominant-axis classifier, NOT a UIKit orientation/polarity mapping.
 * It outputs ±1/±2 after a bounded hold, zero on flat/uncertain geometry. Actual
 * screen orientation remains a separately qualified board/IOHID/UI route. */
typedef struct {uint64_t last_ticks,candidate_since;int32_t axis,candidate;uint8_t clock_failed,has_clock;} EldenMotionRotation;
int elden_motion_rotation(EldenMotionRotation *,const EldenMotionSample *,uint32_t hold_us,int32_t *changed_axis);
#ifdef __cplusplus
}
#endif
#endif
