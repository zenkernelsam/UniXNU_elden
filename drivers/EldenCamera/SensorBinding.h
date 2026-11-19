/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_SENSOR_BINDING_H
#define ELDEN_CAMERA_SENSOR_BINDING_H
#include "Core.h"
#include "NativeCci.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned operation);
    int (*resource)(void *,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned resource,int on);
    int (*delay_us)(void *,unsigned);
    int (*receiver_ready)(void *,uint64_t owner,uint64_t epoch,unsigned sensor);
    int (*receiver_drain)(void *,uint64_t owner,uint64_t epoch,unsigned sensor);
} EldenCameraSensorProvider;
typedef struct {EldenCameraCore core;EldenCameraSensorProvider provider;unsigned sensor,initialized,stopped;} EldenCameraSensor;
/* Original camera owner first opens native CCI with actual CAM clocks/pins,
 * then binds this sensor. Sensor tables/ID/control/stop now route through the
 * actual native CCI, no success stub. Resource/receiver providers stay mandatory.
 * Caller serializes operations. No allocation/startup/IOSurface/OS camera here. */
int elden_camera_sensor_bind(EldenCameraSensor *,const EldenCameraSensorProvider *,unsigned sensor);
int elden_camera_sensor_probe(EldenCameraSensor *);
int elden_camera_sensor_start(EldenCameraSensor *);
int elden_camera_sensor_control(EldenCameraSensor *,uint32_t exposure,uint32_t gain);
int elden_camera_sensor_stop(EldenCameraSensor *);
/* Final owner shutdown is distinct from a reversible sensor stream-off:
 * sensor/receiver STOP MUST succeed before native CCI HALT/IRQ drain/restoration.
 * Error retains both control and physical leases for the original reset path. */
int elden_camera_sensor_shutdown(EldenCameraSensor *);
#ifdef __cplusplus
}
#endif
#endif
