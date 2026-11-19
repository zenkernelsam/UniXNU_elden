/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_MEMORY_BINDING_H
#define ELDEN_SENSOR_MEMORY_BINDING_H
#include "NativeMemory.h"
#include "PackBinding.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Original GLINK endpoint owner. Memory/route and native sealed file provider
 * are concrete; only physical bus lifecycle is supplied by mainline. */
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    uint64_t (*now_ms)(void *);
    int (*send_epoch)(void *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
    void (*wait)(void *,uint32_t ms);
    int (*reserve_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t *);
    int (*complete_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t);
    void *(*allocate)(void *,uint32_t);
    void (*trace)(void *,const char *);
    int (*withdraw)(void *,uint64_t owner,uint64_t epoch);
    void (*wake)(void *);
} EldenSensorEndpoint;
typedef struct EldenSensorMemoryBinding EldenSensorMemoryBinding;
/* Concrete high-PA→lowIOVA memory+sealed-native-cache→originalRPC path.
 * Shared memory/endpoint/capture authorities must all have the same owner/epoch.
 * Failed stages retain *out and all mapped references until actual reset. */
int elden_sensor_memory_binding_open(const EldenSensorMemoryConfig *,const EldenSensorMemoryOwner *,
    const EldenSensorEndpoint *,const EldenSensorPackSeal *,const EldenSensorPackAuthority *,EldenSensorMemoryBinding **);
int elden_sensor_memory_binding_channel(EldenSensorMemoryBinding *,uint64_t owner,uint64_t epoch,int up);
int elden_sensor_memory_binding_receive(EldenSensorMemoryBinding *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
int elden_sensor_memory_binding_step(EldenSensorMemoryBinding *);
int elden_sensor_memory_binding_request_stop(EldenSensorMemoryBinding *,uint64_t owner,uint64_t epoch);
int elden_sensor_memory_binding_finish(EldenSensorMemoryBinding *,uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
