/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_PACK_BINDING_H
#define ELDEN_SENSOR_PACK_BINDING_H
#include "SensorRpc.h"
#include "NativePack.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Original shared GLINK/DMA owner operations only. File reads come from the
 * actual native sealed cache, never a success-only external file callback. */
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    uint64_t (*now_ms)(void *);
    int (*send_epoch)(void *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
    void (*wait)(void *,uint32_t ms);
    int (*publish)(void *,uint64_t owner,uint64_t epoch);
    int (*prepare)(void *,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *);
    int (*reserve_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t *);
    int (*complete_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t);
    void *(*allocate)(void *,uint32_t);
    void (*trace)(void *,const char *);
    int (*withdraw)(void *,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *);
    void (*wake)(void *);
} EldenSensorRpcBus;
typedef struct EldenSensorPackBinding EldenSensorPackBinding;
/* Native cache + actual core are connected immediately. A failed open keeps
 * the allocated binding/callback references in *out until reset. Sole original
 * client worker executes step; callbacks only notify/copy. No DSP auto-start. */
int elden_sensor_pack_binding_open(const EldenSensorRpcBus *,const EldenSensorRpcMemory *,
                                  const EldenSensorPackSeal *,const EldenSensorPackAuthority *,EldenSensorPackBinding **);
int elden_sensor_pack_binding_channel(EldenSensorPackBinding *,uint64_t owner,uint64_t epoch,int up);
int elden_sensor_pack_binding_receive(EldenSensorPackBinding *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
int elden_sensor_pack_binding_step(EldenSensorPackBinding *);
int elden_sensor_pack_binding_request_stop(EldenSensorPackBinding *,uint64_t owner,uint64_t epoch);
int elden_sensor_pack_binding_finish(EldenSensorPackBinding *,uint64_t owner,uint64_t epoch);
int elden_sensor_pack_binding_status(EldenSensorPackBinding *,EldenSensorRpcStatus *,EldenSensorPackStatus *);
#ifdef __cplusplus
}
#endif
#endif
