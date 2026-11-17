/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_TILT_H
#define ELDEN_PEN_TILT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {int32_t altitude_degrees,azimuth_degrees;} EldenPenTilt;
/* IOHID polar fields use signed16:16 DEGREES, not UIKit's public radians.
 * Input is signed HID tilt in the current portrait coordinate basis, +/-60.
 * At the vertical pole azimuth is indeterminate and conventionally zero.
 * Actual panel mounting and UIKit's conversion still require live validation. */
int elden_pen_tilt(int16_t x,int16_t y,EldenPenTilt *);
#ifdef __cplusplus
}
#endif
#endif
