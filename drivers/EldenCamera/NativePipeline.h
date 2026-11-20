/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_NATIVE_PIPELINE_H
#define ELDEN_CAMERA_NATIVE_PIPELINE_H
#include "Pipeline.h"
#include "SensorBinding.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    int (*admit)(void *,const EldenCameraPipelineConfig *,unsigned operation);
    int (*buffer)(void *,const EldenCameraPipelineConfig *,const EldenCameraBuffer *,int complete);
    int (*drain)(void *,const EldenCameraPipelineConfig *);
} EldenCameraNativePipelineOwner;
/* Exact map and exclusive masked GIC ownership. The original camera executor
 * calls service to observe hardware CSID records; no interrupt is fabricated.
 * Parent interrupts remain masked, matching bounded native CCI polling model.
 * Actual power/clock/CCI pins/buffer routes and physical DMA drain are external
 * real-owner operations; no defaults, concurrent CAMSS owner or frame source. */
int elden_camera_native_pipeline_open(const EldenCameraPipelineConfig *,const EldenCameraNativePipelineOwner *);
int elden_camera_native_pipeline_queue(uint64_t owner,uint64_t epoch,unsigned sensor,const EldenCameraBuffer *);
int elden_camera_native_pipeline_start(uint64_t owner,uint64_t epoch,unsigned sensor);
int elden_camera_native_pipeline_service(uint64_t owner,uint64_t epoch,unsigned sensor);
int elden_camera_native_pipeline_frame(uint64_t owner,uint64_t epoch,unsigned sensor,EldenCameraFrame *);
int elden_camera_native_pipeline_stop(uint64_t owner,uint64_t epoch,unsigned sensor);
int elden_camera_native_pipeline_status(unsigned sensor,EldenCameraPipelineStatus *);
int elden_camera_native_pipeline_ready(uint64_t owner,uint64_t epoch,unsigned sensor);
int elden_camera_native_pipeline_prepared(uint64_t owner,uint64_t epoch,unsigned sensor);
typedef struct {EldenCameraSensor sensor;EldenCameraSensorProvider board;unsigned target,initialized,bound,receiver_attempted,stopped;int error;} EldenCameraCapture;
/* Original real board first opens CCI and this native receiver with same
 * owner/epoch. Bind and probe remain explicit; queue two real buffers, start
 * native receiver, then existing source-derived sensor. Stop sensor invokes
 * actual receiver drain before its rails are released. No OS17 CMIO yet. */
int elden_camera_capture_bind(EldenCameraCapture *,const EldenCameraSensorProvider *,unsigned sensor);
int elden_camera_capture_probe(EldenCameraCapture *);
int elden_camera_capture_start(EldenCameraCapture *);
int elden_camera_capture_stop(EldenCameraCapture *);
#ifdef __cplusplus
}
#endif
#endif
