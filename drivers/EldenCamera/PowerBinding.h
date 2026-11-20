/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_POWER_BINDING_H
#define ELDEN_CAMERA_POWER_BINDING_H
#include "Power.h"
#include "NativePipeline.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    EldenCameraCapture capture;EldenCameraSensorProvider board;
    EldenCameraPowerSession *power;unsigned initialized,sensor;
} EldenCameraPoweredCapture;
/* Original board supplies real reset/MCLK/admission/delay, and first opens
 * native CCI + receiver. Rail calls go to the shared WL session, not a dummy
 * resource provider or another controller. Caller owns/serializes both cameras
 * and the shared session; stop BOTH captures before power_session_stop. */
int elden_camera_powered_capture_bind(EldenCameraPoweredCapture *,EldenCameraPowerSession *,const EldenCameraSensorProvider *,unsigned);
int elden_camera_powered_capture_probe(EldenCameraPoweredCapture *);
int elden_camera_powered_capture_start(EldenCameraPoweredCapture *);
int elden_camera_powered_capture_stop(EldenCameraPoweredCapture *);
#ifdef __cplusplus
}
#endif
#endif
