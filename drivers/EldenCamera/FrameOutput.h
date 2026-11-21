/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_FRAME_OUTPUT_H
#define ELDEN_CAMERA_FRAME_OUTPUT_H
#include "DmaBinding.h"
#include "Raw10.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct EldenCameraFrameOutput EldenCameraFrameOutput;
typedef struct {
    uint64_t ticket,sequence,timestamp_ns,bytes;uint32_t slot,width,height,stride,bayer,rotation,sensor;
} EldenCameraOutputFrame;
/* Output Bayer metadata uses EldenRaw* values from Raw10.h. EldenCamera*
 * mode enums describe the source sensor and have a different numeric order.
 * Translate explicitly at this boundary; do not forward mode.bayer as ABI. */
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
/* Explicitly bind the current original camera executor to the retained CSID
 * IRQ latch, then call pump_irq from that executor's existing loop. No new
 * thread/startup/provider is installed. Timeout has no frame and returns-11;
 * an actual event services/acks CSID then performs the same real DMA copy and
 * explicit return/requeue. Stop wakes the waiting original executor. */
int elden_camera_output_enable_irq(EldenCameraFrameOutput *);
int elden_camera_output_pump_irq(EldenCameraFrameOutput *,unsigned timeout_ms);
int elden_camera_output_acquire(EldenCameraFrameOutput *,EldenCameraOutputFrame *);
int elden_camera_output_release(EldenCameraFrameOutput *,uint64_t ticket);
/* Internal native IOUserClient: returns a retained IOMemoryDescriptor only
 * for a current held ticket. Map read-only. Never expose a writable DMA alias. */
void *elden_camera_output_memory(EldenCameraFrameOutput *,uint64_t ticket,unsigned slot);
/* Serialized original executor closes new pump/service admission before the
 * actual physical teardown. Held consumer tickets stay returnable; the output
 * still retires only through output_stop after real DMA/pipeline retirement. */
int elden_camera_output_close(EldenCameraFrameOutput *);
/* Real original camera shutdown/context retirement must precede this stop.
 * Held output snapshots stay held until their actual consumer releases them. */
int elden_camera_output_stop(EldenCameraFrameOutput *);
int elden_camera_output_status(EldenCameraFrameOutput *,EldenCameraOutputStatus *);
int elden_camera_output_info(EldenCameraFrameOutput *,uint64_t output[8]);
#ifdef __cplusplus
}
#endif
#endif
