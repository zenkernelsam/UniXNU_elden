/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_NATIVE_CCI_H
#define ELDEN_CAMERA_NATIVE_CCI_H
#include "Cci.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Real SAME boot/board owner validates active camera GDSC/clock branches,
     * pin/whole-controller exclusivity and actual37.5MHz CCI clock. The native
     * adapter does not turn a donor table/default into a hardware-ready vote. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned controller,unsigned operation);
    uint32_t (*clock_hz)(void *,uint64_t owner,uint64_t epoch,unsigned controller);
} EldenCameraCciProvider;
int elden_camera_native_cci_open(const EldenCameraCciProvider *,unsigned controller);
int elden_camera_native_cci_read(uint64_t owner,uint64_t epoch,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t *);
int elden_camera_native_cci_write(uint64_t owner,uint64_t epoch,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t value);
int elden_camera_native_cci_stop(uint64_t owner,uint64_t epoch,unsigned controller);
int elden_camera_native_cci_status(unsigned controller,EldenCciStatus *);
int elden_camera_native_cci_ready(uint64_t owner,uint64_t epoch,unsigned controller);
/* All methods invoked on original camera owner worker (serialized by Context).
 * Both internal queues/master0 retained; no automatic startup/power/sensor or
 * CSI/DMA/OS17 camera publication. Partial maps/IRQ are retained to reset. */
#ifdef __cplusplus
}
#endif
#endif
