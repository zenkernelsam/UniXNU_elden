/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_FRAME_OUTPUT_H
#define ELDEN_CAMERA_FRAME_OUTPUT_H
#include "DmaBinding.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct EldenCameraFrameOutput EldenCameraFrameOutput;
typedef struct {
    uint64_t ticket,sequence,timestamp_ns,bytes;uint32_t slot,width,height,stride,bayer,rotation,sensor;
} EldenCameraOutputFrame;
typedef struct {uint32_t prepared,stopping,retired,ready,held;int error;uint64_t copied,dropped,last_sequence,last_ns;} EldenCameraOutputStatus;
/* Explicit original camera executor only. Snapshots use real retained cached
 * IOKit descriptors, not a fixed carve-out or mutable raw DMA map. Service
 * never powers/start cameras. Existing ready/started capture is mandatory.
 * All descriptors/output remain to reset even after allocation/copy failure. */
int elden_camera_output_open(EldenCameraDmaCapture *,unsigned sensor,unsigned buffers,EldenCameraFrameOutput **);
/* Same original camera executor invokes this after its real service/IRQ wake.
 * Uses actual pipeline done -> DMA visibility -> frame borrow -> snapshot ->
 * explicit release/requeue. Does not synthesize SOF/pixels or CPU returns. */
int elden_camera_output_pump(EldenCameraFrameOutput *);
int elden_camera_output_acquire(EldenCameraFrameOutput *,EldenCameraOutputFrame *);
int elden_camera_output_release(EldenCameraFrameOutput *,uint64_t ticket);
/* Internal native IOUserClient: returns a retained IOMemoryDescriptor only
 * for a current held ticket. Map read-only. Never expose a writable DMA alias. */
void *elden_camera_output_memory(EldenCameraFrameOutput *,uint64_t ticket,unsigned slot);
/* Real original camera shutdown/context retirement must precede this stop.
 * Held output snapshots stay held until their actual consumer releases them. */
int elden_camera_output_stop(EldenCameraFrameOutput *);
int elden_camera_output_status(EldenCameraFrameOutput *,EldenCameraOutputStatus *);
int elden_camera_output_info(EldenCameraFrameOutput *,uint64_t output[8]);
#ifdef __cplusplus
}
#endif
#endif
