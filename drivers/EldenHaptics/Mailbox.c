/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Mailbox.h"
static int owner(const EldenHapticsMailbox *m,uint64_t who,uint64_t epoch){
    return m&&who&&epoch&&m->owner==who&&m->epoch==epoch;
}
static int found(const EldenHapticsMailbox *m,uint64_t who,uint64_t epoch,uint64_t id){
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(m->slots[i].state&&m->slots[i].request.owner==who&&
        m->slots[i].request.epoch==epoch&&m->slots[i].request.id==id)return (int)i;
    return -1;
}
static void closed(EldenHapticsMailbox *m){
    if(!m->closing)return;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(m->slots[i].state)return;
    m->owner=m->epoch=m->last_id=0;m->closing=0;
}
int elden_haptics_mail_open(EldenHapticsMailbox *m,uint64_t who,uint64_t epoch){
    if(!m||!who||!epoch)return EldenHapticsRequestArgument;
    if(m->stopping||m->held)return EldenHapticsRequestDenied;
    if(m->owner||m->closing)return EldenHapticsRequestBusy;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(m->slots[i].state)return EldenHapticsRequestBusy;
    m->owner=who;m->epoch=epoch;m->last_id=0;return 0;
}
int elden_haptics_mail_submit(EldenHapticsMailbox *m,const EldenHapticsRequest *r,uint16_t intensity){
    if(!r||!r->id||!r->mask||r->mask>3||!r->wave||r->wave>127||!r->duration_us||r->duration_us>250000||
       r->reserved||r->gain[0]||r->gain[1]||intensity>32767)return EldenHapticsRequestArgument;
    if(!owner(m,r->owner,r->epoch)||m->closing||m->stopping||m->held)return EldenHapticsRequestDenied;
    if(r->id<=m->last_id)return EldenHapticsRequestStale;
    int empty=-1;for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(!m->slots[i].state){empty=(int)i;break;}
    if(empty<0)return EldenHapticsRequestBusy;
    EldenHapticsMail value={0};value.request=*r;value.intensity_q15=intensity;value.state=EldenHapticsMailPending;
    m->slots[empty]=value;m->last_id=r->id;return 0;
}
int elden_haptics_mail_take(EldenHapticsMailbox *m,EldenHapticsRequest *out,uint16_t *intensity){
    if(!m||!out||!intensity)return EldenHapticsRequestArgument;
    if(m->stopping||m->held||m->closing)return EldenHapticsRequestDenied;
    int at=-1;uint64_t id=UINT64_MAX;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(m->slots[i].state==EldenHapticsMailPending&&m->slots[i].request.id<id){
        id=m->slots[i].request.id;at=(int)i;
    }
    if(at<0)return EldenHapticsRequestBusy;
    m->slots[at].state=EldenHapticsMailExecuting;*out=m->slots[at].request;*intensity=m->slots[at].intensity_q15;return 0;
}
int elden_haptics_mail_cancel(EldenHapticsMailbox *m,uint64_t who,uint64_t epoch,uint64_t id){
    if(!owner(m,who,epoch)||!id)return EldenHapticsRequestDenied;
    int at=found(m,who,epoch,id);if(at<0)return EldenHapticsRequestStale;
    EldenHapticsMail *s=&m->slots[at];s->cancel=1;
    if(s->state==EldenHapticsMailPending){
        s->result=(EldenHapticsResult){who,epoch,id,EldenHapticsCancelled,0};s->state=EldenHapticsMailResult;
    }
    return 0;
}
int elden_haptics_mail_next_cancel(EldenHapticsMailbox *m,EldenHapticsRequest *out){
    if(!m||!out)return EldenHapticsRequestArgument;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++){
        EldenHapticsMail *s=&m->slots[i];
        if(s->state==EldenHapticsMailExecuting&&s->cancel&&!s->cancel_sent){s->cancel_sent=1;*out=s->request;return 0;}
    }
    return EldenHapticsRequestBusy;
}
int elden_haptics_mail_finish(EldenHapticsMailbox *m,const EldenHapticsResult *r){
    if(!m||!r||!r->id||r->state<EldenHapticsComplete||r->state>EldenHapticsFailed)return EldenHapticsRequestArgument;
    int at=found(m,r->owner,r->epoch,r->id);if(at<0||m->slots[at].state!=EldenHapticsMailExecuting)return EldenHapticsRequestStale;
    if(m->closing){EldenHapticsMail zero={0};m->slots[at]=zero;closed(m);}
    else{m->slots[at].result=*r;m->slots[at].state=EldenHapticsMailResult;}
    return 0;
}
int elden_haptics_mail_result(EldenHapticsMailbox *m,uint64_t who,uint64_t epoch,uint64_t id,EldenHapticsResult *out){
    if(!out||!owner(m,who,epoch)||!id)return EldenHapticsRequestDenied;
    int at=found(m,who,epoch,id);if(at<0)return EldenHapticsRequestStale;
    if(m->slots[at].state!=EldenHapticsMailResult)return EldenHapticsRequestBusy;
    *out=m->slots[at].result;EldenHapticsMail zero={0};m->slots[at]=zero;closed(m);return 0;
}
int elden_haptics_mail_close(EldenHapticsMailbox *m,uint64_t who,uint64_t epoch){
    if(!owner(m,who,epoch))return EldenHapticsRequestStale;m->closing=1;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++){
        EldenHapticsMail *s=&m->slots[i];s->cancel=1;
        if(s->state!=EldenHapticsMailExecuting){EldenHapticsMail zero={0};*s=zero;}
    }
    closed(m);return 0;
}
void elden_haptics_mail_stop(EldenHapticsMailbox *m){
    if(!m)return;m->stopping=1;if(m->owner)(void)elden_haptics_mail_close(m,m->owner,m->epoch);
}
int elden_haptics_mail_work(const EldenHapticsMailbox *m){
    if(!m)return 0;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(m->slots[i].state==EldenHapticsMailPending||m->slots[i].state==EldenHapticsMailExecuting)return 1;
    return 0;
}
