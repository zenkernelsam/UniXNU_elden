/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenGpuRuntimeState {EldenGpuIdle,EldenGpuStarting,EldenGpuReady,EldenGpuClosing,EldenGpuDrained,EldenGpuHeld};
typedef struct {uint32_t state,stage,status,retained;uint64_t requested_hz;} EldenGpuRuntimeStatus;
/* Worker context, one attempt. The frequency must be an exact admitted OPP.
 * Caller must have armed the existing reboot route/watchdog before invocation. */
int elden_gpu_start(uint64_t requested_hz);
/* Closes new userspace operations, drains known fences within budget. Does
 * not claim clocks off/reset: every mapping/vote stays retained until reboot. */
int elden_gpu_quiesce_for_reboot(uint32_t timeout_us);
void elden_gpu_runtime_status(EldenGpuRuntimeStatus *);
#ifdef __cplusplus
}
#endif
