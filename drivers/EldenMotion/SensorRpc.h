/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_RPC_H
#define ELDEN_SENSOR_RPC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Real provider must own this contiguous DMA IOVA and alias until an actual
 * remote/callback/reset fence; no fixed physical heap or guessed free CB.
 * session is FastRPC DT reg3..8, encoded at bit32 on exact SM8750, not a
 * hardware SMMU bank.2MiB call space plus nonzero heap, maximum64MiB. */
typedef struct {uint8_t *arena;uint64_t iova;uint32_t bytes,session,process;} EldenSensorRpcMemory;
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    uint64_t (*now_ms)(void *);
    int (*send_epoch)(void *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
    void (*wait)(void *,uint32_t ms);
    int (*publish)(void *,uint64_t owner,uint64_t epoch);
    int (*prepare)(void *,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *);
    int (*reserve_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t *wire_ctx);
    int (*complete_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t wire_ctx);
    /* Immutable same-device source-qualified pack. 0+size only with hash,
     * firmware/build/device/registry/namespace identity verified; positive
     * value1only if Data not mounted yet. No empty/missing fallback. */
    int (*pack_size)(void *,uint64_t owner,uint64_t epoch,uint64_t *);
    int (*pack_read)(void *,uint64_t owner,uint64_t epoch,uint64_t offset,void *,uint32_t);
    void *(*allocate)(void *,uint32_t);
    void (*trace)(void *,const char *); // optional bounded metadata, no file content
    int (*withdraw)(void *,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *);
    void (*wake)(void *); // stop/transport events wake original executor's wait
} EldenSensorRpcProvider;
typedef struct EldenSensorRpc EldenSensorRpc;
typedef struct {
    int32_t stage,error;uint32_t calls,opens,reads,missing,heap_used,terminal;
    uint64_t owner,epoch,bytes;
} EldenSensorRpcStatus;
/* Original client worker is the only executor; no automatic thread/channel/
 * mapping is created. Context/callback storage remains retained to reset. */
int elden_sensor_rpc_open(const EldenSensorRpcProvider *,const EldenSensorRpcMemory *,EldenSensorRpc **);
int elden_sensor_rpc_channel(EldenSensorRpc *,uint64_t owner,uint64_t epoch,int up);
int elden_sensor_rpc_receive(EldenSensorRpc *,uint64_t owner,uint64_t epoch,const uint8_t *,uint32_t);
/* One original service step; may wait for DSP reverse call. Provider wait
 * must wake when request_stop is latched; live gate then refuses further DMA. */
int elden_sensor_rpc_step(EldenSensorRpc *);
int elden_sensor_rpc_request_stop(EldenSensorRpc *,uint64_t owner,uint64_t epoch);
int elden_sensor_rpc_finish(EldenSensorRpc *,uint64_t owner,uint64_t epoch);
int elden_sensor_rpc_status(EldenSensorRpc *,EldenSensorRpcStatus *);
#ifdef __cplusplus
}
#endif
#endif
