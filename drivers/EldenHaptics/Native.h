/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_NATIVE_H
#define ELDEN_HAPTICS_NATIVE_H
#include "Queue.h"
#ifdef __cplusplus
extern "C" {
#endif
void elden_haptics_native_start(void);
int elden_haptics_stop_for_reboot(void);
/* Kernel-assigned owner/epoch, no userspace gain/voltage/raw-I2C interface. */
int elden_haptics_native_open(uint64_t *owner,uint64_t *epoch);
int elden_haptics_native_close(uint64_t owner,uint64_t epoch);
int elden_haptics_native_submit(uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay_us,
    uint32_t duration_us,uint8_t motor_mask,uint8_t wave,uint16_t intensity_q15);
int elden_haptics_native_cancel(uint64_t owner,uint64_t epoch,uint64_t id);
int elden_haptics_native_submit_effect(uint64_t owner,uint64_t epoch,uint64_t id,uint32_t effect_id,uint64_t delay_us,uint16_t intensity_q15);
int elden_haptics_native_effect_info(uint32_t effect_id,uint64_t output[4]);
int elden_haptics_native_result(uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *);
void elden_haptics_native_info(uint64_t output[8]);
/* System audio client path: exact platform-binary/name authorization is
 * checked by the actual service before open. Every open receives a new ring. */
int elden_haptics_actuator_allowed(void *task);
int elden_haptics_actuator_open(uint64_t *owner,uint64_t *epoch);
int elden_haptics_actuator_close(uint64_t owner,uint64_t epoch);
int elden_haptics_actuator_request(uint64_t owner,uint64_t epoch,int start);
int elden_haptics_actuator_info(uint64_t owner,uint64_t epoch,uint64_t output[8]);
#ifdef __cplusplus
}
class IOMemoryDescriptor;
extern "C" IOMemoryDescriptor *elden_haptics_actuator_memory(uint64_t owner,uint64_t epoch);
#endif
#endif
