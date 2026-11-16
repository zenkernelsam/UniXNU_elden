/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_REPORT_H
#define ELDEN_PEN_REPORT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { EldenPenPacketBytes=109, EldenPenCoordinate=1, EldenPenIdentity=0xf0,
       EldenPenLeave=0xff };
enum { EldenPenOk=0, EldenPenArgument=-1, EldenPenLength=-2,
       EldenPenUnsupported=-3, EldenPenRange=-4 };
typedef struct {
    uint16_t max_x, max_y, max_pressure;
    uint8_t max_tilt;
} EldenPenLimits;
typedef struct {
    uint16_t raw_x, raw_y, x, y, pressure, distance;
    int16_t tilt_x, tilt_y;
    uint8_t kind, tip, in_range, buttons;
    uint8_t battery_raw, tail_raw, identity[6];
} EldenPenFrame;
/* packet[0] is the register byte, as in elden_nvt_points(). Immutable output
 * on failure. This checks framing/field ranges, NOT a pen checksum: byte79's
 * integrity semantics remain unqualified and the finger checksum covers1..65.
 * distance is an uncalibrated raw value; battery_raw is not a percent.
 * Identity/unknown packets must not be presented as new contact coordinates. */
int elden_pen_decode(const uint8_t *packet,size_t bytes,const EldenPenLimits *,EldenPenFrame *);
#ifdef __cplusplus
}
#endif
#endif
