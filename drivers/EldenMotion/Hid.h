/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_HID_H
#define ELDEN_MOTION_HID_H
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint16_t page,usage;const char *product;} EldenMotionHidKind;
const EldenMotionHidKind *elden_motion_hid_kind(unsigned kind);
const uint8_t *elden_motion_hid_descriptor(unsigned kind,uint32_t *length);
typedef struct {uint32_t type,sequence;int32_t q16[3];uint64_t timestamp_ticks;} EldenMotionHidEvent;
/* This boundary requires parsed physical g/rad/s and a qualified mount/timebase.
 * Caller may select signs only as a reviewed iOS event convention; sign flags
 * are not a substitute for the source's physical board mounting matrix. */
int elden_motion_hid_event(const EldenMotionSample *,unsigned negate_axes,uint32_t sequence,EldenMotionHidEvent *);
/* IOHID ReportInterval in us, bounded real producer rate. 0 stops; out of
 * range/narrowing refuse, never silently return successful hardware demand. */
int elden_motion_hid_interval(uint64_t interval_us,uint32_t *hz);
#ifdef __cplusplus
}
#endif
#endif
