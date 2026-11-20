/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native early-memory/listener/SSC execution, host tests supply OS and
 * physical authorities. No claimed sensor sample or automatic startup. */
namespace {
struct SensorRuntimeContext {
    EldenSensorRuntimeConfig config;EldenSensorNativeMemory *memory;
    EldenSensorAdspEndpoint *endpoint;EldenSensorEndpoint endpoint_ops;EldenSensorMemoryBinding *binding;
    IOSimpleLock *status_lock;thread_t worker;
    uint32_t pending,stopping,terminal,prepare_state,start_attempted,start_state,finishing,prepared,endpoint_ops_ready,binding_ready,
             listener_ready,listener_cancelled,motion_starting,motion_started,motion_stopped;
    uint64_t stop_at;int runtime_error,cleanup_error;EldenSensorRuntimeStatus status;
};
static SensorRuntimeContext *sensor_runtime;static uint32_t sensor_runtime_attempted;
constexpr uint64_t sensor_stop_ms=5500;
static uint64_t sensor_now_ms(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000;}
static bool sensor_same(SensorRuntimeContext *c,uint64_t owner,uint64_t epoch){return c&&owner==c->config.adsp.owner&&epoch==c->config.adsp.epoch;}
static void sensor_signal(SensorRuntimeContext *c){__atomic_store_n(&c->pending,1u,__ATOMIC_RELEASE);thread_wakeup((event_t)&c->pending);}
static void sensor_wait(SensorRuntimeContext *c,uint32_t ms){
    if(__atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL))return;
    assert_wait_timeout((event_t)&c->pending,THREAD_UNINT,ms,kMillisecondScale);
    if(__atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL)){clear_wait(current_thread(),THREAD_AWAKENED);return;}
    thread_block(THREAD_CONTINUE_NULL);
}
static void sensor_remember(SensorRuntimeContext *c,int error){
    int none=0;__atomic_compare_exchange_n(&c->runtime_error,&none,error<0?error:-13,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static void sensor_snapshot(SensorRuntimeContext *c,uint32_t stage){
    EldenSensorRuntimeStatus s={};s.stage=stage;s.owner=c->config.adsp.owner;s.epoch=c->config.adsp.epoch;
    s.prepared=__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE);s.worker_started=__atomic_load_n(&c->worker,__ATOMIC_ACQUIRE)!=nullptr;
    s.endpoint_open=c->endpoint!=nullptr;s.binding_open=c->binding!=nullptr;s.listener_ready=__atomic_load_n(&c->listener_ready,__ATOMIC_ACQUIRE);
    s.motion_started=__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE);s.motion_stopped=__atomic_load_n(&c->motion_stopped,__ATOMIC_ACQUIRE);
    s.stop_requested=__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE);s.terminal=__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE);
    s.runtime_error=__atomic_load_n(&c->runtime_error,__ATOMIC_ACQUIRE);s.cleanup_error=c->cleanup_error;
    if(c->binding)(void)elden_sensor_memory_binding_status(c->binding,&s.rpc,&s.pack);
    if(s.motion_started)(void)elden_motion_native_ssc_status(&s.motion);
    IOSimpleLockLock(c->status_lock);c->status=s;IOSimpleLockUnlock(c->status_lock);
}
static int sensor_board_admit(void *p,uint64_t owner,uint64_t epoch,int cleanup){
    auto *c=(SensorRuntimeContext *)p;if(!sensor_same(c,owner,epoch))return -116;
    if(!cleanup&&(!__atomic_load_n(&c->listener_ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)))return -125;
    int rc=c->config.adsp.admit(c->config.adsp.context,owner,epoch,cleanup);if(rc)return rc<0?rc:-13;
    if(c->memory){rc=elden_sensor_memory_native_admit(c->memory,owner,epoch,cleanup);if(rc)return rc;}
    rc=c->config.board.admit(c->config.board.context,owner,epoch,cleanup);return rc<=0?rc:-13;
}
static int sensor_board_qualify(void *p,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *attrs,EldenMotionSpec *out){
    auto *c=(SensorRuntimeContext *)p;int rc=sensor_board_admit(c,owner,epoch,0);if(rc)return rc;
    return c->config.board.qualify(c->config.board.context,owner,epoch,kind,attrs,out);
}
static int sensor_board_time(void *p,uint64_t owner,uint64_t epoch,uint64_t ssc,uint64_t *out){
    auto *c=(SensorRuntimeContext *)p;int rc=sensor_board_admit(c,owner,epoch,0);if(rc)return rc;
    return c->config.board.map_time(c->config.board.context,owner,epoch,ssc,out);
}
static void sensor_cancel_listener(SensorRuntimeContext *c){
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->listener_cancelled,&zero,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    if(__atomic_load_n(&c->binding_ready,__ATOMIC_ACQUIRE))(void)elden_sensor_memory_binding_request_stop(c->binding,c->config.adsp.owner,c->config.adsp.epoch);
    if(__atomic_load_n(&c->endpoint_ops_ready,__ATOMIC_ACQUIRE))c->endpoint_ops.wake(c->endpoint_ops.context);
}
static void sensor_stop_latch(SensorRuntimeContext *c){
    uint64_t at=sensor_now_ms(),zero=0;__atomic_compare_exchange_n(&c->stop_at,&zero,at?at:1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    __atomic_store_n(&c->stopping,1u,__ATOMIC_SEQ_CST);__atomic_store_n(&c->listener_ready,0u,__ATOMIC_RELEASE);
    if(__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE))(void)elden_motion_native_ssc_request_stop(c->config.adsp.owner,c->config.adsp.epoch);
    // Listener may still be needed to serve DSP cleanup while SSC disables
    // its streams. Keep it live until observed motion terminal or expiry.
    if(!__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&c->motion_starting,__ATOMIC_SEQ_CST))sensor_cancel_listener(c);
    sensor_signal(c);
}
static void sensor_motion_observer(void *p,const EldenMotionNativeStatus *status){
    auto *c=(SensorRuntimeContext *)p;
    if(!status||!sensor_same(c,status->owner,status->epoch))return;
    if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)&&status->terminal)sensor_cancel_listener(c);
    if(!__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)&&(status->runtime_error||status->terminal)){
        sensor_remember(c,status->runtime_error?status->runtime_error:-32);sensor_stop_latch(c);
    }
    IOSimpleLockLock(c->status_lock);c->status.motion=*status;
    c->status.runtime_error=__atomic_load_n(&c->runtime_error,__ATOMIC_ACQUIRE);
    c->status.stop_requested=__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE);IOSimpleLockUnlock(c->status_lock);
    sensor_signal(c);
}
static bool sensor_expired(SensorRuntimeContext *c){uint64_t now=sensor_now_ms(),at=__atomic_load_n(&c->stop_at,__ATOMIC_ACQUIRE);return !now||now<at||now-at>=sensor_stop_ms;}
static int sensor_finish(SensorRuntimeContext *c){
    const uint64_t owner=c->config.adsp.owner,epoch=c->config.adsp.epoch;
    if(__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&c->motion_stopped,__ATOMIC_ACQUIRE)){
        int rc=elden_motion_native_ssc_request_stop(owner,epoch);if(rc)return rc;
        EldenMotionNativeStatus status={};rc=elden_motion_native_ssc_status(&status);if(rc)return rc;
        if(!status.terminal)return -11;if(status.error)return status.error;
        __atomic_store_n(&c->motion_stopped,1u,__ATOMIC_RELEASE);
    }
    sensor_cancel_listener(c);
    sensor_snapshot(c,EldenSensorRuntimeClosingRpc);
    if(c->binding){int rc=elden_sensor_memory_binding_finish(c->binding,owner,epoch);if(rc)return rc;}
    else {
        if(c->endpoint){int rc=elden_sensor_adsp_endpoint_close(c->endpoint,owner,epoch);if(rc)return rc;}
        if(c->memory){int rc=elden_sensor_memory_native_stop(c->memory,owner,epoch);if(rc)return rc;}
    }
    __atomic_store_n(&c->prepared,0u,__ATOMIC_RELEASE);return 0;
}
static bool sensor_digest(const uint8_t *b){unsigned bits=0;for(unsigned i=0;i<32;i++)bits|=b[i];return bits!=0;}
static bool sensor_config(const EldenSensorRuntimeConfig *cfg){
    if(!cfg||!cfg->adsp.owner||!cfg->adsp.epoch||!cfg->adsp.admit||!cfg->adsp.reserve_cookie||!cfg->adsp.complete_cookie||!cfg->adsp.retire||
       cfg->memory_owner.owner!=cfg->adsp.owner||cfg->memory_owner.epoch!=cfg->adsp.epoch||!cfg->memory_owner.admit||
       !cfg->memory_owner.reserve||!cfg->memory_owner.install||!cfg->memory_owner.observe||!cfg->memory_owner.withdraw||
       cfg->qrtr.owner!=cfg->adsp.owner||cfg->qrtr.epoch!=cfg->adsp.epoch||!cfg->qrtr.lock||!cfg->qrtr.unlock||!cfg->qrtr.admit||!cfg->qrtr.retire||
       cfg->qrtr.rx_intent_size<32+EldenSscMaxPacket||cfg->qrtr.rx_intent_size>65536||!cfg->qrtr.rx_intent_count||cfg->qrtr.rx_intent_count>24||
       cfg->rpc_intent_size<64||cfg->rpc_intent_size>65536||!cfg->rpc_intent_count||cfg->rpc_intent_count>24||
       cfg->pack.owner!=cfg->adsp.owner||cfg->pack.epoch!=cfg->adsp.epoch||!cfg->pack.admit||
       !cfg->board.admit||!cfg->board.qualify||!cfg->board.map_time||!cfg->board.max_age_us||cfg->board.max_age_us>1000000||
       cfg->board.max_future_us>50000||cfg->board.ios_negate_axes[0]>7||cfg->board.ios_negate_axes[1]>7||
       cfg->seal.version!=1||cfg->seal.bytes<32||cfg->seal.bytes>(64u<<20))return false;
    if(!sensor_digest(cfg->seal.sha256)||!sensor_digest(cfg->seal.namespace_sha256)||!sensor_digest(cfg->seal.identity.device_sha256)||
       !sensor_digest(cfg->seal.identity.build_sha256)||!sensor_digest(cfg->seal.identity.adsp_sha256)||
       !sensor_digest(cfg->seal.identity.adsp_dtb_sha256)||!sensor_digest(cfg->seal.identity.registry_sha256))return false;
    return true;
}
}
extern "C" void elden_sensor_runtime_worker(void *argument,wait_result_t){
    auto *c=(SensorRuntimeContext *)argument;__atomic_store_n(&c->worker,current_thread(),__ATOMIC_RELEASE);
    const uint64_t owner=c->config.adsp.owner,epoch=c->config.adsp.epoch;int error=0;
    if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))error=-125;
    EldenSensorAdspEndpoint *endpoint=nullptr;EldenSensorEndpoint ops={};EldenSensorMemoryBinding *binding=nullptr;
    if(!error)error=elden_sensor_adsp_endpoint_open(&c->config.adsp,c->config.rpc_intent_size,c->config.rpc_intent_count,&endpoint,&ops);
    c->endpoint=endpoint;
    if(!error){c->endpoint_ops=ops;__atomic_store_n(&c->endpoint_ops_ready,1u,__ATOMIC_RELEASE);
        if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))error=-125;}
    if(!error){
        error=elden_sensor_memory_binding_open_prepared(c->memory,&ops,&c->config.seal,&c->config.pack,&binding);}
    c->binding=binding;
    if(!error){__atomic_store_n(&c->binding_ready,1u,__ATOMIC_RELEASE);error=elden_sensor_adsp_endpoint_attach(endpoint,binding,owner,epoch);}
    if(error){if(error!=-125||!__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))sensor_remember(c,error);sensor_stop_latch(c);}
    sensor_snapshot(c,EldenSensorRuntimeListener);
    for(;;){
        if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)){
            if(__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&c->listener_cancelled,__ATOMIC_ACQUIRE)){
                EldenMotionNativeStatus motion={};int rc=elden_motion_native_ssc_status(&motion);
                if(!rc&&motion.terminal)sensor_cancel_listener(c);
                else if(sensor_expired(c)||__atomic_load_n(&c->runtime_error,__ATOMIC_ACQUIRE))sensor_cancel_listener(c);
                else { (void)elden_sensor_memory_binding_step(c->binding);continue; }
            }
            sensor_snapshot(c,EldenSensorRuntimeStoppingMotion);int rc=sensor_finish(c);
            if(!rc){c->cleanup_error=0;break;}
            if(rc!=-11||sensor_expired(c)){c->cleanup_error=rc==-11?-110:rc;break;}
            sensor_wait(c,10);continue;
        }
        int rc=elden_sensor_memory_binding_step(c->binding);EldenSensorRpcStatus rpc={};EldenSensorPackStatus pack={};
        int status=elden_sensor_memory_binding_status(c->binding,&rpc,&pack);
        if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))continue;
        if(status||rpc.error||rpc.stage<0||rc<=0){sensor_remember(c,status?status:rpc.error?rpc.error:rc<0?rc:!rc?-32:-13);sensor_stop_latch(c);continue;}
        if(!__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE)&&rpc.stage>=6&&pack.state==2&&!pack.stopping){
            __atomic_store_n(&c->listener_ready,1u,__ATOMIC_RELEASE);EldenMotionTransport transport={};
            error=elden_motion_qrtr_transport(&transport);
            if(!error&&(transport.owner!=owner||transport.epoch!=epoch))error=-116;
            EldenMotionBoard board=c->config.board;board.context=c;board.admit=sensor_board_admit;board.qualify=sensor_board_qualify;board.map_time=sensor_board_time;
            if(!error){
                /* Paired seq-cst markers: either stop sees this active startup
                 * and preserves listener cleanup, or startup sees stop and
                 * never calls into SSC after listener cancellation. */
                __atomic_store_n(&c->motion_starting,1u,__ATOMIC_SEQ_CST);
                if(!__atomic_load_n(&c->stopping,__ATOMIC_SEQ_CST)){
                    error=elden_motion_native_ssc_start(&transport,&board);
                    if(!error)__atomic_store_n(&c->motion_started,1u,__ATOMIC_RELEASE);
                }
                __atomic_store_n(&c->motion_starting,0u,__ATOMIC_SEQ_CST);
            }
            if(error){sensor_remember(c,error);sensor_stop_latch(c);continue;}
            if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)){sensor_stop_latch(c);continue;}
        }
        if(__atomic_load_n(&c->motion_started,__ATOMIC_ACQUIRE)){
            EldenMotionNativeStatus motion={};error=elden_motion_native_ssc_status(&motion);
            if(error||motion.runtime_error||motion.terminal){sensor_remember(c,error?error:motion.runtime_error?motion.runtime_error:-32);sensor_stop_latch(c);continue;}
        }
        sensor_snapshot(c,c->motion_started?EldenSensorRuntimeSampling:EldenSensorRuntimeListener);
    }
    __atomic_store_n(&c->listener_ready,0u,__ATOMIC_RELEASE);__atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);
    sensor_snapshot(c,c->cleanup_error?EldenSensorRuntimeRetained:EldenSensorRuntimeStopped);sensor_signal(c);thread_terminate(current_thread());
}
static inline __attribute__((always_inline)) int sensor_runtime_prepare(const EldenSensorRuntimeConfig *cfg,EldenSensorNativeMemory *memory){
    if(!sensor_config(cfg)||ml_at_interrupt_context())return -22;
    int rc=cfg->adsp.admit(cfg->adsp.context,cfg->adsp.owner,cfg->adsp.epoch,0);if(rc)return rc<0?rc:-13;
    if(memory){
        EldenSensorMemoryView view={};rc=elden_sensor_memory_native_view(memory,cfg->adsp.owner,cfg->adsp.epoch,&view);if(rc)return rc;
        if(view.memory.iova!=cfg->memory.iova||view.memory.bytes!=cfg->memory.bytes||view.memory.session!=cfg->memory.session||view.memory.process!=cfg->memory.process)return -116;
        rc=elden_sensor_memory_native_admit(memory,cfg->adsp.owner,cfg->adsp.epoch,0);if(rc)return rc;
    }
    uint32_t none=0;if(!__atomic_compare_exchange_n(&sensor_runtime_attempted,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    auto *c=(SensorRuntimeContext *)IOMallocData(sizeof(SensorRuntimeContext));if(!c)return -12;bzero(c,sizeof(*c));c->config=*cfg;
    c->status_lock=IOSimpleLockAlloc();if(!c->status_lock){IOFreeData(c,sizeof(*c));return -12;}
    __atomic_store_n(&c->prepare_state,1u,__ATOMIC_RELEASE);
    __atomic_store_n(&sensor_runtime,c,__ATOMIC_RELEASE);
    if(memory)c->memory=memory;
    else rc=elden_sensor_memory_native_open(&cfg->memory,&cfg->memory_owner,&c->memory);
    if(!rc&&__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))rc=-125;
    if(!rc)rc=elden_qrtr_ports_prepare(&cfg->qrtr);
    if(!rc)rc=elden_motion_native_ssc_observe(c,cfg->adsp.owner,cfg->adsp.epoch,sensor_motion_observer);
    if(rc){sensor_remember(c,rc);sensor_snapshot(c,EldenSensorRuntimeRetained);__atomic_store_n(&c->prepare_state,3u,__ATOMIC_RELEASE);return rc;}
    __atomic_store_n(&c->prepared,1u,__ATOMIC_RELEASE);sensor_snapshot(c,EldenSensorRuntimePrepared);__atomic_store_n(&c->prepare_state,2u,__ATOMIC_RELEASE);return 0;
}
extern "C" int elden_sensor_runtime_prepare(const EldenSensorRuntimeConfig *cfg){return sensor_runtime_prepare(cfg,nullptr);}
extern "C" int elden_sensor_runtime_adopt(const EldenSensorRuntimeConfig *cfg,EldenSensorNativeMemory *memory){if(!memory)return -22;return sensor_runtime_prepare(cfg,memory);}
extern "C" int elden_sensor_runtime_start(uint64_t owner,uint64_t epoch){
    auto *c=__atomic_load_n(&sensor_runtime,__ATOMIC_ACQUIRE);if(!sensor_same(c,owner,epoch))return -116;
    if(ml_at_interrupt_context()||__atomic_load_n(&c->prepare_state,__ATOMIC_ACQUIRE)!=2||!__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -16;
    uint32_t none=0;if(!__atomic_compare_exchange_n(&c->start_state,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    __atomic_store_n(&c->start_attempted,1u,__ATOMIC_RELEASE);sensor_snapshot(c,EldenSensorRuntimeOpening);
    thread_t thread=nullptr;int rc=kernel_thread_start(elden_sensor_runtime_worker,c,&thread);
    if(rc!=KERN_SUCCESS){sensor_remember(c,-12);c->cleanup_error=-12;sensor_snapshot(c,EldenSensorRuntimeRetained);__atomic_store_n(&c->start_state,3u,__ATOMIC_RELEASE);return -12;}
    __atomic_store_n(&c->start_state,2u,__ATOMIC_RELEASE);thread_deallocate(thread);return 0;
}
extern "C" int elden_sensor_runtime_request_stop(uint64_t owner,uint64_t epoch){
    auto *c=__atomic_load_n(&sensor_runtime,__ATOMIC_ACQUIRE);if(!c)return 0;if(!sensor_same(c,owner,epoch))return -116;
    sensor_stop_latch(c);return 0;
}
extern "C" int elden_sensor_runtime_stop(uint64_t owner,uint64_t epoch){
    auto *c=__atomic_load_n(&sensor_runtime,__ATOMIC_ACQUIRE);if(!c)return 0;if(!sensor_same(c,owner,epoch))return -116;
    if(ml_at_interrupt_context()||current_thread()==__atomic_load_n(&c->worker,__ATOMIC_ACQUIRE))return -16;
    sensor_stop_latch(c);
    for(unsigned i=0;i<550&&__atomic_load_n(&c->prepare_state,__ATOMIC_ACQUIRE)==1&&!sensor_expired(c);i++)IOSleep(10);
    if(__atomic_load_n(&c->prepare_state,__ATOMIC_ACQUIRE)==1)return -11;
    for(unsigned i=0;i<550&&__atomic_load_n(&c->start_state,__ATOMIC_ACQUIRE)==1&&!sensor_expired(c);i++)IOSleep(10);
    uint32_t state=__atomic_load_n(&c->start_state,__ATOMIC_ACQUIRE);
    if(!state){uint32_t none=0;if(!__atomic_compare_exchange_n(&c->start_state,&none,4u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))state=none;else state=4;}
    if(state==4||state==3){
        uint32_t idle=0;if(!__atomic_compare_exchange_n(&c->finishing,&idle,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -11;
        c->cleanup_error=sensor_finish(c);if(!c->cleanup_error)__atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);
        sensor_snapshot(c,c->cleanup_error?EldenSensorRuntimeRetained:EldenSensorRuntimeStopped);__atomic_store_n(&c->finishing,0u,__ATOMIC_RELEASE);return c->cleanup_error;
    }
    for(unsigned i=0;i<550&&!__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)&&!sensor_expired(c);i++)IOSleep(10);
    if(!__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)&&sensor_expired(c))sensor_cancel_listener(c);
    return __atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)?c->cleanup_error:-11;
}
extern "C" int elden_sensor_runtime_status(EldenSensorRuntimeStatus *out){
    auto *c=__atomic_load_n(&sensor_runtime,__ATOMIC_ACQUIRE);if(!c||!out)return -22;
    IOSimpleLockLock(c->status_lock);*out=c->status;IOSimpleLockUnlock(c->status_lock);return 0;
}
extern "C" int elden_sensor_runtime_recovery_stop(void){
    auto *c=__atomic_load_n(&sensor_runtime,__ATOMIC_ACQUIRE);
    return c?elden_sensor_runtime_stop(c->config.adsp.owner,c->config.adsp.epoch)==0:elden_motion_native_ssc_recovery_stop();
}
