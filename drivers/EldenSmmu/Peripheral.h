/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SMMU_PERIPHERAL_H
#define ELDEN_SMMU_PERIPHERAL_H
#include "Shared.h"
#include "../EldenCamera/NativeDma.h"
#include "../EldenMotion/NativeMemory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    /* Same real physical owner serializes peripheral APIs with its early
     * SMMU lease. The shared core owns register/context observation; these
     * callbacks prove retained IOKit backing and device-write visibility. */
    int (*admit)(void *,uint64_t,uint64_t,unsigned,int cleanup);
    int (*camera_memory)(void *,const EldenCameraDmaView *,int cleanup);
    int (*camera_visible)(void *,const EldenCameraDmaView *,const EldenCameraBuffer *);
    int (*sensor_memory)(void *,const EldenSensorMemoryView *,int cleanup);
} EldenSmmuPeripheralOps;
typedef union {uint64_t alignment;uint8_t bytes[16384];} EldenSmmuPeripheral;
/* The sole shared EARLY owner has already reserved the complete batch. The
 * adapters do not create a controller, append a route or start a device. */
int elden_smmu_peripheral_open(EldenSmmuPeripheral *,EldenSmmuShared *,
    const EldenSmmuSharedConfig *,const EldenSmmuPeripheralOps *);
int elden_smmu_camera_owner(EldenSmmuPeripheral *,EldenCameraDmaOwner *);
int elden_smmu_sensor_owner(EldenSmmuPeripheral *,EldenSensorMemoryOwner *);
/* After the REAL shared commit, switches adapter admission to the exact
 * observed context/route, then callers activate each retained native object.
 * One adapter failure closes both normal paths, never cleanup/retained maps. */
int elden_smmu_peripheral_activate(EldenSmmuPeripheral *,uint64_t,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
