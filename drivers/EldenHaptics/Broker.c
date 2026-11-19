/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Broker.h"
static void lock(EldenHapticsBroker *b){b->io.lock(b->io.context,1);}
static void unlock(EldenHapticsBroker *b){b->io.lock(b->io.context,0);}
static int fail(EldenHapticsBroker *b,int error){
    __atomic_store_n(&b->held,1u,__ATOMIC_RELEASE);b->error=error;
    lock(b);b->mail.held=1;unlock(b);return error;
}
static int session(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch){
    lock(b);int valid=b->mail.owner==owner&&b->mail.epoch==epoch&&!b->mail.closing&&!b->mail.stopping&&!b->mail.held;unlock(b);
    return valid?0:-125;
}
int elden_broker_output_admit(EldenHapticsBroker *b){
    if(!b||!__atomic_load_n(&b->initialized,__ATOMIC_ACQUIRE)||__atomic_load_n(&b->held,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&b->stopping,__ATOMIC_ACQUIRE))return -125;
    uint64_t owner=__atomic_load_n(&b->live_owner,__ATOMIC_ACQUIRE);
    if(!owner||session(b,owner,b->epoch))return -125;
    return b->io.admit(b->io.context,0);
}
static int ram_admit(void *v,uint64_t owner,uint64_t epoch){
    EldenHapticsBroker *b=v;if(session(b,owner,epoch))return -125;return elden_broker_output_admit(b);
}
static int ram_start(void *v,unsigned motor,uint8_t wave,uint8_t gain,uint32_t duration){
    EldenHapticsBroker *b=v;return motor<2?elden_aw_ram_start(b->chips[motor],wave,gain,duration):-22;
}
static int ram_poll(void *v,unsigned motor){EldenHapticsBroker *b=v;return motor<2?elden_aw_poll(b->chips[motor]):-22;}
static int ram_stop(void *v,unsigned motor){EldenHapticsBroker *b=v;return motor<2?elden_aw_stop(b->chips[motor]):-22;}
static void completion(EldenHapticsBroker *b,const EldenHapticsResult *input){
    EldenHapticsResult r=*input;
    int budget=elden_shared_budget_finish(&b->budget,r.owner,r.epoch,r.id,b->io.now_us(b->io.context),1);
    if(budget&&budget!=-116){r.state=EldenHapticsFailed;r.error=budget;fail(b,budget);}
    lock(b);int rc=elden_haptics_mail_finish(&b->mail,&r);unlock(b);
    if(rc)fail(b,rc);
    __atomic_store_n(&b->current_kind,0u,__ATOMIC_RELEASE);
}
static void ram_complete(void *v,const EldenHapticsResult *r){completion(v,r);}
static int ram_reserve(void *v,const EldenHapticsRequest *r,uint64_t now){
    EldenHapticsBroker *b=v;(void)now;
    // Same-step previous STOP may have advanced the hardware clock while the
    // queue still holds its entry timestamp. Use fresh time for shared budget.
    return elden_shared_budget_ram(&b->budget,r->owner,r->epoch,r->id,b->io.now_us(b->io.context),r->duration_us,r->gain);
}
static int rtp_admit(void *v,uint64_t id,int cleanup){
    EldenHapticsBroker *b=v;if(id!=b->current.id)return -116;
    if(cleanup)return b->io.admit(b->io.context,1);
    int rc=session(b,b->current.owner,b->current.epoch);return rc?rc:elden_broker_output_admit(b);
}
static int rtp_reserve(void *v,uint64_t id,uint32_t duration,const uint64_t square[2],const uint32_t on[2]){
    EldenHapticsBroker *b=v;if(id!=b->current.id)return -116;
    uint32_t time[2];for(unsigned m=0;m<2;m++)time[m]=(uint32_t)(((uint64_t)on[m]*1000000+23999)/24000);
    // Worst overlap bounds simultaneous motor-on time without a false exact
    // phase/coil model. A signed waveform's zeros don't prove motor idle.
    uint32_t dual=time[0]<time[1]?time[0]:time[1];
    return elden_shared_budget_reserve(&b->budget,b->current.owner,b->current.epoch,id,b->io.now_us(b->io.context),duration,square,time,dual);
}
static uint64_t rtp_now(void *v){EldenHapticsBroker *b=v;return b->io.now_us(b->io.context);}
static void pcm_release(EldenHapticsBroker *b){
    for(unsigned m=0;m<2;m++)if(b->pcm[m]){b->io.release(b->io.context,b->pcm[m],b->pcm_bytes);b->pcm[m]=0;}
    b->pcm_bytes=0;
}
int elden_broker_init(EldenHapticsBroker *b,const EldenHapticsBrokerIo *io,EldenAwChip *chips[2],
    const EldenHapticsQueueLimits *ram,const EldenSharedBudgetLimits *shared,const EldenRtpLimits *rtp,const EldenHapticsEffect *effects,size_t count){
    if(!b||b->initialized||b->budget.initialized||b->ram.initialized||b->rtp.state||!io||!io->admit||!io->lock||!io->now_us||!io->allocate||!io->release||!chips||!chips[0]||!chips[1]||
       (count&&!rtp)||((ram||count)&&!shared)||elden_haptics_catalog_check(effects,count,rtp))return -22;
    if(count&&rtp->window_frames!=(uint64_t)shared->window_us*24000/1000000)return -22;
    EldenHapticsBroker zero={0};*b=zero;b->io=*io;b->chips[0]=chips[0];b->chips[1]=chips[1];b->epoch=1;b->next_owner=1;
    if(shared){int rc=elden_shared_budget_init(&b->budget,shared);if(rc)return rc;}
    if(ram){EldenHapticsQueueIo qio={b,ram_admit,ram_start,ram_poll,ram_stop,ram_complete,ram_reserve};
        int rc=elden_haptics_queue_init(&b->ram,ram,&qio);if(rc)return rc;b->ram_ready=1;}
    if(count){b->effects=effects;b->effect_count=count;b->rtp_limits=*rtp;b->rtp_ready=1;}
    __atomic_store_n(&b->initialized,1u,__ATOMIC_RELEASE);return 0;
}
int elden_broker_open(EldenHapticsBroker *b,uint64_t *owner,uint64_t *epoch){
    if(!b||!owner||!epoch||!__atomic_load_n(&b->initialized,__ATOMIC_ACQUIRE))return -22;
    lock(b);int rc=-16;
    if(__atomic_load_n(&b->held,__ATOMIC_ACQUIRE)||__atomic_load_n(&b->stopping,__ATOMIC_ACQUIRE)){unlock(b);return -125;}
    if(__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)){unlock(b);return -16;}
    if(b->next_owner!=UINT64_MAX){uint64_t next=b->next_owner;rc=elden_haptics_mail_open(&b->mail,next,b->epoch);
        if(!rc){b->next_owner++;__atomic_store_n(&b->live_owner,next,__ATOMIC_RELEASE);*owner=next;*epoch=b->epoch;}}
    unlock(b);return rc;
}
int elden_broker_close(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch){
    if(!b||!b->initialized)return -22;
    lock(b);int rc=elden_haptics_mail_close(&b->mail,owner,epoch);if(!rc)__atomic_store_n(&b->live_owner,0u,__ATOMIC_RELEASE);unlock(b);return rc;
}
static int submit(EldenHapticsBroker *b,EldenHapticsRequest *r,uint64_t delay,uint16_t intensity,uint32_t effect){
    if(!r->id||delay>1000000||intensity>32767)return -22;
    int rc=elden_broker_output_admit(b);if(rc)return rc;
    uint64_t now=b->io.now_us(b->io.context);if(now>UINT64_MAX-delay)return -22;r->not_before_us=now+delay;
    lock(b);rc=__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)==3?-16:
        effect?elden_haptics_mail_submit_effect(&b->mail,r,intensity,effect):elden_haptics_mail_submit(&b->mail,r,intensity);unlock(b);return rc;
}
int elden_broker_submit_ram(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay,uint32_t duration,uint8_t mask,uint8_t wave,uint16_t intensity){
    if(!b||!b->initialized||!b->ram_ready||!duration||duration>b->ram.limits.max_duration_us||!mask||mask&~3u||!wave)return -13;
    for(unsigned m=0;m<2;m++)if(mask&(1u<<m)&&wave>b->chips[m]->waves)return -22;
    EldenHapticsRequest r={owner,epoch,id,0,duration,mask,wave,{0,0},0};return submit(b,&r,delay,intensity,0);
}
int elden_broker_submit_effect(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t id,uint32_t effect,uint64_t delay,uint16_t intensity){
    if(!b||!b->initialized||!b->rtp_ready)return -13;
    const EldenHapticsEffect *e=elden_haptics_catalog_find(b->effects,b->effect_count,effect);if(!e)return -116;
    uint32_t duration=(uint32_t)(((uint64_t)e->frames*1000000+23999)/24000);
    EldenHapticsRequest r={owner,epoch,id,0,duration,e->mask,1,{0,0},0};return submit(b,&r,delay,intensity,effect);
}
int elden_broker_cancel(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t id){
    if(!b||!b->initialized)return -22;lock(b);int rc=elden_haptics_mail_cancel(&b->mail,owner,epoch,id);unlock(b);return rc;
}
int elden_broker_result(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *out){
    if(!b||!b->initialized)return -22;lock(b);int rc=elden_haptics_mail_result(&b->mail,owner,epoch,id,out);unlock(b);return rc;
}
static int start_rtp(EldenHapticsBroker *b){
    EldenRtpStream zero={0};b->rtp=zero;
    const EldenHapticsEffect *effect=elden_haptics_catalog_find(b->effects,b->effect_count,b->effect_id);if(!effect)return -116;
    EldenRtpClip clip={0};clip.id=b->current.id;clip.rate_hz=24000;clip.mask=effect->mask;clip.frames=effect->frames;b->pcm_bytes=effect->frames;
    for(unsigned m=0;m<2;m++)if(effect->mask&(1u<<m)){
        b->pcm[m]=b->io.allocate(b->io.context,effect->frames);if(!b->pcm[m]){pcm_release(b);return -12;}
        for(size_t i=0;i<effect->frames;i++){
            int value=effect->pcm[m][i];unsigned magnitude=(unsigned)(value<0?-value:value);
            int scaled=(int)((magnitude*b->current_intensity+16383)/32767);b->pcm[m][i]=(int8_t)(value<0?-scaled:scaled);
        }
        clip.pcm[m]=b->pcm[m];clip.gain[m]=effect->gain[m];
    }
    EldenRtpIo io={b,rtp_admit,rtp_reserve,rtp_now};
    return elden_rtp_stream_start(&b->rtp,b->chips,&io,&b->rtp_limits,&clip);
}
static int end_rtp(EldenHapticsBroker *b,int error,uint32_t state){
    if(b->rtp.state==EldenRtpHeld){
        (void)elden_shared_budget_finish(&b->budget,b->current.owner,b->current.epoch,b->current.id,b->io.now_us(b->io.context),0);
        return fail(b,error?error:-125); // retain PCM, pending receipt and original owner
    }
    // No RTP state after preflight rejection means no GO/owned chip; buffers
    // are safe to release. All other terminal states imply actual STOP/restore.
    pcm_release(b);EldenHapticsResult r={b->current.owner,b->current.epoch,b->current.id,state,error};completion(b,&r);return error;
}
int elden_broker_step(EldenHapticsBroker *b){
    if(!b||!b->initialized)return -22;if(__atomic_load_n(&b->held,__ATOMIC_ACQUIRE))return b->error?b->error:-125;
    if(__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)==3)return 0; // actual retained Actuator worker path
    // Only the worker reads chip state. API threads consume the atomic held
    // gate and mailbox owner rather than racing AW's register-state updates.
    if(b->chips[0]->held||b->chips[1]->held||!b->chips[0]->ready||!b->chips[1]->ready)return fail(b,-125);
    uint64_t now=b->io.now_us(b->io.context);
    if(b->has_clock&&now<b->clock_us){b->error=-110;int stopped=elden_broker_stop(b);return fail(b,stopped?stopped:-110);}
    b->has_clock=1;b->clock_us=now;
    EldenHapticsRequest cancel;lock(b);int cancellation=elden_haptics_mail_next_cancel(&b->mail,&cancel);unlock(b);
    if(!cancellation&&b->current_kind==1)(void)elden_haptics_queue_cancel(&b->ram,cancel.owner,cancel.epoch,cancel.id);
    if(b->current_kind==2){
        int rc;
        if(!cancellation||session(b,b->current.owner,b->current.epoch)||b->stopping){
            rc=elden_rtp_stream_stop(&b->rtp);return end_rtp(b,rc,EldenHapticsCancelled);
        }
        rc=elden_rtp_stream_step(&b->rtp);
        if(rc==1)return end_rtp(b,0,EldenHapticsComplete);
        if(rc<0)return end_rtp(b,rc,EldenHapticsFailed);return 0;
    }
    if(b->ram_ready){int rc=elden_haptics_queue_step(&b->ram,now);
        if(b->ram.held){
            (void)elden_shared_budget_finish(&b->budget,b->current.owner,b->current.epoch,b->current.id,b->io.now_us(b->io.context),0);return fail(b,rc?rc:-125);}
        if(b->ram.stopping){b->error=rc?rc:b->ram.error;return elden_broker_stop(b);}
    }
    if(b->current_kind||b->stopping)return 0;
    now=b->io.now_us(b->io.context);
    // Do not take another request across a still-active hardware/BSD callback.
    lock(b);
    if(__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)==3){unlock(b);return 0;}
    uint64_t first=UINT64_MAX;EldenHapticsMail *next=0;
    for(unsigned i=0;i<EldenHapticsQueueSlots;i++)if(b->mail.slots[i].state==EldenHapticsMailPending&&b->mail.slots[i].request.id<first){next=&b->mail.slots[i];first=next->request.id;}
    int rc=-16;if(next&&now>=next->request.not_before_us)rc=elden_haptics_mail_take_native(&b->mail,&b->current,&b->current_intensity,&b->effect_id);
    unlock(b);if(rc)return rc==-16||rc==-13?0:rc;
    if(b->effect_id){
        EldenRtpStream zero={0};b->rtp=zero;
        __atomic_store_n(&b->current_kind,2u,__ATOMIC_RELEASE);
        if(now-b->current.not_before_us>1000000)return end_rtp(b,-116,EldenHapticsFailed);
        rc=start_rtp(b);if(rc)return end_rtp(b,rc,EldenHapticsFailed);return 0;
    }
    __atomic_store_n(&b->current_kind,1u,__ATOMIC_RELEASE);
    rc=elden_haptics_queue_submit(&b->ram,b->current.owner,b->current.epoch,b->current.id,b->current.not_before_us,
        b->current.duration_us,b->current.mask,b->current.wave,b->current_intensity);
    if(rc){EldenHapticsResult r={b->current.owner,b->current.epoch,b->current.id,EldenHapticsFailed,rc};completion(b,&r);return rc;}
    rc=elden_haptics_queue_step(&b->ram,now);
    if(b->ram.held){(void)elden_shared_budget_finish(&b->budget,b->current.owner,b->current.epoch,b->current.id,b->io.now_us(b->io.context),0);return fail(b,rc?rc:-125);}
    return rc;
}
int elden_broker_stop(EldenHapticsBroker *b){
    if(!b||!b->initialized)return -22;
    __atomic_store_n(&b->stopping,1u,__ATOMIC_RELEASE);__atomic_store_n(&b->live_owner,0u,__ATOMIC_RELEASE);
    lock(b);elden_haptics_mail_stop(&b->mail);unlock(b);int error=0;
    if(__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)==3)return fail(b,-125); // native must first stop/release actual Actuator
    if(b->current_kind==2){int rc=elden_rtp_stream_stop(&b->rtp);if(rc)error=rc;
        rc=end_rtp(b,rc,EldenHapticsCancelled);if(rc&&!error)error=rc;}
    if(b->ram_ready){int rc=elden_haptics_queue_stop(&b->ram,b->io.now_us(b->io.context));if(rc&&!error)error=rc;
        if(!elden_haptics_queue_terminal(&b->ram)&&!error)error=-16;}
    if(error||b->held||elden_shared_budget_active(&b->budget))return fail(b,error?error:b->error?b->error:-125);
    return 0;
}
int elden_broker_work(EldenHapticsBroker *b){
    if(!b||!b->initialized)return 0;lock(b);int work=elden_haptics_mail_work(&b->mail);unlock(b);return work;
}
int elden_broker_actuator_claim(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch){
    if(!b||!b->initialized||!b->budget.initialized)return -13;
    lock(b);
    int rc=0;
    if(b->mail.owner!=owner||b->mail.epoch!=epoch||b->mail.closing||b->mail.stopping||b->mail.held||b->held||b->stopping)rc=-125;
    else if(__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)||elden_haptics_mail_work(&b->mail))rc=-16;
    if(!rc){EldenHapticsRequest r={0};r.owner=owner;r.epoch=epoch;b->current=r;
        __atomic_store_n(&b->current_kind,3u,__ATOMIC_RELEASE);}
    unlock(b);return rc;
}
int elden_broker_actuator_reserve(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t run,uint32_t lease,
    const uint64_t square[2],const uint32_t on[2]){
    if(!b||!square||!on||__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)!=3||b->current.owner!=owner||b->current.epoch!=epoch)return -116;
    int rc=elden_broker_output_admit(b);if(rc)return rc;
    uint32_t time[2];for(unsigned m=0;m<2;m++)time[m]=(uint32_t)(((uint64_t)on[m]*1000000+23999)/24000);
    uint32_t dual=time[0]<time[1]?time[0]:time[1];
    return elden_shared_budget_reserve(&b->budget,owner,epoch,run,b->io.now_us(b->io.context),lease,square,time,dual);
}
int elden_broker_actuator_finish(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t run,int confirmed){
    if(!b||__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)!=3||b->current.owner!=owner||b->current.epoch!=epoch)return -116;
    int rc=elden_shared_budget_finish_actual(&b->budget,owner,epoch,run,b->io.now_us(b->io.context),confirmed);
    if(rc)fail(b,rc);return rc;
}
int elden_broker_actuator_settle(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,uint64_t run,int confirmed,
    const uint64_t square[2],const uint32_t on[2]){
    if(!b||__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)!=3||b->current.owner!=owner||b->current.epoch!=epoch)return -116;
    uint32_t time[2]={0};
    if(confirmed){if(!square||!on)return -22;
        for(unsigned m=0;m<2;m++){if(on[m]>240000)return -22;time[m]=(uint32_t)(((uint64_t)on[m]*1000000+23999)/24000);}}
    int rc=elden_shared_budget_settle_actual(&b->budget,owner,epoch,run,b->io.now_us(b->io.context),confirmed,square,time);
    if(rc)fail(b,rc);return rc;
}
int elden_broker_actuator_release(EldenHapticsBroker *b,uint64_t owner,uint64_t epoch,int confirmed){
    if(!b||__atomic_load_n(&b->current_kind,__ATOMIC_ACQUIRE)!=3||b->current.owner!=owner||b->current.epoch!=epoch)return -116;
    if(!confirmed||b->held||elden_shared_budget_active(&b->budget))return fail(b,-125);
    lock(b);__atomic_store_n(&b->current_kind,0u,__ATOMIC_RELEASE);unlock(b);return 0;
}
