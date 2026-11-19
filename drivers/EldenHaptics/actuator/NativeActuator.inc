/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native lifecycle, included by the same SE6 owning worker. No physical
 * access is added to the UI client; its actions are acknowledged by the worker.
 * Retain every old session/descriptor to reset. Never map a former client's ring
 * into a new owner or recycle unconfirmed physical resources.
 */
struct ActuatorSession {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(ActuatorSession));}
    Context *context=nullptr;ActuatorSession *next=nullptr;
    IOBufferMemoryDescriptor *memory=nullptr;
    EldenActuatorHistory *history=nullptr;size_t history_bytes=0;
    EldenActuatorRing ring={};EldenActuatorLive live={};
    uint64_t owner=0,epoch=0,next_run=1,budget_run=0;
    uint32_t ready=0,prepared=0,claimed=0,closing=0,desired=0,request_seq=0,ack_seq=0,closed=0,control_busy=0;
    uint32_t running=0; // worker-owned; public desired alone never authorizes GO
    int ack_result=0,prepare_result=0;
};
static uint64_t actuator_host(void *){return mach_absolute_time();}
static int actuator_admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){
    ActuatorSession *s=(ActuatorSession *)v;Context *c=s->context;
    if(s->owner!=owner||s->epoch!=epoch)return -116;
    int rc=platform_admit(c,cleanup);if(rc)return rc;
    if(!cleanup){
        if(__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||!thermal(c))return -125;
        return elden_broker_output_admit(&c->broker);
    }
    return 0;
}
static int actuator_reserve(void *v,uint64_t owner,uint64_t epoch,const uint64_t square[2],const uint32_t on[2]){
    ActuatorSession *s=(ActuatorSession *)v;
    if(s->budget_run||s->next_run==UINT64_MAX)return -16;
    uint64_t run=s->next_run;
    int rc=elden_broker_actuator_reserve(&s->context->broker,owner,epoch,run,s->context->actuator_profile->max_run_us+500000,square,on);
    if(!rc){s->budget_run=run;s->next_run++;}return rc;
}
static int actuator_finish(void *v,uint64_t owner,uint64_t epoch,int confirmed,const uint64_t square[2],const uint32_t on[2]){
    ActuatorSession *s=(ActuatorSession *)v;if(!s->budget_run)return -116;
    int rc=elden_broker_actuator_settle(&s->context->broker,owner,epoch,s->budget_run,confirmed,square,on);
    if(!rc)s->budget_run=0;return rc;
}
static void actuator_mode(Context *c){
    unsigned mode=0;PE_parse_boot_argn("elden-actuator",&mode,sizeof(mode));
    if(mode!=1)return;
    const EldenActuatorLimits *profile=elden_actuator_profile_get();if(!profile)return;
    mach_timebase_info_data_t tb;clock_timebase_info(&tb);
    if(!tb.numer||!tb.denom||(uint64_t)tb.numer*19200000!=UINT64_C(1000000000)*tb.denom){
        log("\nELDEN_ACTUATOR refused actual host clock/timebase\n");return;
    }
    c->actuator_profile=profile;__atomic_store_n(&c->actuator_enabled,1u,__ATOMIC_RELEASE);
}
static ActuatorSession *actuator_find(Context *c,uint64_t owner,uint64_t epoch){
    ActuatorSession *s=__atomic_load_n(&c->actuator,__ATOMIC_ACQUIRE);
    return s&&s->owner==owner&&s->epoch==epoch?s:nullptr;
}
static int actuator_open(Context *c,uint64_t *owner,uint64_t *epoch){
    if(!owner||!epoch)return -22;
    if(__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE)!=2||!__atomic_load_n(&c->actuator_enabled,__ATOMIC_ACQUIRE)||!c->actuator_profile||
       __atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))return -19;
    uint64_t who=0,generation=0;int rc=elden_broker_open(&c->broker,&who,&generation);if(rc)return rc;
    IOLockLock(c->lock);bool capacity=c->actuator_opens<8;
    if(capacity)c->actuator_opens++;IOLockUnlock(c->lock);
    if(!capacity){(void)elden_broker_close(&c->broker,who,generation);return -12;}
    ActuatorSession *s=new ActuatorSession;
    if(!s){(void)elden_broker_close(&c->broker,who,generation);return -12;}
    s->context=c;s->owner=who;s->epoch=generation;
    // Keep failed allocations as well; physical state is never inferred from
    // a missing descriptor. Each mapped session stays distinct until reset.
    IOLockLock(c->lock);s->next=c->retired_actuators;c->retired_actuators=s;IOLockUnlock(c->lock);
    s->memory=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
        (IOOptionBits)kIODirectionInOut|(IOOptionBits)kIOMemoryKernelUserShared|(IOOptionBits)kIOMemoryMapperNone,
        ELDEN_ACTUATOR_BYTES,4096);
    if(!s->memory||s->memory->prepare()!=kIOReturnSuccess){(void)elden_broker_close(&c->broker,who,generation);return -12;}
    void *bytes=elden_actuator_buffer_bytes(s->memory);
    s->history_bytes=(size_t)c->actuator_profile->window_frames*sizeof(EldenActuatorHistory);
    s->history=s->history_bytes?(EldenActuatorHistory *)IOMalloc(s->history_bytes):nullptr;
    if(!bytes||!s->history){(void)elden_broker_close(&c->broker,who,generation);return -12;}
    rc=elden_actuator_ring_init(&s->ring,bytes,ELDEN_ACTUATOR_BYTES,mach_absolute_time(),who,generation);
    if(rc){(void)elden_broker_close(&c->broker,who,generation);return rc;}
    // Publish metadata only. Claim, chip-state reads and live initialization
    // belong to the original worker, never race it on the user-client thread.
    // Phase3 enters under this same lock, so late allocation cannot appear
    // after final shutdown has already sampled its active session.
    IOLockLock(c->lock);
    bool active_phase=__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE)==2&&!__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE);
    if(active_phase)__atomic_store_n(&c->actuator,s,__ATOMIC_RELEASE);
    IOLockUnlock(c->lock);
    if(!active_phase){(void)elden_broker_close(&c->broker,who,generation);return -125;}
    for(unsigned i=0;i<500;i++){
        uint32_t state=__atomic_load_n(&s->prepared,__ATOMIC_ACQUIRE);
        if(state){rc=__atomic_load_n(&s->prepare_result,__ATOMIC_ACQUIRE);
            if(!rc&&!__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)){
                *owner=who;*epoch=generation;return 0;
            }
            return rc?rc:-125;
        }
        if(__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))break;IOSleep(1);
    }
    __atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);(void)elden_broker_close(&c->broker,who,generation);return -110;
}
static int actuator_close(Context *c,uint64_t owner,uint64_t epoch){
    ActuatorSession *s=actuator_find(c,owner,epoch);if(!s)return -116;
    // Invalidate the producer immediately; only the owning worker stops the
    // physical motor and releases its broker claim afterward.
    __atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);
    IOLockLock(c->lock);__atomic_store_n(&s->desired,0u,__ATOMIC_RELEASE);
    uint32_t sequence=__atomic_load_n(&s->request_seq,__ATOMIC_RELAXED);
    if(sequence!=UINT32_MAX)__atomic_store_n(&s->request_seq,sequence+1,__ATOMIC_RELEASE);
    IOLockUnlock(c->lock);return elden_broker_close(&c->broker,owner,epoch);
}
static int actuator_request(Context *c,uint64_t owner,uint64_t epoch,int start){
    if(start!=0&&start!=1)return -22;ActuatorSession *s=actuator_find(c,owner,epoch);
    if(!s||!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||ml_at_interrupt_context())return -125;
    IOLockLock(c->lock);uint32_t sequence=__atomic_load_n(&s->request_seq,__ATOMIC_RELAXED);
    if(s->control_busy||sequence!=__atomic_load_n(&s->ack_seq,__ATOMIC_ACQUIRE)||sequence==UINT32_MAX){IOLockUnlock(c->lock);return -16;}
    if(__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)){IOLockUnlock(c->lock);return -125;}
    s->control_busy=1;
    sequence++;__atomic_store_n(&s->desired,(uint32_t)start,__ATOMIC_RELEASE);__atomic_store_n(&s->request_seq,sequence,__ATOMIC_RELEASE);IOLockUnlock(c->lock);
    // A bounded worker acknowledgment proves new generation/STOP, not a
    // future waveform outcome. Timeout also withdraws requested playback.
    for(unsigned i=0;i<500;i++){
        int result=0;bool done=false;
        if(__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)){result=-125;done=true;}
        else if(__atomic_load_n(&s->ack_seq,__ATOMIC_ACQUIRE)==sequence){result=__atomic_load_n(&s->ack_result,__ATOMIC_ACQUIRE);done=true;}
        if(done){IOLockLock(c->lock);s->control_busy=0;IOLockUnlock(c->lock);return result;}IOSleep(1);
    }
    __atomic_store_n(&s->desired,0u,__ATOMIC_RELEASE);__atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);
    IOLockLock(c->lock);s->control_busy=0;IOLockUnlock(c->lock);
    (void)elden_broker_close(&c->broker,owner,epoch);return -110;
}
static int actuator_info(Context *c,uint64_t owner,uint64_t epoch,uint64_t out[8]){
    if(!out)return -22;ActuatorSession *s=actuator_find(c,owner,epoch);
    if(!s||!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE))return -19;
    const uint64_t data[]={ELDEN_ACTUATOR_MAGIC,ELDEN_ACTUATOR_VERSION,ELDEN_ACTUATOR_BYTES,400,owner,epoch,
        __atomic_load_n(&s->ring.page->state,__ATOMIC_ACQUIRE),__atomic_load_n(&s->ring.page->generation,__ATOMIC_ACQUIRE)};
    for(unsigned i=0;i<8;i++)out[i]=data[i];return 0;
}
static IOMemoryDescriptor *actuator_memory(Context *c,uint64_t owner,uint64_t epoch){
    ActuatorSession *s=actuator_find(c,owner,epoch);
    if(!s||!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)||!s->memory)return nullptr;
    s->memory->retain();return s->memory;
}
static int actuator_round(Context *c){
    ActuatorSession *s=__atomic_load_n(&c->actuator,__ATOMIC_ACQUIRE);
    if(!s)return 0;
    if(__atomic_load_n(&s->closed,__ATOMIC_ACQUIRE))return __atomic_load_n(&s->ack_result,__ATOMIC_ACQUIRE);
    // Snapshot the command pair under the same lock as its publisher. The
    // worker keeps its own acknowledged running state; observing desired=1
    // before its sequence must never replay a previous generation's samples.
    IOLockLock(c->lock);
    bool closing=__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE);
    uint32_t sequence=__atomic_load_n(&s->request_seq,__ATOMIC_ACQUIRE);
    bool requested_start=__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)!=0;
    IOLockUnlock(c->lock);
    int rc=0;
    if(closing){
        s->running=0;
        rc=s->live.ring?elden_actuator_live_stop(&s->live):0;
        if(!rc&&s->claimed)rc=elden_broker_actuator_release(&c->broker,s->owner,s->epoch,1);
        __atomic_store_n(&s->ack_result,rc,__ATOMIC_RELEASE);__atomic_store_n(&s->ack_seq,sequence,__ATOMIC_RELEASE);
        __atomic_store_n(&s->ready,0u,__ATOMIC_RELEASE);__atomic_store_n(&s->closed,rc?2u:1u,__ATOMIC_RELEASE);
        if(!__atomic_load_n(&s->prepared,__ATOMIC_ACQUIRE)){
            __atomic_store_n(&s->prepare_result,rc?rc:-125,__ATOMIC_RELEASE);__atomic_store_n(&s->prepared,2u,__ATOMIC_RELEASE);
        }
        if(!rc){(void)elden_broker_close(&c->broker,s->owner,s->epoch);
            ActuatorSession *old=s;
            (void)__atomic_compare_exchange_n(&c->actuator,&old,(ActuatorSession *)nullptr,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);}
        return rc;
    }
    if(!__atomic_load_n(&s->prepared,__ATOMIC_ACQUIRE)){
        rc=elden_broker_actuator_claim(&c->broker,s->owner,s->epoch);
        if(!rc){s->claimed=1;EldenAwChip *chips[]={&c->chips[0],&c->chips[1]};
            EldenActuatorLiveIo io={s,actuator_admit,actuator_reserve,actuator_finish,actuator_host};
            rc=elden_actuator_live_init(&s->live,&s->ring,chips,c->actuator_profile,s->history,s->history_bytes,&io);
        }
        __atomic_store_n(&s->prepare_result,rc,__ATOMIC_RELEASE);
        if(!rc)__atomic_store_n(&s->ready,1u,__ATOMIC_RELEASE);
        else __atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);
        __atomic_store_n(&s->prepared,rc?2u:1u,__ATOMIC_RELEASE);
        return rc;
    }
    if(sequence!=__atomic_load_n(&s->ack_seq,__ATOMIC_ACQUIRE)){
        bool start=requested_start;
        rc=start?(s->running?0:elden_actuator_live_new_run(&s->live,mach_absolute_time())):elden_actuator_live_stop(&s->live);
        // Duplicate StartIO is idempotent. Other failed transitions must not
        // abandon a still-playing motor when running is withdrawn.
        if(rc&&start){int stopped=elden_actuator_live_stop(&s->live);if(stopped)rc=stopped;
            __atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);}
        s->running=!rc&&start?1u:0u;
        if(!rc&&start)__atomic_store_n(&s->ring.page->state,EldenActuatorReady|EldenActuatorRequested,__ATOMIC_RELEASE);
        __atomic_store_n(&s->ack_result,rc,__ATOMIC_RELEASE);__atomic_store_n(&s->ack_seq,sequence,__ATOMIC_RELEASE);
        if(rc)__atomic_store_n(&s->desired,0u,__ATOMIC_RELEASE);
    }
    if(!rc&&s->running&&!__atomic_load_n(&s->closing,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))rc=elden_actuator_live_step(&s->live,1);
    if(rc){
        s->running=0;
        __atomic_store_n(&s->desired,0u,__ATOMIC_RELEASE);
        if(s->live.state==EldenActuatorTerminal||s->live.state==EldenActuatorUncertain)__atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);
    }
    return rc;
}
static int actuator_shutdown(Context *c){
    ActuatorSession *s=__atomic_load_n(&c->actuator,__ATOMIC_ACQUIRE);if(!s)return 0;
    __atomic_store_n(&s->closing,1u,__ATOMIC_RELEASE);return actuator_round(c);
}
