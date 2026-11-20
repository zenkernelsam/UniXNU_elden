/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_SERVICE_H
#define ELDEN_CAMERA_SERVICE_H
#include "FrameOutput.h"
#include "ServiceAbi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Publish only a real already-running capture snapshot producer. Original
 * executor pumps output separately; the user client cannot power cameras.
 * Explicit late metaclass registration, no automatic startup/marker file.
 * One privileged consumer per sensor per boot; no snapshot memory reassigned
 * to another process after close. Grants no kernel camera privacy bypass. */
int elden_camera_service_publish(EldenCameraFrameOutput *,unsigned sensor);
int elden_camera_service_withdraw(unsigned sensor);
#ifdef __cplusplus
}
#endif
#endif
