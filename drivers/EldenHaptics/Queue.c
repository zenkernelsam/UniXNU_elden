/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Queue.h"
static int limits_valid(const EldenHapticsQueueLimits *l){
    if(!l||!l->points||l->count<2||l->count>EldenHapticsQueuePoints||!l->available_mask||l->available_mask>3||
       !l->max_duration_us||l->max_duration_us>250000||!l->max_late_us||l->max_late_us>1000000||
       !l->window_us||l->window_us>10000000||!l->max_sum_gain||l->max_sum_gain>256)return 0;
    for(unsigned m=0;m<2;++m)if(l->available_mask&(1u<<m)){
        if(!l->max_gain[m]||l->max_gain[m]>128||!l->max_on_us[m]||l->max_on_us[m]>l->window_us||!l->square_budget[m])return 0;
    }
    if(l->available_mask==3&&(!l->max_dual_on_us||l->max_dual_on_us>l->window_us))return 0;
    if(l->points[0].intensity_q15||l->points[0].gain[0]||l->points[0].gain[1]||l->points[l->count-1].intensity_q15!=32767)return 0;
    for(unsigned i=0;i<l->count;++i){
        const EldenHapticsClickPoint *p=&l->points[i];
        if(p->intensity_q15>32767)return 0;
        if(i&&l->points[i-1].intensity_q15>=p->intensity_q15)return 0;
        for(unsigned m=0;m<2;++m){
            if(p->gain[m]>l->max_gain[m]||(!(l->available_mask&(1u<<m))&&p->gain[m]))return 0;
            if(i&&p->gain[m]<l->points[i-1].gain[m])return 0;
        }
        if((unsigned)p->gain[0]+p->gain[1]>l->max_sum_gain)return 0;
    }
    return 1;
}
int elden_haptics_queue_init(EldenHapticsQueue *q,const EldenHapticsQueueLimits *l,const EldenHapticsQueueIo *io){
    if(!q||q->initialized||!limits_valid(l)||!io||!io->admit||!io->start||!io->poll||!io->stop||!io->complete)return EldenHapticsRequestArgument;
    EldenHapticsQueue value={0};value.io=*io;value.limits=*l;value.active_index=-1;value.next_sequence=1;
    for(unsigned i=0;i<l->count;++i)value.points[i]=l->points[i];*q=value;q->limits.points=q->points;q->initialized=1;return 0;
}
static uint8_t gain(const EldenHapticsQueue *q,uint16_t intensity,unsigned motor){
    for(unsigned i=1;i<q->limits.count;++i){
        const EldenHapticsClickPoint *a=&q->points[i-1],*b=&q->points[i];
        if(intensity<=b->intensity_q15){unsigned width=b->intensity_q15-a->intensity_q15;
            return (uint8_t)(a->gain[motor]+((intensity-a->intensity_q15)*(b->gain[motor]-a->gain[motor])+width/2)/width);}
    }
    return q->points[q->limits.count-1].gain[motor];
}
static int any(const EldenHapticsQueue *q){for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(q->slots[i].used)return 1;return 0;}
int elden_haptics_queue_submit(EldenHapticsQueue *q,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t time,uint32_t duration,uint8_t mask,uint8_t wave,uint16_t intensity){
    if(!q||!q->initialized||!owner||!epoch||!id||!mask||mask&~q->limits.available_mask||!wave||wave>127||
       intensity>32767||!duration||duration>q->limits.max_duration_us)return EldenHapticsRequestArgument;
    if(q->held||q->stopping)return EldenHapticsRequestDenied;
    int rc=q->io.admit(q->io.context,owner,epoch);if(rc)return rc;
    if(q->last_owner==owner&&q->last_epoch==epoch&&id<=q->last_id)return EldenHapticsRequestStale;
    if((q->last_owner!=owner||q->last_epoch!=epoch)&&any(q))return EldenHapticsRequestBusy;
    int free=-1;for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(!q->slots[i].used){free=(int)i;break;}
    if(free<0||q->next_sequence==UINT64_MAX)return EldenHapticsRequestBusy;
    EldenHapticsRequest r={owner,epoch,id,time,duration,mask,wave,{0,0},0};
    for(unsigned m=0;m<2;++m)if(mask&(1u<<m))r.gain[m]=gain(q,intensity,m);
    if((unsigned)r.gain[0]+r.gain[1]>q->limits.max_sum_gain)return EldenHapticsRequestDenied;
    EldenHapticsQueueSlot slot={0};slot.request=r;slot.used=1;slot.sequence=q->next_sequence++;
    q->slots[free]=slot;q->last_owner=owner;q->last_epoch=epoch;q->last_id=id;return 0;
}
static void complete(EldenHapticsQueue *q,unsigned index,unsigned state,int error){
    const EldenHapticsRequest r=q->slots[index].request;
    /* Clear before callbacks so result observers never see a completed slot as
     * executable. Native serial gate forbids queue reentry from callbacks. */
    q->slots[index].used=0;q->slots[index].cancel=0;
    EldenHapticsResult result={r.owner,r.epoch,r.id,state,error};q->io.complete(q->io.context,&result);
}
static int stop_active(EldenHapticsQueue *q,unsigned state,int error){
    if(q->active_index<0)return 0;
    int failed=0;
    for(unsigned m=0;m<2;++m)if(q->active_mask&(1u<<m)){
        int rc=q->io.stop(q->io.context,m);
        if(rc){if(!failed)failed=rc;}else q->active_mask&=(uint8_t)~(1u<<m);
    }
    if(failed){q->held=1;q->stopping=1;q->error=failed;return failed;}
    unsigned index=(unsigned)q->active_index;q->active_index=-1;complete(q,index,state,error);return 0;
}
static int account(EldenHapticsQueue *q,const EldenHapticsRequest *r,uint64_t now){
    /* Charge full reservations until end+window, rather than resetting on a
     * calendar boundary. This bounds every rolling window conservatively even
     * if the prior waveform stopped early or the native start partly failed. */
    if(now>UINT64_MAX-q->limits.window_us-r->duration_us)return EldenHapticsRequestClock;
    uint64_t squares[2]={0,0};uint32_t on[2]={0,0},dual=0;int free=-1;
    for(unsigned i=0;i<EldenHapticsBudgetSlots;++i){
        EldenHapticsBudget *b=&q->budget[i];if(b->used&&now>=b->expires_us)b->used=0;
        if(!b->used){if(free<0)free=(int)i;continue;}
        for(unsigned m=0;m<2;++m)if(b->mask&(1u<<m)){on[m]+=b->duration_us;squares[m]+=b->square[m];}
        if(b->mask==3)dual+=b->duration_us;
    }
    if(free<0)return EldenHapticsRequestBusy;
    EldenHapticsBudget next={0};next.duration_us=r->duration_us;next.expires_us=now+r->duration_us+q->limits.window_us;next.used=1;
    for(unsigned m=0;m<2;++m)if(r->gain[m]){
        next.mask|=(uint8_t)(1u<<m);next.square[m]=(uint64_t)r->gain[m]*r->gain[m]*r->duration_us;
        if(r->duration_us>q->limits.max_on_us[m]||on[m]>q->limits.max_on_us[m]-r->duration_us||
           next.square[m]>q->limits.square_budget[m]||squares[m]>q->limits.square_budget[m]-next.square[m])return EldenHapticsRequestDenied;
    }
    if(next.mask==3&&(r->duration_us>q->limits.max_dual_on_us||dual>q->limits.max_dual_on_us-r->duration_us))return EldenHapticsRequestDenied;
    q->budget[free]=next;
    return 0;
}
int elden_haptics_queue_step(EldenHapticsQueue *q,uint64_t now){
    if(!q||!q->initialized)return EldenHapticsRequestArgument;
    if(q->has_clock&&now<q->clock_us){q->clock_failed=1;q->stopping=1;q->error=EldenHapticsRequestClock;}
    q->has_clock=1;q->clock_us=now;
    /* Cancel future work even if an active stop remains unconfirmed. */
    for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(q->slots[i].used&&(int)i!=q->active_index&&(q->stopping||q->slots[i].cancel))
        complete(q,i,EldenHapticsCancelled,q->clock_failed?EldenHapticsRequestClock:0);
    if(q->active_index>=0){
        unsigned i=(unsigned)q->active_index;EldenHapticsRequest *r=&q->slots[i].request;
        int admission=q->io.admit(q->io.context,r->owner,r->epoch);
        if(q->stopping||q->slots[i].cancel||admission||q->held){
            int rc=stop_active(q,q->held?EldenHapticsFailed:EldenHapticsCancelled,admission?admission:q->error);
            return rc?rc:q->clock_failed?EldenHapticsRequestClock:0;
        }
        if(now>=q->active_deadline)return stop_active(q,EldenHapticsComplete,0);
        for(unsigned m=0;m<2;++m)if(q->active_mask&(1u<<m)){
            int rc=q->io.poll(q->io.context,m);
            if(rc<0){q->error=rc;q->stopping=1;return stop_active(q,EldenHapticsFailed,rc);}
            if(rc==1)q->active_mask&=(uint8_t)~(1u<<m);
        }
        if(q->active_mask)return 0;
        q->active_index=-1;complete(q,i,EldenHapticsComplete,0);
    }
    if(q->stopping||q->held)return q->clock_failed?EldenHapticsRequestClock:0;
    int index=-1;uint64_t sequence=UINT64_MAX;
    for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(q->slots[i].used&&q->slots[i].sequence<sequence){sequence=q->slots[i].sequence;index=(int)i;}
    if(index<0)return 0;
    EldenHapticsRequest *r=&q->slots[index].request;
    if(now<r->not_before_us)return 0;
    int rc=q->io.admit(q->io.context,r->owner,r->epoch);
    if(!rc&&now-r->not_before_us>q->limits.max_late_us)rc=EldenHapticsRequestStale;
    if(!rc)rc=account(q,r,now);
    if(!rc&&q->io.reserve)rc=q->io.reserve(q->io.context,r,now);
    if(rc){complete(q,(unsigned)index,EldenHapticsFailed,rc);return rc;}
    q->active_index=index;q->active_deadline=now+r->duration_us;
    for(unsigned m=0;m<2;++m)if(r->gain[m]){
        rc=q->io.start(q->io.context,m,r->wave,r->gain[m],r->duration_us);
        /* A failed GO may still have physically started. Always attempt an
         * owned stop for that motor, without claiming no motion from rc alone. */
        q->active_mask|=(uint8_t)(1u<<m);
        if(rc){q->error=rc;return stop_active(q,EldenHapticsFailed,rc);}
    }
    if(!q->active_mask){q->active_index=-1;complete(q,(unsigned)index,EldenHapticsComplete,0);}
    return 0;
}
int elden_haptics_queue_cancel(EldenHapticsQueue *q,uint64_t owner,uint64_t epoch,uint64_t id){
    if(!q||!q->initialized||!owner||!epoch||!id)return EldenHapticsRequestArgument;
    for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(q->slots[i].used&&q->slots[i].request.owner==owner&&q->slots[i].request.epoch==epoch&&q->slots[i].request.id==id){q->slots[i].cancel=1;return 0;}
    return EldenHapticsRequestStale;
}
int elden_haptics_queue_disconnect(EldenHapticsQueue *q,uint64_t owner,uint64_t epoch){
    if(!q||!q->initialized||!owner||!epoch)return EldenHapticsRequestArgument;
    for(unsigned i=0;i<EldenHapticsQueueSlots;++i)if(q->slots[i].used&&q->slots[i].request.owner==owner&&q->slots[i].request.epoch==epoch)q->slots[i].cancel=1;
    return 0;
}
int elden_haptics_queue_stop(EldenHapticsQueue *q,uint64_t now){
    if(!q||!q->initialized)return EldenHapticsRequestArgument;q->stopping=1;return elden_haptics_queue_step(q,now);
}
int elden_haptics_queue_terminal(const EldenHapticsQueue *q){return q&&q->initialized&&q->stopping&&q->active_index<0&&!any(q);}
