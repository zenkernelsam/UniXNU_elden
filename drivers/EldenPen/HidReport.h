/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_HID_REPORT_H
#define ELDEN_PEN_HID_REPORT_H
#include "PenReport.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { EldenPenHidBytes=18, EldenPenPressureScale=1000, EldenPenDescriptorCapacity=256 };
/* OS17 donor keyboard-first matching + separate Stylus application0x0d/0x20.
 * Reports1/2 exist for matching but are never sent. Pen report ID7 only.
 * Secondary button is Barrel Switch2, never falsely labeled eraser/invert. */
int elden_pen_hid_descriptor(uint16_t pressure_max,uint8_t *,size_t,size_t *);
int elden_pen_hid_report(const EldenPenFrame *,uint16_t pressure_max,uint8_t [EldenPenHidBytes]);
/* IOHID uncalibrated symmetric logical pressure -> UIKit's declared scale1000.
 * Unknown/out-of-range values fail without changing output. */
int elden_pen_pressure_fixed(int32_t normalized,int32_t *scaled);
#ifdef __cplusplus
}
#endif
#endif
