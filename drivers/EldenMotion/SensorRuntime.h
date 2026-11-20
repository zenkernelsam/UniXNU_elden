/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_RUNTIME_H
#define ELDEN_SENSOR_RUNTIME_H
#include "AdspEndpoint.h"
#include "QrtrTransport.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenSensorRuntimePrepared=1,EldenSensorRuntimeOpening=2,EldenSensorRuntimeListener=3,
      EldenSensorRuntimeSampling=4,EldenSensorRuntimeStoppingMotion=5,EldenSensorRuntimeClosingRpc=6,
      EldenSensorRuntimeStopped=7,EldenSensorRuntimeRetained=8};
typedef struct {
    EldenSensorMemoryConfig memory;EldenSensorMemoryOwner memory_owner;
    EldenSensorAdspAuthority adsp;uint32_t rpc_intent_size,rpc_intent_count;
    EldenQrtrPortOps qrtr;EldenSensorPackSeal seal;EldenSensorPackAuthority pack;
    EldenMotionBoard board;
} EldenSensorRuntimeConfig;
typedef struct {
    uint32_t stage,prepared,worker_started,endpoint_open,binding_open,listener_ready,motion_started,
             motion_stopped,stop_requested,terminal;
    uint64_t owner,epoch;int runtime_error,cleanup_error;
    EldenSensorRpcStatus rpc;EldenSensorPackStatus pack;EldenMotionNativeStatus motion;
} EldenSensorRuntimeStatus;
/* Explicit original owner only, early BEFORE existing touch route seal. Real
 * config/authority/pack inputs are mandatory. Allocates/installs actual memory
 * once then qualifies receive intents on the same original QRTR owner.
 * No DSP power-on, MMIO controller, new router or default capture is added. */
int elden_sensor_runtime_prepare(const EldenSensorRuntimeConfig *);
/* Explicit later owner call after original ADSP registry is available. One
 * listener executor uses prepared memory; existing SSC worker starts only
 * after actual listener-init2+verified pack status. Board qualifier/time remain
 * real callbacks. There is no bootarg/default/automatic caller in this image. */
int elden_sensor_runtime_start(uint64_t owner,uint64_t epoch);
int elden_sensor_runtime_request_stop(uint64_t owner,uint64_t epoch);
/* Same aggregate <=5.5s observation bound as original motion recovery. Stops
 * sampling before endpoint/remote/translation withdrawal. Failed proof retains
 * every map/context to reset; physical owner callbacks must be bounded. */
int elden_sensor_runtime_stop(uint64_t owner,uint64_t epoch);
int elden_sensor_runtime_status(EldenSensorRuntimeStatus *);
/*1only absent legacy success or actual whole runtime terminal-clean success. */
int elden_sensor_runtime_recovery_stop(void);
#ifdef __cplusplus
}
#endif
#endif
