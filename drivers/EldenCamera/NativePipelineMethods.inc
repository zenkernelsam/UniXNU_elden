/* SPDX-License-Identifier: BSD-2-Clause-Patent */
namespace {
struct CameraPipelineContext;
struct CameraPipelineIrq {CameraPipelineContext *context;uint32_t index;};
struct CameraPipelineContext {
    static void *operator new(size_t bytes) noexcept{return IOMalloc(bytes);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(CameraPipelineContext));}
    EldenCameraPipelineConfig config={};EldenCameraNativePipelineOwner provider={};EldenCameraPipeline core={};
    PipelineWindow block[3],gic;IOLock *lock=nullptr;uint32_t intids[3]={},attached[3]={};CameraPipelineIrq irq[3]={};
    uint32_t inflight=0,prepared=0,stopped=0;int error=0;
};
static CameraPipelineContext *camera_pipelines[2];static uint32_t camera_pipeline_claims;
static int pipeline_admit(CameraPipelineContext *c,unsigned op){
    if(!c||ml_at_interrupt_context()||!armcpu_gic_ready)return -13;
    if(op!=EldenPipeCleanup&&(c->stopped||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)))return -125;
    int rc=c->provider.admit(c->provider.context,&c->config,op);return rc<=0?rc:-13;
}
static void pipeline_error(CameraPipelineContext *c,int rc){int none=0;if(rc)__atomic_compare_exchange_n(&c->error,&none,rc<0?rc:-13,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
static int pipeline_mask(CameraPipelineContext *c,unsigned i){
    if(!c->attached[i])return -19;uint32_t id=c->intids[i],word=id/32*4,bit=1u<<(id%32);IOReturn rc=armcpu_spi_mask(id,true);if(rc)return (int)rc;
    for(unsigned n=0;n<2000;n++){uint32_t ctrl=0,enabled=0;int error=c->gic.read(0,&ctrl);if(!error)error=c->gic.read(0x100+word,&enabled);if(error)return error;
        if(!(ctrl&(1u<<31))&&!(enabled&bit))return 0;IODelay(1);}return -110;
}
static int pipeline_irq_drain(CameraPipelineContext *c){
    for(unsigned i=0;i<3;i++){int rc=pipeline_mask(c,i);if(rc)return rc;uint32_t id=c->intids[i],word=id/32*4,bit=1u<<(id%32);
        for(unsigned n=0;n<2000;n++){uint32_t active=0;rc=c->gic.read(0x300+word,&active);if(rc)return rc;
            if(!(active&bit)&&!__atomic_load_n(&c->inflight,__ATOMIC_ACQUIRE)){
                rc=c->gic.write(0x280+word,bit);if(rc)return rc;uint32_t pending=0;rc=c->gic.read(0x200+word,&pending);if(rc)return rc;if(pending&bit)return -16;break;
            }if(n==1999)return -110;IODelay(1);
        }
    }return 0;
}
static void pipeline_irq(void *v){auto *irq=(CameraPipelineIrq *)v;auto *c=irq->context;
    __atomic_fetch_add(&c->inflight,1u,__ATOMIC_ACQ_REL);if(c->attached[irq->index]){IOReturn rc=armcpu_spi_mask(c->intids[irq->index],true);pipeline_error(c,rc?(int)rc:-71);}
    __atomic_fetch_sub(&c->inflight,1u,__ATOMIC_RELEASE);
}
static int pipeline_core_admit(void *p,const EldenCameraPipelineConfig *cfg,unsigned op){auto *c=(CameraPipelineContext *)p;
    return cfg->owner==c->config.owner&&cfg->epoch==c->config.epoch&&cfg->route_epoch==c->config.route_epoch&&cfg->sensor==c->config.sensor?pipeline_admit(c,op):-116;}
static int pipeline_read(void *p,unsigned block,uint32_t reg,uint32_t *out){auto *c=(CameraPipelineContext *)p;int rc=pipeline_admit(c,EldenPipeCleanup);return rc?rc:block>=3?-22:c->block[block].read(reg,out);}
static int pipeline_write(void *p,unsigned block,uint32_t reg,uint32_t value){auto *c=(CameraPipelineContext *)p;int rc=pipeline_admit(c,EldenPipeCleanup);return rc?rc:block>=3?-22:c->block[block].write(reg,value);}
static int pipeline_delay(void *,unsigned us){if(us>8000)return -22;IODelay(us);return 0;}
static int pipeline_buffer(void *p,const EldenCameraPipelineConfig *cfg,const EldenCameraBuffer *b,int complete){auto *c=(CameraPipelineContext *)p;int rc=pipeline_admit(c,EldenPipeCapture);return rc?rc:c->provider.buffer(c->provider.context,cfg,b,complete);}
static int pipeline_drain(void *p,const EldenCameraPipelineConfig *cfg){auto *c=(CameraPipelineContext *)p;int rc=pipeline_admit(c,EldenPipeCleanup);
    if(!rc)rc=c->provider.drain(c->provider.context,cfg);if(!rc)rc=pipeline_irq_drain(c);return rc;}
static CameraPipelineContext *pipeline_find(uint64_t owner,uint64_t epoch,unsigned sensor){if(sensor>1)return nullptr;auto *c=__atomic_load_n(&camera_pipelines[sensor],__ATOMIC_ACQUIRE);return c&&c->config.owner==owner&&c->config.epoch==epoch?c:nullptr;}
}
extern "C" int elden_camera_native_pipeline_open(const EldenCameraPipelineConfig *cfg,const EldenCameraNativePipelineOwner *owner){
    uint64_t physical[3];uint32_t bytes[3],intids[3];if(!cfg||!owner||!owner->admit||!owner->buffer||!owner->drain||ml_at_interrupt_context()||!armcpu_gic_ready||elden_camera_pipeline_resources(cfg,physical,bytes,intids))return -22;
    int rc=owner->admit(owner->context,cfg,EldenPipeAcquire);if(rc)return rc<0?rc:-13;
    uint32_t bits=(1u<<cfg->sensor)|(1u<<(cfg->decoder+2)),previous=__atomic_load_n(&camera_pipeline_claims,__ATOMIC_ACQUIRE);
    for(;;){if(previous&bits)return -16;if(__atomic_compare_exchange_n(&camera_pipeline_claims,&previous,previous|bits,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;}
    auto *c=new CameraPipelineContext;if(!c)return -12;c->config=*cfg;c->provider=*owner;c->lock=IOLockAlloc();__atomic_store_n(&camera_pipelines[cfg->sensor],c,__ATOMIC_RELEASE);
    if(!c->lock||!c->gic.map(0x16000000,0x10000))return -12;
    if((rc=pipeline_admit(c,EldenPipeAcquire)))return rc;
    for(unsigned i=0;i<3;i++){
        if((rc=pipeline_admit(c,EldenPipeAcquire)))return rc;
        if(!c->block[i].map(physical[i],bytes[i]))return -12;c->intids[i]=intids[i];c->irq[i]={c,i};
        uint32_t observed=0;uint64_t router=0;IOReturn error=armcpu_spi_attach(intids[i],true,pipeline_irq,&c->irq[i],&observed,&router);
        if(error)return (int)error;c->attached[i]=1;if((observed&0xff07)!=0xa007)return -71;if((rc=pipeline_mask(c,i)))return rc;
    }
    if((rc=pipeline_irq_drain(c)))return rc;
    EldenCameraPipelineIo io={c,pipeline_core_admit,pipeline_read,pipeline_write,pipeline_now_us,pipeline_delay,pipeline_buffer,pipeline_drain};
    IOLockLock(c->lock);rc=elden_camera_pipeline_open(&c->core,cfg,&io);if(!rc)rc=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    if(rc)pipeline_error(c,rc);else c->prepared=1;IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_pipeline_queue(uint64_t owner,uint64_t epoch,unsigned sensor,const EldenCameraBuffer *b){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeConfigure);if(!rc)rc=elden_camera_pipeline_queue(&c->core,b);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_start(uint64_t owner,uint64_t epoch,unsigned sensor){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeConfigure);if(!rc)rc=elden_camera_pipeline_start(&c->core);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_service(uint64_t owner,uint64_t epoch,unsigned sensor){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeCapture);if(!rc)rc=elden_camera_pipeline_service(&c->core);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_frame(uint64_t owner,uint64_t epoch,unsigned sensor,EldenCameraFrame *out){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeCapture);if(!rc)rc=elden_camera_pipeline_frame(&c->core,out);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_stop(uint64_t owner,uint64_t epoch,unsigned sensor){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeCleanup);if(!rc)rc=elden_camera_pipeline_stop(&c->core);if(!rc)c->stopped=1;IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_status(unsigned sensor,EldenCameraPipelineStatus *out){if(sensor>1||!out||ml_at_interrupt_context())return -22;auto *c=__atomic_load_n(&camera_pipelines[sensor],__ATOMIC_ACQUIRE);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=elden_camera_pipeline_status(&c->core,out);if(!rc&&!out->error)out->error=(uint32_t)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_ready(uint64_t owner,uint64_t epoch,unsigned sensor){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeCapture);if(!rc)rc=elden_camera_pipeline_ready(&c->core);IOLockUnlock(c->lock);return rc;}
extern "C" int elden_camera_native_pipeline_prepared(uint64_t owner,uint64_t epoch,unsigned sensor){auto *c=pipeline_find(owner,epoch,sensor);if(!c||!c->lock||!c->prepared)return -19;IOLockLock(c->lock);int rc=pipeline_admit(c,EldenPipeConfigure);EldenCameraPipelineStatus s={};if(!rc)rc=elden_camera_pipeline_status(&c->core,&s);if(!rc&&s.state!=EldenPipeReady)rc=-19;IOLockUnlock(c->lock);return rc;}
