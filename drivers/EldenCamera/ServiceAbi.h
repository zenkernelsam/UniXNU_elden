/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_SERVICE_ABI_H
#define ELDEN_CAMERA_SERVICE_ABI_H
#include <stdint.h>
enum {EldenCameraServiceVersion=1,EldenCameraServiceInfo=0,EldenCameraServiceAcquire=1,EldenCameraServiceRelease=2};
enum {EldenCameraServiceInfoWords=8,EldenCameraServiceFrameWords=12,EldenCameraServiceMaxBuffers=4};
/* Info: version,sensor,width,height,stride,RAWbits,Bayer,rotation.
 * Acquire: ticket,slot,sequence,timestamp_ns,bytes,width,height,stride,Bayer,
 * rotation,sensor,version. No PID/user pointer/raw register/gain is transmitted.
 * Release takes exact ticket. Memorytype0..3 maps current acquired snapshot
 * READONLY; heldticket release acknowledges consumer completion. Snapshots
 * belong to one privileged process perboot, never reassigned to another.
 * Actual camera capture/device-written visibility is upstream; returned
 * frame metadata alone is not physical camera/calibration/ISP acceptance. */
#endif
