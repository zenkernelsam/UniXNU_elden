/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_REPORT_H
#define ELDEN_PEN_REPORT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { EldenPenPacketBytes=109, EldenPenCoordinate=1, EldenPenIdentity=0xf0,EldenPenInfo=0xf1,
       EldenPenLeave=0xff };
enum { EldenPenOk=0, EldenPenArgument=-1, EldenPenLength=-2,
       EldenPenUnsupported=-3, EldenPenRange=-4,EldenPenEnvelope=-5 };
typedef struct {
    uint16_t max_x, max_y, max_pressure;
    uint8_t max_tilt;
} EldenPenLimits;
typedef struct {
    uint16_t raw_x, raw_y, x, y, pressure, distance;
    int16_t tilt_x, tilt_y;
    uint8_t kind, tip, in_range, buttons;
    uint8_t battery_raw, tail_raw, identity[6];
    /* F1 metadata; not coordinates, not a contact keepalive, never a request
     * to reconfigure firmware. info_raw[7] is stored but unverified checksum;
     * info_raw[8] is OEM pen type. Do not publish private IDs in normal logs. */
    uint32_t serial;
    uint16_t vendor,product;
    uint8_t info_raw[9],reserved[3];
} EldenPenFrame;
/* packet[0] is the register byte, as in elden_nvt_points(). Immutable output
 * on failure. Retains OEM envelope admission (sum of bytes1..65 is zero), which
 * does NOT checksum pen bytes66 onward. Byte79's integrity remains unqualified.
 * distance is an uncalibrated raw value; battery_raw is not a percent.
 * Identity/unknown packets must not be presented as new contact coordinates. */
int elden_pen_decode(const uint8_t *packet,size_t bytes,const EldenPenLimits *,EldenPenFrame *);
#ifdef __cplusplus
}
#endif
#endif
