/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_BROKER_H
#define ELDEN_HAPTICS_BROKER_H
#include "Catalog.h"
#include "Mailbox.h"
#include "Budget.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    int (*admit)(void *,int cleanup); /*actual native recovery/temperature/session scope*/
    void (*lock)(void *,int acquire); /*real native IOLock; never held over hardware*/
    uint64_t (*now_us)(void *);
    void *(*allocate)(void *,size_t);
    void (*release)(void *,void *,size_t);
} EldenHapticsBrokerIo;
typedef struct {
    EldenHapticsBrokerIo io;EldenAwChip *chips[2];
    EldenHapticsMailbox mail;EldenHapticsQueue ram;EldenSharedBudget budget;EldenRtpStream rtp;
    const EldenHapticsEffect *effects;size_t effect_count;EldenRtpLimits rtp_limits;
    EldenHapticsRequest current;uint64_t next_owner,live_owner,epoch,clock_us;
    int8_t *pcm[2];size_t pcm_bytes;uint32_t effect_id,current_kind;
    uint16_t current_intensity;uint8_t initialized,has_clock,ram_ready,rtp_ready,held,stopping;
    int error;
} EldenHapticsBroker;
/* Same boot-lifetime broker holds RAM and RTP budgets across client reconnects.
 * Default absent limits/catalog initialize with NO output; zero-gain inspection
 * remains native's separate controller setup. Host synthetic limits never enter
 * the production generator. Only worker calls step/stop; API methods lock mail. */
int elden_broker_init(EldenHapticsBroker *,const EldenHapticsBrokerIo *,EldenAwChip *[2],
    const EldenHapticsQueueLimits *,const EldenSharedBudgetLimits *,const EldenRtpLimits *,const EldenHapticsEffect *,size_t count);
int elden_broker_open(EldenHapticsBroker *,uint64_t *owner,uint64_t *epoch);
int elden_broker_close(EldenHapticsBroker *,uint64_t owner,uint64_t epoch);
int elden_broker_submit_ram(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay_us,
    uint32_t duration_us,uint8_t mask,uint8_t wave,uint16_t intensity_q15);
int elden_broker_submit_effect(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,uint32_t effect_id,uint64_t delay_us,uint16_t intensity_q15);
int elden_broker_cancel(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id);
int elden_broker_result(EldenHapticsBroker *,uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *);
int elden_broker_step(EldenHapticsBroker *);
int elden_broker_stop(EldenHapticsBroker *);
int elden_broker_work(EldenHapticsBroker *);
/* Same native worker's AW playback admission; never asks userspace for gains. */
int elden_broker_output_admit(EldenHapticsBroker *);
#ifdef __cplusplus
}
#endif
#endif
