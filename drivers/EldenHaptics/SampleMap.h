/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_SAMPLE_MAP_H
#define ELDEN_HAPTICS_SAMPLE_MAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint16_t magnitude_q15,drive_q15;} EldenHapticsGainPoint;
typedef struct {
    const EldenHapticsGainPoint *points;uint16_t count,drive_peak_q15;
    uint32_t rate_hz,max_frames;
    uint64_t square_budget_q30;
} EldenHapticsCalibration;
/* Calibrated LUT maps normalized Apple/current Actuator PCM magnitude to
 * motor drive magnitude. Caller/native manifest establishes actual calibration
 * identity and gain/boost/rate. Zero peak/rate/budget forbids any output.
 * Signed samples stay signed; amplitude can only be attenuated by intensity.
 * Integral squared drive is an accounting proxy, NOT a thermal/coil model.
 * Output remains unchanged on invalid samples/calibration/budget overrun. */
int elden_haptics_samples(const int16_t *,size_t,uint16_t intensity_q15,
    const EldenHapticsCalibration *,int8_t *,uint64_t *square_cost);
#ifdef __cplusplus
}
#endif
#endif
