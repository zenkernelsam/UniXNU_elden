/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_MAILBOX_H
#define ELDEN_HAPTICS_MAILBOX_H
#include "Queue.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenHapticsMailEmpty=0,EldenHapticsMailPending=1,EldenHapticsMailExecuting=2,EldenHapticsMailResult=3};
typedef struct {
    EldenHapticsRequest request;EldenHapticsResult result;
    uint16_t intensity_q15;
    uint32_t effect_id; /*0=RAM, >0 immutable native catalog; never raw PCM*/
    uint8_t state,cancel,cancel_sent,reserved;
} EldenHapticsMail;
typedef struct {
    EldenHapticsMail slots[EldenHapticsQueueSlots];
    uint64_t owner,epoch,last_id;
    uint8_t closing,stopping,held,reserved;
} EldenHapticsMailbox;
/* The actual native IOLock serializes all methods. Only one kernel-assigned
 * owner/epoch is admitted at a time. Each submit reserves its result slot;
 * completion can never silently overflow a separate result ring. */
int elden_haptics_mail_open(EldenHapticsMailbox *,uint64_t owner,uint64_t epoch);
int elden_haptics_mail_submit(EldenHapticsMailbox *,const EldenHapticsRequest *,uint16_t intensity_q15);
int elden_haptics_mail_submit_effect(EldenHapticsMailbox *,const EldenHapticsRequest *,uint16_t intensity_q15,uint32_t effect_id);
int elden_haptics_mail_take(EldenHapticsMailbox *,EldenHapticsRequest *,uint16_t *intensity_q15);
int elden_haptics_mail_take_native(EldenHapticsMailbox *,EldenHapticsRequest *,uint16_t *intensity_q15,uint32_t *effect_id);
int elden_haptics_mail_cancel(EldenHapticsMailbox *,uint64_t owner,uint64_t epoch,uint64_t id);
int elden_haptics_mail_next_cancel(EldenHapticsMailbox *,EldenHapticsRequest *);
int elden_haptics_mail_finish(EldenHapticsMailbox *,const EldenHapticsResult *);
int elden_haptics_mail_result(EldenHapticsMailbox *,uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *);
int elden_haptics_mail_close(EldenHapticsMailbox *,uint64_t owner,uint64_t epoch);
void elden_haptics_mail_stop(EldenHapticsMailbox *);
int elden_haptics_mail_work(const EldenHapticsMailbox *);
#ifdef __cplusplus
}
#endif
#endif
