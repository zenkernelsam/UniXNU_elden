/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_ADSP_ENDPOINT_H
#define ELDEN_SENSOR_ADSP_ENDPOINT_H
#include "MemoryBinding.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup); // real shared bus/PD admission, nonsleeping
    /* Actual source-qualified opaque64bit ctx token, low4bitsPD2. Never
     * reuse within this live GLINK/memory session. Original owner reserves
     * it across all RPC users; endpoint tracks one outstanding invoke. */
    int (*reserve_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t *);
    int (*complete_cookie)(void *,uint64_t owner,uint64_t epoch,uint64_t);
    /*GLINK handshakes do not prove the sensors PD has stopped DMA.0only
     * after actual remote-client/memory inactivity proof from original owner.
     * Called after observed endpoint queue/callback/CLOSE fence. */
    int (*retire)(void *,uint64_t owner,uint64_t epoch,uint32_t channel_epoch,int pending_invoke);
    void (*trace)(void *,const char *); // optional bounded metadata only
} EldenSensorAdspAuthority;
typedef struct EldenSensorAdspEndpoint EldenSensorAdspEndpoint;
typedef struct {
    uint32_t connected,channel_epoch,closing,terminal,pending,received,stale,malformed;
    uint64_t owner,epoch,requests,completed;int error;
} EldenSensorAdspStatus;
/* Explicit original owner setup, same ADSP registry and worker. Only channel
 * fastrpcglink-apps-dsp; no second IPCRTR, new DSP controller/power activation.
 * Intent bounds come from reviewed original board/source input, not defaults. */
int elden_sensor_adsp_endpoint_open(const EldenSensorAdspAuthority *,uint32_t intent_size,uint32_t intent_count,
                                  EldenSensorAdspEndpoint **,EldenSensorEndpoint *);
/* Original worker creates MemoryBinding with the returned real endpoint, then
 * attaches it before first core step. State callbacks catch actual level even
 * when GLINK connected before attach. Sole DSP callback producer retained. */
int elden_sensor_adsp_endpoint_attach(EldenSensorAdspEndpoint *,EldenSensorMemoryBinding *,uint64_t owner,uint64_t epoch);
int elden_sensor_adsp_endpoint_status(EldenSensorAdspEndpoint *,EldenSensorAdspStatus *);
int elden_sensor_adsp_endpoint_close(EldenSensorAdspEndpoint *,uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
