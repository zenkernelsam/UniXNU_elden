/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_NATIVE_HID_H
#define ELDEN_MOTION_NATIVE_HID_H
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    // Producer owns Gen4 firmware/transport/attrs/mount and maps SSC clock to
    // actual mach time. No phantom service with a NULL/failed provider.
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned kind,int cleanup);
    int (*demand)(void *,uint64_t owner,uint64_t epoch,unsigned kind,uint32_t hz);
    EldenMotionSuid suid[2];uint32_t max_abs_q16[2],max_age_us,max_future_us;
    uint8_t negate_axes[2]; // explicitly qualified iOS sign convention
} EldenMotionNativeProducer;
/* Opt-in actual producer call; no automatic worker/transport/ADT insertion.
 * Real OS17 accel/gyro event-driver publication only after provider admission.
 * DeviceOrientation/SPU is separate and is not published by this API. */
int elden_motion_native_hid_start(const EldenMotionNativeProducer *);
/* Original producer worker binds the event-driver hooks before any sample or
 * demand starts;1means both bound,0still matching,negative refused. This
 * avoids needing the first sample to install the hook that starts sampling. */
int elden_motion_native_hid_bind(uint64_t owner,uint64_t epoch);
int elden_motion_native_hid_packet(const EldenMotionSample *);
int elden_motion_native_hid_stop(uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
