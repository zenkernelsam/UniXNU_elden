/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_PIPELINE_H
#define ELDEN_CAMERA_PIPELINE_H
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenCameraPhy=0,EldenCameraCsid=1,EldenCameraVfe=2};
enum {EldenPipeAcquire=0,EldenPipeConfigure=1,EldenPipeCapture=2,EldenPipeCleanup=3};
enum {EldenPipeEmpty=0,EldenPipeReady=1,EldenPipeStreaming=2,EldenPipeHeld=3,EldenPipeStopped=4};
typedef struct {
    uint64_t owner,epoch,route_epoch;uint32_t sensor,decoder,rdi,timer_hz;
    /* Exact Gen4 mode comes from the retained sensor core. decoder0..4
     * is chosen by the real original owner, never assumed to be free.
     * CSID/VFE indices must be paired; rear PHY3, front PHY4 per Elden DT. */
    EldenCameraMode mode;
} EldenCameraPipelineConfig;
typedef struct {
    uint64_t id,owner,epoch,route_epoch,iova,bytes;void *mapping;
} EldenCameraBuffer;
typedef struct {EldenCameraBuffer buffer;uint64_t sequence,timestamp_ns;uint32_t bytesused,error;} EldenCameraFrame;
typedef struct {
    void *context;
    /* Actual whole PHY/CSID/VFE ownership, real power/clock/pins and same
     * SMMU lease. Every accessor checks this. No hardware-ready default. */
    int (*admit)(void *,const EldenCameraPipelineConfig *,unsigned operation);
    int (*read)(void *,unsigned block,uint32_t offset,uint32_t *);
    int (*write)(void *,unsigned block,uint32_t offset,uint32_t);
    uint64_t (*now_us)(void *);
    int (*delay_us)(void *,unsigned);
    /* Each buffer must be a real owned, wired contiguous32bit IOVA/CPU
     * mapping.0only after actual mapping/cache/route observation. complete1
     * requires device-written visibility, never fabricated frame pixels. */
    int (*buffer)(void *,const EldenCameraPipelineConfig *,const EldenCameraBuffer *,int complete);
    /*0only after actual source/CSI/VFE/DMA/IRQ/fault context inactivity
     * observed. WM disable/config readback is not that proof. This provider
     * retains mappings until actual platform reset; caller does not free. */
    int (*drain)(void *,const EldenCameraPipelineConfig *);
} EldenCameraPipelineIo;
typedef union {uint64_t alignment;uint8_t bytes[16384];} EldenCameraPipeline;
typedef struct {
    uint32_t state,error,phy_version,csid_version,vfe_version,stride,frame_bytes;
    uint32_t buffers,queued,active,completed,started,stopped,reset_seen;
    uint64_t frames,sequence,last_ns;uint32_t last_top_status,last_buffer_status;
} EldenCameraPipelineStatus;
/* Caller serializes this core's ownership from its one camera executor.
 * Internal reentry gate rejects IRQ/callback concurrency. No memory mapping,
 * clock voting, fake reset, sensor activation or OS camera publication here. */
int elden_camera_pipeline_open(EldenCameraPipeline *,const EldenCameraPipelineConfig *,const EldenCameraPipelineIo *);
int elden_camera_pipeline_queue(EldenCameraPipeline *,const EldenCameraBuffer *);
int elden_camera_pipeline_start(EldenCameraPipeline *);
int elden_camera_pipeline_service(EldenCameraPipeline *); /* actual CSID reset/SOF/done registers */
int elden_camera_pipeline_frame(EldenCameraPipeline *,EldenCameraFrame *);
int elden_camera_pipeline_ready(EldenCameraPipeline *);
int elden_camera_pipeline_stop(EldenCameraPipeline *);
int elden_camera_pipeline_status(EldenCameraPipeline *,EldenCameraPipelineStatus *);
/* Fixed-source physical resources, not permission to map/own them. */
int elden_camera_pipeline_resources(const EldenCameraPipelineConfig *,uint64_t physical[3],uint32_t bytes[3],uint32_t intids[3]);
#ifdef __cplusplus
}
#endif
#endif
