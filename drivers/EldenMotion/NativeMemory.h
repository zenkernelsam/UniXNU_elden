/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_NATIVE_MEMORY_H
#define ELDEN_SENSOR_NATIVE_MEMORY_H
#include "SensorRpc.h"
#include "../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t iova;uint32_t bytes,session,process;
} EldenSensorMemoryConfig;
typedef struct {
    uint32_t id0,id2,asid,bank;uint64_t route_epoch;
} EldenSensorMemoryLease;
typedef struct {
    EldenSensorRpcMemory memory;EldenSensorMemoryLease lease;
    uint16_t stream_ids[4]; // exact Elden100N/104N/106N/108N for sessionN
    uint64_t data_physical,table_physical;uint32_t table_bytes;
    const volatile uint64_t *table_mapping;
    EldenWifiSmmu4KConfig table_config;EldenWifiSmmu4KResult tables;
    EldenWifiSmmu4KContextImage context;
} EldenSensorMemoryView;
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup); // nonsleeping actual lease/current physical admission
    /* Sole shared SMMU owner reads real IDs and exclusively reserves actual
     * ASID/bank plus exact four Elden session SIDs before any device traffic.
     * FastRPC session tag3..8 is independent of hardware context-bank index. */
    int (*reserve)(void *,uint64_t owner,uint64_t epoch,const EldenSensorMemoryConfig *,EldenSensorMemoryLease *);
    int (*install)(void *,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *);
    int (*observe)(void *,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *,int cleanup);
    /*0only after real remote/send/callback/DMA/fault IRQ drain, context/TLB
     * withdrawal and route release proof. All native allocations stay to reset. */
    int (*withdraw)(void *,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *);
    /* Read-only installed-context/table proof before one shared early batch
     * commit. This callback cannot publish normal DSP memory readiness. */
    int (*prepared)(void *,uint64_t,uint64_t,const EldenSensorMemoryView *);
} EldenSensorMemoryOwner;
typedef struct EldenSensorNativeMemory EldenSensorNativeMemory;
typedef struct {
    uint32_t stage,reason,access,attribute,installed,stopping;int error;
    uint64_t offset,physical,expected_physical,segment_bytes,par;
    uint64_t data_physical,table_physical;uint32_t bytes,table_pages;
} EldenSensorMemoryStatus;
/* Explicit real shared-owner call. Prepared contiguous data/table descriptors,
 * verified WB primary then fresh Unique+Prefault NC alias; table graph audited
 * before actual provider install/observe. No fixed PA/bank/ASID/MMIO owner. */
int elden_sensor_memory_native_open(const EldenSensorMemoryConfig *,const EldenSensorMemoryOwner *,EldenSensorNativeMemory **);
int elden_sensor_memory_native_prepare(const EldenSensorMemoryConfig *,const EldenSensorMemoryOwner *,EldenSensorNativeMemory **);
int elden_sensor_memory_native_activate(EldenSensorNativeMemory *,uint64_t,uint64_t);
int elden_sensor_memory_native_view(EldenSensorNativeMemory *,uint64_t owner,uint64_t epoch,EldenSensorMemoryView *);
/* Nonsleeping provider admission for the original RPC live/wait boundary.
 * Fresh hardware/context checks run separately at prepare/publish/complete. */
int elden_sensor_memory_native_admit(EldenSensorNativeMemory *,uint64_t owner,uint64_t epoch,int cleanup);
int elden_sensor_memory_native_publish(EldenSensorNativeMemory *,uint64_t owner,uint64_t epoch);
int elden_sensor_memory_native_stop(EldenSensorNativeMemory *,uint64_t owner,uint64_t epoch);
int elden_sensor_memory_native_status(EldenSensorNativeMemory *,EldenSensorMemoryStatus *);
#ifdef __cplusplus
}
#endif
#endif
