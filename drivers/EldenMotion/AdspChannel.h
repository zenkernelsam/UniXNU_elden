/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_ADSP_OWNED_CHANNEL_H
#define ELDEN_ADSP_OWNED_CHANNEL_H
#include "ShengAdsp.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void (*state)(void *,uint32_t channel_epoch,int up);
    void (*receive)(void *,uint32_t channel_epoch,const uint8_t *,uint32_t);
} EldenAdspEpochClient;
typedef struct {
    uint32_t epoch,connected,queued,pumping,callbacks,closing,close_sent,close_acked,peer_closed,fenced;
    int error;
} EldenAdspChannelStatus;
/* Same actual ADSP registry/worker/GLINK endpoint; cannot duplicate a name or
 * create a second transport. Epoch callbacks carry the exact channel state.
 * Closing fences future callbacks/sends, purges only the owning queued data
 * on the DSP worker, then waits for both GLINK CLOSE handshakes. */
ShengAdspChannel *elden_adsp_channel_epoch(const char *name,uint32_t intent_size,uint32_t intent_count,int initiator,
                                        const EldenAdspEpochClient *,void *);
int elden_adsp_channel_status(ShengAdspChannel *,EldenAdspChannelStatus *);
/* Queue/callback/CLOSE state only, never a proof of physical DSP/DMA stop.0
 * means request latched. status.fenced is the separately observed endpoint
 * proof; the real memory owner must still prove remote inactivity/withdrawal. */
int elden_adsp_channel_close(ShengAdspChannel *,uint32_t channel_epoch);
#ifdef __cplusplus
}
#endif
#endif
