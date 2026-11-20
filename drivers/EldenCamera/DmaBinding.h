/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_DMA_BINDING_H
#define ELDEN_CAMERA_DMA_BINDING_H
#include "NativeDma.h"
#include "Board.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    EldenCameraNativeDma *dma;EldenCameraBoard board;EldenCameraBoardOwner physical;
    uint64_t owner,epoch,route_epoch;uint32_t initialized;
} EldenCameraDmaCapture;
/* Connects native camera board to the actual prepared DMA arena. Physical
 * receiver resources/clock/RPMh/IRQ/DMA-drain callbacks remain mandatory;
 * buffer validation/completion uses the real allocator+immutable route.
 * Original owner serializes this session's APIs, keeps early SMMU mapping
 * fixed to reset, and retains frame mappings across stop/error. The physical
 * receiver_resources provider supplies rail/NoC/controller dependencies; the
 * shared SMMU reservation belongs to dma's owner and stays retained through
 * all Board releases until dma_stop's real retirement. No duplicate release.
 * No auto-start. */
int elden_camera_dma_capture_bind(EldenCameraDmaCapture *,EldenCameraNativeDma *,EldenCameraNativeClocks *,EldenCameraPowerSession *,const EldenCameraBoardOwner *);
int elden_camera_dma_capture_prepare(EldenCameraDmaCapture *,const EldenCameraPipelineConfig *,const EldenCameraBoardClockPlan *);
int elden_camera_dma_capture_probe(EldenCameraDmaCapture *,unsigned sensor);
int elden_camera_dma_capture_queue_initial(EldenCameraDmaCapture *,unsigned sensor);
int elden_camera_dma_capture_start(EldenCameraDmaCapture *,unsigned sensor);
int elden_camera_dma_capture_service(EldenCameraDmaCapture *,unsigned sensor);
int elden_camera_dma_capture_frame(EldenCameraDmaCapture *,unsigned sensor,EldenCameraFrame *);
int elden_camera_dma_capture_release(EldenCameraDmaCapture *,unsigned sensor,const EldenCameraFrame *,int requeue);
int elden_camera_dma_capture_stop(EldenCameraDmaCapture *,unsigned sensor);
int elden_camera_dma_capture_shutdown(EldenCameraDmaCapture *);
#ifdef __cplusplus
}
#endif
#endif
