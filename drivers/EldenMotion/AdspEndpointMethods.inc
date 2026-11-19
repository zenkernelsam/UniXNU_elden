/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Concrete epoch-safe real GLINK endpoint for original sensor RPC worker. */
struct EldenSensorAdspEndpoint {
    EldenSensorAdspAuthority authority;ShengAdspChannel *channel;EldenSensorMemoryBinding *binding;
    IOSimpleLock *lock;uint32_t pending_events,channel_epoch,connected,has_connection,closing,terminal,error;
    uint64_t pending_cookie,last_cookie,requests,completed;uint32_t receive_count,stale,malformed,reply_ready,withdrawing;
};
namespace {
static bool endpoint_same(EldenSensorAdspEndpoint *c,uint64_t owner,uint64_t epoch){return c&&c->authority.owner==owner&&c->authority.epoch==epoch;}
static void endpoint_signal(EldenSensorAdspEndpoint *c){__atomic_store_n(&c->pending_events,1u,__ATOMIC_RELEASE);thread_wakeup((event_t)&c->pending_events);}
static int endpoint_error(EldenSensorAdspEndpoint *c,int error){uint32_t none=0;
    __atomic_compare_exchange_n(&c->error,&none,(uint32_t)(error<0?error:-13),false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);endpoint_signal(c);return (int)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
}
static void endpoint_state(void *p,uint32_t epoch,int up){
    auto *c=(EldenSensorAdspEndpoint *)p;
    if(__atomic_load_n(&c->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return;
    IOSimpleLockLock(c->lock);bool stale=c->has_connection&&(epoch!=c->channel_epoch||!up);
    if(!stale)c->channel_epoch=epoch;c->connected=!stale&&up!=0;
    if(up)c->has_connection=1;auto *binding=__atomic_load_n(&c->binding,__ATOMIC_ACQUIRE);
    // The exact MemoryBinding->RPC channel route only updates atomic latches.
    // Serialize it with attach so a concurrent up cannot be overwritten by
    // attach's older down snapshot. No wait or physical operation under lock.
    int rc=binding?elden_sensor_memory_binding_channel(binding,c->authority.owner,c->authority.epoch,stale?0:up):0;
    IOSimpleLockUnlock(c->lock);
    if(stale)endpoint_error(c,-116);
    if(rc&&rc!=-125)endpoint_error(c,rc);
    endpoint_signal(c);
}
static uint64_t endpoint_u64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void endpoint_receive(void *p,uint32_t epoch,const uint8_t *data,uint32_t size){
    auto *c=(EldenSensorAdspEndpoint *)p;
    if(__atomic_load_n(&c->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return;
    if(!data||size<12||size>64){IOSimpleLockLock(c->lock);if(c->malformed!=UINT32_MAX)c->malformed++;IOSimpleLockUnlock(c->lock);endpoint_error(c,-84);return;}
    IOSimpleLockLock(c->lock);uint64_t cookie=endpoint_u64(data);bool accept=epoch==c->channel_epoch&&c->connected&&cookie&&cookie==c->pending_cookie;
    auto *binding=__atomic_load_n(&c->binding,__ATOMIC_ACQUIRE);
    if(!accept||!binding||c->reply_ready){if(c->stale!=UINT32_MAX)c->stale++;IOSimpleLockUnlock(c->lock);return;}
    if(c->receive_count==UINT32_MAX){IOSimpleLockUnlock(c->lock);endpoint_error(c,-75);return;}c->receive_count++;c->reply_ready=1;IOSimpleLockUnlock(c->lock);
    int rc=elden_sensor_memory_binding_receive(binding,c->authority.owner,c->authority.epoch,data,size);
    if(rc&&rc!=-125)endpoint_error(c,rc);endpoint_signal(c);
}
static int endpoint_admit(void *p,uint64_t owner,uint64_t epoch,int cleanup){
    auto *c=(EldenSensorAdspEndpoint *)p;if(!endpoint_same(c,owner,epoch))return -116;
    if(!cleanup&&(__atomic_load_n(&c->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)))return -125;
    int rc=c->authority.admit(c->authority.context,owner,epoch,cleanup);return rc<=0?rc:-13;
}
static uint64_t endpoint_now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000;}
static int endpoint_send(void *p,uint64_t owner,uint64_t epoch,const uint8_t *data,uint32_t size){
    auto *c=(EldenSensorAdspEndpoint *)p;int rc=endpoint_admit(c,owner,epoch,0);if(rc)return rc;
    if(!data||size!=40)return -22;IOSimpleLockLock(c->lock);uint32_t channel_epoch=c->channel_epoch;
    bool allowed=c->connected&&c->pending_cookie&&c->pending_cookie==endpoint_u64(data+8);IOSimpleLockUnlock(c->lock);if(!allowed)return -116;
    rc=sheng_adsp_send_epoch(c->channel,channel_epoch,data,size);
    if(rc)return endpoint_error(c,rc==-1?-11:rc==-3?-116:-22);return 0;
}
static void endpoint_wait(void *p,uint32_t ms){
    auto *c=(EldenSensorAdspEndpoint *)p;
    if(__atomic_exchange_n(&c->pending_events,0u,__ATOMIC_ACQ_REL))return;
    assert_wait_timeout((event_t)&c->pending_events,THREAD_UNINT,ms?ms:1,kMillisecondScale);
    if(__atomic_exchange_n(&c->pending_events,0u,__ATOMIC_ACQ_REL)){clear_wait(current_thread(),THREAD_AWAKENED);return;}
    thread_block(THREAD_CONTINUE_NULL);
}
static int endpoint_reserve(void *p,uint64_t owner,uint64_t epoch,uint64_t *out){
    auto *c=(EldenSensorAdspEndpoint *)p;int rc=endpoint_admit(c,owner,epoch,0);if(rc)return rc;
    if(!out)return -22;IOSimpleLockLock(c->lock);bool pending=c->pending_cookie!=0,connected=c->connected&&c->binding;IOSimpleLockUnlock(c->lock);
    if(pending)return -16;if(!connected)return -19;
    uint64_t cookie=0;rc=c->authority.reserve_cookie(c->authority.context,owner,epoch,&cookie);if(rc)return rc<0?rc:-13;
    IOSimpleLockLock(c->lock);
    // Opaque original-owner tokens need not be numerically ordered. Global
    // nonreuse is the shared authority's contract; reject a direct replay.
    if(!cookie||(cookie&15)!=2||cookie==c->last_cookie||c->pending_cookie||c->requests==UINT64_MAX){IOSimpleLockUnlock(c->lock);return endpoint_error(c,-75);}
    c->pending_cookie=cookie;c->last_cookie=cookie;c->reply_ready=0;c->requests++;*out=cookie;IOSimpleLockUnlock(c->lock);return 0;
}
static int endpoint_complete(void *p,uint64_t owner,uint64_t epoch,uint64_t cookie){
    auto *c=(EldenSensorAdspEndpoint *)p;int rc=endpoint_admit(c,owner,epoch,0);if(rc)return rc;
    IOSimpleLockLock(c->lock);bool correct=cookie&&c->pending_cookie==cookie&&c->reply_ready;IOSimpleLockUnlock(c->lock);if(!correct)return -116;
    rc=c->authority.complete_cookie(c->authority.context,owner,epoch,cookie);if(rc)return rc<0?rc:-13;
    IOSimpleLockLock(c->lock);
    if(c->pending_cookie!=cookie||c->completed==UINT64_MAX){IOSimpleLockUnlock(c->lock);return endpoint_error(c,-75);}
    c->pending_cookie=0;c->reply_ready=0;c->completed++;IOSimpleLockUnlock(c->lock);return 0;
}
static void *endpoint_alloc(void *,uint32_t bytes){return IOMallocData(bytes);}
static void endpoint_trace(void *p,const char *s){auto *c=(EldenSensorAdspEndpoint *)p;if(c->authority.trace)c->authority.trace(c->authority.context,s);}
static void endpoint_wake(void *p){endpoint_signal((EldenSensorAdspEndpoint *)p);}
static int endpoint_withdraw_serial(EldenSensorAdspEndpoint *c,uint64_t owner,uint64_t epoch){
    if(!c->lock||!c->channel)return (int)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)?(int)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE):-19;
    int rc=endpoint_admit(c,owner,epoch,1);if(rc)return rc;
    __atomic_store_n(&c->closing,1u,__ATOMIC_RELEASE);endpoint_signal(c);
    EldenAdspChannelStatus status={};rc=elden_adsp_channel_status(c->channel,&status);if(rc)return rc;
    // epoch advances on remote CLOSE; use the exact first close token again,
    // never incorrectly claim the post-close epoch is the original session.
    if(!status.closing){rc=elden_adsp_channel_close(c->channel,status.epoch);if(rc)return rc;return -11;}
    if(status.error)return status.error;if(!status.fenced||status.queued||status.pumping||status.callbacks)return -11;
    IOSimpleLockLock(c->lock);bool pending=c->pending_cookie!=0;uint32_t channel_epoch=c->channel_epoch;IOSimpleLockUnlock(c->lock);
    rc=c->authority.retire(c->authority.context,owner,epoch,channel_epoch,pending);if(rc)return rc<0?rc:-13;
    IOSimpleLockLock(c->lock);c->connected=0;IOSimpleLockUnlock(c->lock);
    __atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);return 0;
}
static int endpoint_withdraw(void *p,uint64_t owner,uint64_t epoch){
    auto *c=(EldenSensorAdspEndpoint *)p;if(!endpoint_same(c,owner,epoch))return -116;
    if(ml_at_interrupt_context())return -16;
    if(__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return 0;
    uint32_t idle=0;if(!__atomic_compare_exchange_n(&c->withdrawing,&idle,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -11;
    int rc=endpoint_withdraw_serial(c,owner,epoch);__atomic_store_n(&c->withdrawing,0u,__ATOMIC_RELEASE);return rc;
}
}
extern "C" int elden_sensor_adsp_endpoint_open(const EldenSensorAdspAuthority *authority,uint32_t size,uint32_t count,EldenSensorAdspEndpoint **out,EldenSensorEndpoint *endpoint){
    if(!authority||!out||*out||!endpoint||!authority->owner||!authority->epoch||!authority->admit||!authority->reserve_cookie||!authority->complete_cookie||!authority->retire||
       size<64||size>65536||!count||count>24||ml_at_interrupt_context())return -22;
    int rc=authority->admit(authority->context,authority->owner,authority->epoch,0);if(rc)return rc<0?rc:-13;
    auto *c=(EldenSensorAdspEndpoint *)IOMallocData(sizeof(EldenSensorAdspEndpoint));if(!c)return -12;bzero(c,sizeof(*c));c->authority=*authority;*out=c;
    c->lock=IOSimpleLockAlloc();if(!c->lock)return endpoint_error(c,-12);
    const EldenAdspEpochClient client={endpoint_state,endpoint_receive};
    c->channel=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",size,count,0,&client,c);if(!c->channel)return endpoint_error(c,-19);
    *endpoint={c,authority->owner,authority->epoch,endpoint_admit,endpoint_now,endpoint_send,endpoint_wait,endpoint_reserve,endpoint_complete,endpoint_alloc,endpoint_trace,endpoint_withdraw,endpoint_wake};return 0;
}
extern "C" int elden_sensor_adsp_endpoint_attach(EldenSensorAdspEndpoint *c,EldenSensorMemoryBinding *binding,uint64_t owner,uint64_t epoch){
    if(!endpoint_same(c,owner,epoch)||!binding||!c->lock)return -116;IOSimpleLockLock(c->lock);
    if(c->binding||__atomic_load_n(&c->closing,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)){IOSimpleLockUnlock(c->lock);return -16;}
    __atomic_store_n(&c->binding,binding,__ATOMIC_RELEASE);
    int rc=elden_sensor_memory_binding_channel(binding,owner,epoch,c->connected);IOSimpleLockUnlock(c->lock);
    if(rc)return endpoint_error(c,rc);endpoint_signal(c);return 0;
}
extern "C" int elden_sensor_adsp_endpoint_status(EldenSensorAdspEndpoint *c,EldenSensorAdspStatus *out){
    if(!c||!out||!c->lock)return -22;IOSimpleLockLock(c->lock);
    *out={c->connected,c->channel_epoch,__atomic_load_n(&c->closing,__ATOMIC_ACQUIRE),__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE),c->pending_cookie!=0,c->receive_count,c->stale,c->malformed,
           c->authority.owner,c->authority.epoch,c->requests,c->completed,(int)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)};
    IOSimpleLockUnlock(c->lock);return 0;
}
extern "C" int elden_sensor_adsp_endpoint_close(EldenSensorAdspEndpoint *c,uint64_t owner,uint64_t epoch){return endpoint_withdraw(c,owner,epoch);}
