/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_CAPTURE_SESSION_H
#define ELDEN_CAMERA_CAPTURE_SESSION_H
#include "Service.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    EldenCameraDmaCapture *capture;EldenCameraFrameOutput *output[2];
    uint64_t owner,epoch,route_epoch;int error;
    uint32_t initialized,stopping,shutdown_done;
    uint32_t prepared[2],probed[2],queued[2],started[2],opened[2],listening[2],published[2],closed[2],stopped[2],retired[2];
} EldenCameraCaptureSession;
/* One original sleepable camera executor serializes this session across both
 * sensors; all real physical providers ride inside the bound
 * EldenCameraDmaCapture. This is the explicit outer lifecycle composition the
 * mainline original executor performs; it is not linked into the audited
 * native objects, installs no new thread or register route, and adds no
 * automatic boot/start. Every step calls the existing board/DMA/output/
 * service owner exactly once, in the required order. */
int elden_camera_capture_session_bind(EldenCameraCaptureSession *,EldenCameraDmaCapture *);
/* Ordered admission for one target: board prepare -> sensor identity probe ->
 * real initial DMA queue -> stream start -> retained snapshot open -> original
 * CSID listen. A step failure latches session error and leaves teardown to
 * stop/shutdown through the board's existing partial-state cleanup. output[s]
 * may stay a retained non-null partial object after a failed open (e.g. IOLock
 * allocation): explicit, admission-impossible and non-blocking to teardown. */
int elden_camera_capture_session_begin(EldenCameraCaptureSession *,unsigned sensor,const EldenCameraPipelineConfig *,const EldenCameraBoardClockPlan *,unsigned buffers);
/* Publish the running snapshot producer through the real OS17 service.
 * Failure leaves the physical capture running and is not latched; a slot
 * committed before the failure is still retired by owner shutdown. */
int elden_camera_capture_session_publish(EldenCameraCaptureSession *,unsigned sensor);
/* The original executor's service entry: bounded real CSID wait then the same
 * actual service/frame/snapshot chain. -11 on timeout or no frame. */
int elden_camera_capture_session_serve(EldenCameraCaptureSession *,unsigned sensor,unsigned timeout_ms);
/* Ordered close for one target: new output/service admission closes first,
 * then the actual physical drain/stop. Consumer-held tickets stay returnable.
 * A stop before begin or after a failed prepare still reaches the board stop
 * and leaves the sensor honestly terminal. */
int elden_camera_capture_session_stop(EldenCameraCaptureSession *,unsigned sensor);
/* Close every opened output before any physical stop; a refused close on a
 * fully opened output gates teardown, while a lockless partial that never
 * admitted a consumer never blocks it. Both physical stops are attempted
 * even when the first drain fails, then the real board shutdown and whole-
 * arena retirement run and each output retires — published slots via the
 * identity-checked withdraw that also covers a partially published service
 * (slot committed, start failed) and never touches a foreign registration.
 * Reports the first real error and retains state. */
int elden_camera_capture_session_shutdown(EldenCameraCaptureSession *);
#ifdef __cplusplus
}
#endif
#endif
