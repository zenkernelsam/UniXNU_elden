/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_USER_CLIENT_H
#define ELDEN_CAMERA_USER_CLIENT_H
#include "ServiceAbi.h"
#include "Raw10.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {unsigned sensor;uint32_t connection,seen;EldenRaw10Config conversion;uint64_t last_sequence;} EldenCameraUserClient;
int elden_camera_client_open(EldenCameraUserClient *,unsigned sensor);
/* Maps only a genuine acquired cached snapshot READONLY, converts it, unmaps
 * before exact ticket return. Missing/faulted/no frame yields an error, never
 * a synthetic frame. Caller serializes open/frame/close. */
int elden_camera_client_frame(EldenCameraUserClient *,uint8_t *,size_t,size_t,uint64_t *timestamp_ns);
void elden_camera_client_close(EldenCameraUserClient *);
#ifdef __cplusplus
}
#endif
#endif
