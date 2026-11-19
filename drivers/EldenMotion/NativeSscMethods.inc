/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native worker methods; host test supplies explicit OS/router/HID peers. */
namespace {
enum {NativeSlots=16,NativeOpening=1,NativeDiscovering=2,NativePublished=3,
      NativeStopping=4,NativeStopped=5,NativeRetained=6};
struct MotionContext;
struct MotionCallback {MotionContext *context;unsigned connection;};
struct MotionIncoming {EldenSscServer server;uint32_t connection,size;uint8_t data[EldenSscMaxPacket];};
struct MotionContext {
    EldenMotionTransport transport;EldenMotionBoard board;EldenSsc manager;
    IOSimpleLock *lock;thread_t worker;void *handles[3];uint8_t port_closed[3];MotionCallback callbacks[3];
    MotionIncoming inbox[NativeSlots];unsigned head,used;
    EldenSscServer published_server;EldenSscAttrs attrs[2];EldenMotionSpec specs[2];
    uint32_t pending,stop_requested,fault,phase_ready,wanted[2],dirty,terminal;
    uint32_t hid_started,hid_bound,packets,dropped,stale;int terminal_error;
    EldenMotionNativeStatus status;
};
static MotionContext *motion_context;static uint32_t motion_attempted;
static void signal(MotionContext *c,uint32_t event){__atomic_fetch_or(&c->pending,event,__ATOMIC_RELEASE);thread_wakeup((event_t)&c->pending);}
static void fault(MotionContext *c,int error){
    __atomic_store_n(&c->phase_ready,0u,__ATOMIC_RELEASE);uint32_t none=0;
    __atomic_compare_exchange_n(&c->fault,&none,(uint32_t)(error?error:EldenMotionPeer),false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    signal(c,4);
}
static void bump(uint32_t *n){uint32_t before=__atomic_load_n(n,__ATOMIC_RELAXED);while(before!=UINT32_MAX&&!__atomic_compare_exchange_n(n,&before,before+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){} }
static bool identity(MotionContext *c,uint64_t owner,uint64_t epoch){return owner==c->transport.owner&&epoch==c->transport.epoch;}
static bool server_same(const EldenSscServer &a,const EldenSscServer &b){return a.service==b.service&&a.instance==b.instance&&a.node==b.node&&a.port==b.port&&a.generation==b.generation;}
static int physical(MotionContext *c,int cleanup){
    int rc=c->transport.admit(c->transport.context,c->transport.owner,c->transport.epoch,cleanup);if(rc)return rc<0?rc:EldenMotionPeer;
    rc=c->board.admit(c->board.context,c->transport.owner,c->transport.epoch,cleanup);return rc<=0?rc:EldenMotionPeer;
}
static void on_server(void *context,uint64_t owner,uint64_t epoch,const EldenSscServer *server,int up){
    auto *callback=(MotionCallback *)context;MotionContext *c=callback->context;
    if(!identity(c,owner,epoch)||!server){bump(&c->stale);return;}
    if(__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)){bump(&c->stale);return;}
    if(__atomic_load_n(&c->phase_ready,__ATOMIC_ACQUIRE)&&(!up||!server_same(c->published_server,*server)))fault(c,EldenMotionStale);
    signal(c,1); // worker reads actual provider level; no router work in callback
}
static void on_receive(void *context,uint64_t owner,uint64_t epoch,const EldenSscServer *server,const uint8_t *data,uint32_t size){
    auto *callback=(MotionCallback *)context;MotionContext *c=callback->context;
    if(!identity(c,owner,epoch)||!server){bump(&c->stale);return;}
    if(__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)){bump(&c->stale);return;}
    if(!data||!size||size>EldenSscMaxPacket){bump(&c->dropped);fault(c,EldenSscOverflow);return;}
    IOSimpleLockLock(c->lock);
    if(c->used==NativeSlots){IOSimpleLockUnlock(c->lock);bump(&c->dropped);fault(c,EldenSscOverflow);return;}
    MotionIncoming &m=c->inbox[(c->head+c->used)%NativeSlots];m.server=*server;m.connection=callback->connection;m.size=size;
    memcpy(m.data,data,size);c->used++;IOSimpleLockUnlock(c->lock);signal(c,2);
}
static bool take(MotionContext *c,MotionIncoming *out){
    IOSimpleLockLock(c->lock);bool available=c->used!=0;
    if(available){*out=c->inbox[c->head];c->head=(c->head+1)%NativeSlots;c->used--;}
    IOSimpleLockUnlock(c->lock);return available;
}
static uint32_t wait_events(MotionContext *c,uint32_t ms){
    uint32_t bits=__atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL);if(bits)return bits;
    assert_wait_timeout((event_t)&c->pending,THREAD_UNINT,ms?ms:1,kMillisecondScale);
    bits=__atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL);
    if(bits){clear_wait(current_thread(),THREAD_AWAKENED);return bits;}
    thread_block(THREAD_CONTINUE_NULL);return __atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL);
}
static int core_admit(void *context,uint64_t owner,uint64_t epoch,int cleanup){
    auto *c=(MotionContext *)context;return identity(c,owner,epoch)?physical(c,cleanup):EldenMotionStale;
}
static int core_send(void *context,uint64_t owner,uint64_t epoch,const EldenSscServer *server,unsigned connection,const uint8_t *data,uint32_t size){
    auto *c=(MotionContext *)context;if(!identity(c,owner,epoch)||connection>=3||!c->handles[connection])return EldenMotionStale;
    return c->transport.send_epoch(c->transport.context,c->handles[connection],owner,epoch,server,data,size);
}
static int core_withdraw(void *context,uint64_t owner,uint64_t epoch,const EldenSscServer *server,unsigned connection){
    auto *c=(MotionContext *)context;if(!identity(c,owner,epoch)||connection>=3)return EldenMotionStale;
    if(!c->handles[connection]||c->port_closed[connection])return 0;
    int rc=c->transport.withdraw(c->transport.context,c->handles[connection],owner,epoch,server);
    if(!rc)c->port_closed[connection]=1;return rc; // retain handle/context to reset
}
static int core_qualify(void *context,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *attrs,EldenMotionSpec *spec){
    auto *c=(MotionContext *)context;int rc=core_admit(c,owner,epoch,0);if(rc)return rc;
    return c->board.qualify(c->board.context,owner,epoch,kind,attrs,spec);
}
static int core_map(void *context,uint64_t owner,uint64_t epoch,uint64_t ssc,uint64_t *out){
    auto *c=(MotionContext *)context;int rc=core_admit(c,owner,epoch,0);if(rc)return rc;
    return c->board.map_time(c->board.context,owner,epoch,ssc,out);
}
static int hid_admit(void *context,uint64_t owner,uint64_t epoch,unsigned kind,int cleanup){
    auto *c=(MotionContext *)context;if(!identity(c,owner,epoch)||kind<1||kind>2)return EldenMotionStale;
    if(!cleanup&&(!__atomic_load_n(&c->phase_ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->fault,__ATOMIC_ACQUIRE)))return EldenSscAgain;
    return physical(c,cleanup);
}
static int hid_demand(void *context,uint64_t owner,uint64_t epoch,unsigned kind,uint32_t hz){
    auto *c=(MotionContext *)context;if(kind<1||kind>2||hz>400)return EldenMotionArgument;
    int rc=hid_admit(c,owner,epoch,kind,hz==0);if(rc)return rc;
    if(hz&&!elden_ssc_pick_rate(&c->attrs[kind-1],hz))return EldenMotionPeer;
    __atomic_store_n(&c->wanted[kind-1],hz,__ATOMIC_RELEASE);__atomic_fetch_or(&c->dirty,1u<<(kind-1),__ATOMIC_RELEASE);signal(c,8);return 0;
}
static int core_deliver(void *context,uint64_t owner,uint64_t epoch,const EldenMotionSample *sample){
    auto *c=(MotionContext *)context;int rc=hid_admit(c,owner,epoch,sample->kind,0);if(rc)return rc;
    if(!__atomic_load_n(&c->hid_bound,__ATOMIC_ACQUIRE))return EldenSscAgain;
    rc=elden_motion_native_hid_packet(sample);
    return rc==(int)kIOReturnSuccess?0:rc==(int)kIOReturnNotReady?int(EldenSscAgain):int(EldenMotionPeer);
}
static void snapshot(MotionContext *c,uint32_t stage,int error){
    EldenMotionNativeStatus s={};s.stage=stage;s.error=error;s.owner=c->transport.owner;s.epoch=c->transport.epoch;
    s.generation=c->manager.server.generation;s.runtime_error=(int)__atomic_load_n(&c->fault,__ATOMIC_ACQUIRE);
    s.hid_started=c->hid_started;s.hid_bound=__atomic_load_n(&c->hid_bound,__ATOMIC_ACQUIRE);
    s.packets=c->packets;s.dropped=__atomic_load_n(&c->dropped,__ATOMIC_RELAXED);s.stale=__atomic_load_n(&c->stale,__ATOMIC_RELAXED);
    s.samples=c->manager.samples;s.terminal=__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE);
    IOSimpleLockLock(c->lock);c->status=s;IOSimpleLockUnlock(c->lock);
}
static int update_server(MotionContext *c,uint64_t now){
    EldenSscServer current[3]={};int rc=physical(c,0);if(rc)return rc;
    for(unsigned i=0;i<3;i++){
        rc=c->transport.level(c->transport.context,c->handles[i],c->transport.owner,c->transport.epoch,&current[i]);
        if(rc!=1){if(rc<0)return rc;if(c->manager.server_up)return EldenMotionStale;return EldenSscAgain;}
    }
    if(!server_same(current[0],current[1])||!server_same(current[0],current[2]))return EldenMotionStale;
    return elden_ssc_server(&c->manager,c->transport.owner,c->transport.epoch,&current[0],1,now);
}
static int publish(MotionContext *c){
    if(c->hid_started||c->manager.state!=EldenSscReady)return 0;
    c->published_server=c->manager.server;EldenMotionNativeProducer producer={};producer.context=c;producer.owner=c->transport.owner;producer.epoch=c->transport.epoch;
    producer.admit=hid_admit;producer.demand=hid_demand;producer.max_age_us=c->board.max_age_us;producer.max_future_us=c->board.max_future_us;
    for(unsigned i=0;i<2;i++){
        c->attrs[i]=c->manager.sensor[i].attrs;c->specs[i]=c->manager.sensor[i].spec;producer.suid[i]=c->specs[i].suid;
        uint64_t bound=(uint32_t)c->specs[i].max_abs_q16;
        if(c->specs[i].unit==EldenMotionMetersPerSecond2)bound=(bound*100000+980664)/980665;
        producer.max_abs_q16[i]=(uint32_t)bound;producer.negate_axes[i]=c->board.ios_negate_axes[i];
    }
    __atomic_store_n(&c->phase_ready,1u,__ATOMIC_RELEASE);c->hid_started=1;
    int rc=elden_motion_native_hid_start(&producer);if(rc){fault(c,rc);return rc;}return 0;
}
static int close_all(MotionContext *c){
    int rc=physical(c,1);if(rc)return rc;
    for(unsigned i=0;i<3;i++)if(c->handles[i]&&!c->port_closed[i]){
        rc=core_withdraw(c,c->transport.owner,c->transport.epoch,&c->manager.server,i);if(rc)return rc;
    }
    return 0;
}
static void run_motion(void *argument,wait_result_t){
    auto *c=(MotionContext *)argument;__atomic_store_n(&c->worker,current_thread(),__ATOMIC_RELEASE);
    EldenSscOps ops={c,c->transport.owner,c->transport.epoch,core_admit,core_send,core_withdraw,core_qualify,core_map,core_deliver};
    int error=elden_ssc_init(&c->manager,&ops);static const EldenMotionTransportClient client={on_server,on_receive};
    for(unsigned i=0;!error&&i<3;i++){
        if(__atomic_load_n(&c->fault,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE)){error=EldenSscClosed;break;}
        error=physical(c,0);if(error)break;
        c->callbacks[i]={c,i};c->handles[i]=c->transport.open(c->transport.context,c->transport.owner,c->transport.epoch,EldenSscService,i,&client,&c->callbacks[i]);
        if(!c->handles[i])error=EldenSscAbsent;
    }
    if(error)fault(c,error);snapshot(c,error?NativeRetained:NativeDiscovering,error);
    bool stopping=false;uint64_t stop_started=0;
    for(;;){
        uint64_t now=mach_absolute_time();uint32_t failure=__atomic_load_n(&c->fault,__ATOMIC_ACQUIRE);
        if(!stopping&&(failure||__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE))){
            stopping=true;stop_started=now;__atomic_store_n(&c->phase_ready,0u,__ATOMIC_RELEASE);if(failure)error=(int)failure;
            if(c->hid_started){int rc=elden_motion_native_hid_stop(c->transport.owner,c->transport.epoch);if(rc&&!error)error=rc;}
            snapshot(c,NativeStopping,error);
        }
        if(stopping){
            MotionIncoming message;
            for(unsigned processed=0;processed<NativeSlots&&take(c,&message);processed++){
                bump(&c->packets);
                int received=elden_ssc_receive(&c->manager,c->transport.owner,c->transport.epoch,&message.server,message.connection,message.data,message.size,mach_absolute_time());
                if(received<0&&received!=EldenSscAgain&&received!=EldenMotionStale&&!error)error=received;
            }
            int rc=c->manager.initialized?elden_ssc_stop(&c->manager,c->transport.owner,c->transport.epoch,now):0;
            if(!rc){rc=close_all(c);if(!rc){error=0;break;}}
            if(rc!=EldenSscAgain){if(!error)error=rc;if(c->manager.state!=EldenSscHeld)fault(c,rc);}
            if(!now||now<stop_started||now-stop_started>=UINT64_C(105600000)){if(!error)error=EldenMotionClock;break;}
            wait_events(c,100);continue;
        }
        int rc=update_server(c,now);if(rc<0&&rc!=EldenSscAgain){fault(c,rc);continue;}
        MotionIncoming message;
        for(unsigned processed=0;processed<NativeSlots&&take(c,&message);processed++){
            bump(&c->packets);rc=elden_ssc_receive(&c->manager,c->transport.owner,c->transport.epoch,&message.server,message.connection,message.data,message.size,mach_absolute_time());
            if(rc<0&&rc!=EldenSscAgain&&rc!=EldenMotionStale){fault(c,rc);break;}
            if(rc==EldenMotionStale)bump(&c->stale);
        }
        if(__atomic_load_n(&c->fault,__ATOMIC_ACQUIRE))continue;
        rc=publish(c);if(rc)continue;
        if(c->hid_started&&!__atomic_load_n(&c->hid_bound,__ATOMIC_ACQUIRE)){
            rc=elden_motion_native_hid_bind(c->transport.owner,c->transport.epoch);
            if(rc<0){fault(c,rc);continue;}if(rc==1)__atomic_store_n(&c->hid_bound,1u,__ATOMIC_RELEASE);
        }
        // Bind both event-driver hooks before sending any demand/config. A
        // first report cannot be required to install the hook that starts it.
        uint32_t dirty=__atomic_load_n(&c->hid_bound,__ATOMIC_ACQUIRE)?__atomic_exchange_n(&c->dirty,0u,__ATOMIC_ACQ_REL):0;
        for(unsigned i=0;i<2;i++)if(dirty&(1u<<i)){
            rc=elden_ssc_demand(&c->manager,c->transport.owner,c->transport.epoch,i+1,__atomic_load_n(&c->wanted[i],__ATOMIC_ACQUIRE));
            if(rc){fault(c,rc);break;}
        }
        rc=elden_ssc_step(&c->manager,mach_absolute_time());if(rc<0&&rc!=EldenSscAgain){fault(c,rc);continue;}
        snapshot(c,c->hid_started?NativePublished:NativeDiscovering,0);
        uint32_t ms=elden_ssc_idle_ms(&c->manager,mach_absolute_time());if(c->hid_started&&!__atomic_load_n(&c->hid_bound,__ATOMIC_ACQUIRE)&&ms>100)ms=100;
        wait_events(c,ms);
    }
    if(!error&&c->hid_started){int rc=elden_motion_native_hid_stop(c->transport.owner,c->transport.epoch);if(rc)error=rc;}
    if(!error){
        // Close has supplied the real callback/send fence. Discard only this
        // retained client's obsolete inbox, never another router's queue.
        IOSimpleLockLock(c->lock);c->used=0;c->head=0;IOSimpleLockUnlock(c->lock);
    }
    c->terminal_error=error;__atomic_store_n(&c->phase_ready,0u,__ATOMIC_RELEASE);__atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);
    snapshot(c,error?NativeRetained:NativeStopped,error);signal(c,16);thread_terminate(current_thread());
    // Callback contexts, port leases and native HID objects remain to reset.
}
}
extern "C" int elden_motion_native_ssc_start(const EldenMotionTransport *transport,const EldenMotionBoard *board){
    if(!transport||!board||!transport->owner||!transport->epoch||transport->max_rx_payload<EldenSscMaxPacket||
       !transport->admit||!transport->open||!transport->level||!transport->send_epoch||!transport->withdraw||
       !board->admit||!board->qualify||!board->map_time||!board->max_age_us||board->max_age_us>1000000||board->max_future_us>50000||
       board->ios_negate_axes[0]>7||board->ios_negate_axes[1]>7||ml_at_interrupt_context())return EldenMotionArgument;
    int rc=transport->admit(transport->context,transport->owner,transport->epoch,0);if(rc)return rc;
    rc=board->admit(board->context,transport->owner,transport->epoch,0);if(rc)return rc;
    mach_timebase_info_data_t timebase;clock_timebase_info(&timebase);
    if(!timebase.numer||!timebase.denom||(uint64_t)timebase.numer*19200000!=UINT64_C(1000000000)*timebase.denom)return -95;
    uint32_t none=0;if(!__atomic_compare_exchange_n(&motion_attempted,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return EldenSscBusy;
    auto *c=(MotionContext *)IOMallocData(sizeof(MotionContext));if(!c)return EldenSscAbsent;
    bzero(c,sizeof(*c));c->transport=*transport;c->board=*board;c->lock=IOSimpleLockAlloc();
    if(!c->lock){IOFreeData(c,sizeof(*c));return EldenSscAbsent;}
    __atomic_store_n(&motion_context,c,__ATOMIC_RELEASE);snapshot(c,NativeOpening,0);
    thread_t thread=nullptr;rc=kernel_thread_start(run_motion,c,&thread);
    if(rc!=KERN_SUCCESS){c->terminal_error=EldenSscAbsent;__atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);snapshot(c,NativeRetained,EldenSscAbsent);return EldenSscAbsent;}
    thread_deallocate(thread);return 0;
}
extern "C" int elden_motion_native_ssc_request_stop(uint64_t owner,uint64_t epoch){
    auto *c=__atomic_load_n(&motion_context,__ATOMIC_ACQUIRE);if(!c)return EldenSscAbsent;
    if(!identity(c,owner,epoch))return EldenMotionStale;__atomic_store_n(&c->phase_ready,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&c->stop_requested,1u,__ATOMIC_RELEASE);signal(c,4);return 0;
}
extern "C" int elden_motion_native_ssc_stop(uint64_t owner,uint64_t epoch){
    auto *c=__atomic_load_n(&motion_context,__ATOMIC_ACQUIRE);if(!c)return 0;
    if(!identity(c,owner,epoch))return EldenMotionStale;if(ml_at_interrupt_context()||current_thread()==__atomic_load_n(&c->worker,__ATOMIC_ACQUIRE))return EldenSscBusy;
    int rc=elden_motion_native_ssc_request_stop(owner,epoch);if(rc)return rc;
    for(unsigned i=0;i<550&&!__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE);i++)IOSleep(10);
    return __atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)?c->terminal_error:EldenSscAgain;
}
extern "C" int elden_motion_native_ssc_status(EldenMotionNativeStatus *out){
    if(!out)return EldenMotionArgument;auto *c=__atomic_load_n(&motion_context,__ATOMIC_ACQUIRE);if(!c)return EldenSscAbsent;
    IOSimpleLockLock(c->lock);*out=c->status;IOSimpleLockUnlock(c->lock);return 0;
}
extern "C" int elden_motion_native_ssc_recovery_stop(void){
    auto *c=__atomic_load_n(&motion_context,__ATOMIC_ACQUIRE);return !c||elden_motion_native_ssc_stop(c->transport.owner,c->transport.epoch)==0;
}
