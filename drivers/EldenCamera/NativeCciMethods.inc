/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Same actual native owner/mask/drain/register methods in host and XNU builds. */
namespace {
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    EldenCameraCciProvider provider={};unsigned controller=0;Window cci,gic;EldenCci core={};IOLock *lock=nullptr;
    uint32_t attempted_irq=0,attached=0,inflight=0,ready=0,prepared=0,stopped=0;int error=0;
};
static Context *contexts[2];static uint32_t attempts[2];
static void record_error(Context *c,int error){int empty=0;if(error)(void)__atomic_compare_exchange_n(&c->error,&empty,error,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);}
static int admission(Context *c,unsigned operation){
    if(!c||!c->provider.admit||ml_at_interrupt_context()||!armcpu_gic_ready)return -13;
    if(operation!=EldenCciCleanup&&(__atomic_load_n(&c->stopped,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE)))return -125;
    int rc=c->provider.admit(c->provider.context,c->provider.owner,c->provider.epoch,c->controller,operation);if(rc)return rc;
    return c->provider.clock_hz(c->provider.context,c->provider.owner,c->provider.epoch,c->controller)==37500000?0:-95;
}
static int admit(void *v,uint64_t owner,uint64_t epoch,unsigned controller,unsigned operation){
    Context *c=(Context *)v;if(c->provider.owner!=owner||c->provider.epoch!=epoch||c->controller!=controller)return -116;
    return admission(c,operation);
}
static int read(void *v,unsigned reg,uint32_t *out){Context *c=(Context *)v;
    int rc=admission(c,EldenCciCleanup);return rc?rc:c->cci.read(reg,out);}
static int write(void *v,unsigned reg,uint32_t value){Context *c=(Context *)v;
    int rc=admission(c,EldenCciCleanup);if(rc)return rc;
    // Exact controller registers used by the reviewed original-master0 path.
    // Restore allows only the previously snapshotted timing of both masters.
    bool allowed=reg==4||reg==8||reg==0x34||reg==0x300||reg==0x310||reg==0x400||reg==0x410||
        reg==0xc00||reg==0xc04||reg==0xc08||(reg>=0x100&&reg<=0x110)||(reg>=0x200&&reg<=0x210);
    return allowed?c->cci.write(reg,value):-13;
}
static int delay(void *,unsigned us){if(us!=10)return -22;IODelay(us);return 0;}
static void irq(void *v){Context *c=(Context *)v;__atomic_fetch_add(&c->inflight,1u,__ATOMIC_ACQ_REL);
    if(__atomic_load_n(&c->attached,__ATOMIC_ACQUIRE)){
        IOReturn rc=armcpu_spi_mask(CciIntid[c->controller],true);
        // This path polls with its exclusive GIC IRQ masked. An observed
        // unexpected primary delivery closes admission; never fake completion.
        record_error(c,rc?(int)rc:-71);
    }
    __atomic_fetch_sub(&c->inflight,1u,__ATOMIC_RELEASE);
}
static int mask(void *v,int on){Context *c=(Context *)v;
    if(on!=1||!c->attached)return -13;unsigned intid=CciIntid[c->controller];
    IOReturn rc=armcpu_spi_mask(intid,true);if(rc)return (int)rc;
    unsigned word=(intid/32)*4,bit=1u<<(intid%32);
    for(unsigned i=0;i<2000;i++){
        uint32_t ctl=0,en=0;int error=c->gic.read(0,&ctl);if(!error)error=c->gic.read(0x100+word,&en);if(error)return error;
        if(!(ctl&(1u<<31))&&!(en&bit))return 0;IODelay(1);
    }
    return -110;
}
static int drain(void *v){Context *c=(Context *)v;int rc=mask(v,1);if(rc)return rc;
    unsigned id=CciIntid[c->controller],word=(id/32)*4,bit=1u<<(id%32);
    for(unsigned i=0;i<2000;i++){
        uint32_t active;rc=c->gic.read(0x300+word,&active);if(rc)return rc;
        if(!(active&bit)&&!__atomic_load_n(&c->inflight,__ATOMIC_ACQUIRE)){
            rc=c->gic.write(0x280+word,bit);if(rc)return rc;uint32_t pending;rc=c->gic.read(0x200+word,&pending);
            return rc?rc:pending&bit?-16:0;
        }
        IODelay(1);
    }
    return -110;
}
static Context *find(uint64_t owner,uint64_t epoch,unsigned controller){
    if(controller>1)return nullptr;Context *c=__atomic_load_n(&contexts[controller],__ATOMIC_ACQUIRE);
    return c&&c->provider.owner==owner&&c->provider.epoch==epoch?c:nullptr;
}
}
extern "C" int elden_camera_native_cci_open(const EldenCameraCciProvider *provider,unsigned controller){
    if(!provider||!provider->admit||!provider->clock_hz||!provider->owner||!provider->epoch||controller>1||ml_at_interrupt_context()||!armcpu_gic_ready)return -22;
    int rc=provider->admit(provider->context,provider->owner,provider->epoch,controller,EldenCciAcquire);if(rc)return rc;
    if(provider->clock_hz(provider->context,provider->owner,provider->epoch,controller)!=37500000)return -95;
    uint32_t empty=0;if(!__atomic_compare_exchange_n(&attempts[controller],&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    Context *c=new Context;if(!c)return -12;c->provider=*provider;c->controller=controller;c->lock=IOLockAlloc();
    __atomic_store_n(&contexts[controller],c,__ATOMIC_RELEASE);
    if(!c->lock||!c->cci.map(CciBase[controller],0x1000)||!c->gic.map(0x16000000,0x10000))return -12;
    rc=admission(c,EldenCciAcquire);if(rc)return rc;
    uint32_t observed=0;uint64_t router=0;c->attempted_irq=1;
    IOReturn attached=armcpu_spi_attach(CciIntid[controller],true,irq,c,&observed,&router);
    if(attached)return (int)attached;__atomic_store_n(&c->attached,1u,__ATOMIC_RELEASE);
    if((rc=mask(c,1)))return rc;
    if((observed&0xff07)!=0xa007)return -71;
    EldenCciIo io={c,provider->owner,provider->epoch,admit,read,write,now_us,delay,mask,drain};
    IOLockLock(c->lock);
    rc=elden_camera_cci_init(&c->core,&io,controller,37500000);
    if(!rc)rc=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE); // actual unexpected IRQ cannot be erased by init success
    if(rc)record_error(c,rc);
    else __atomic_store_n(&c->ready,1u,__ATOMIC_RELEASE);
    __atomic_store_n(&c->prepared,1u,__ATOMIC_RELEASE);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_cci_read(uint64_t owner,uint64_t epoch,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t *out){
    Context *c=find(owner,epoch,controller);if(!c||!out)return -116;
    if(!c->lock||!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)||ml_at_interrupt_context())return -19;
    IOLockLock(c->lock);int rc=admission(c,EldenCciTransfer);uint32_t value=0;
    if(!rc)rc=elden_camera_cci_read_register(&c->core,slave,reg,width,&value);
    if(!rc)rc=admission(c,EldenCciTransfer);
    if(!rc)*out=value;
    if(rc&&rc!=-22)record_error(c,rc);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_cci_write(uint64_t owner,uint64_t epoch,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t value){
    Context *c=find(owner,epoch,controller);if(!c)return -116;
    if(!c->lock||!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)||ml_at_interrupt_context())return -19;
    IOLockLock(c->lock);int rc=admission(c,EldenCciTransfer);
    if(!rc)rc=elden_camera_cci_write_register(&c->core,slave,reg,width,value);
    if(!rc)rc=admission(c,EldenCciTransfer);
    if(rc&&rc!=-22)record_error(c,rc);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_cci_stop(uint64_t owner,uint64_t epoch,unsigned controller){
    Context *c=find(owner,epoch,controller);if(!c)return -116;
    if(!c->lock||!__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE)||ml_at_interrupt_context())return -19;
    IOLockLock(c->lock);__atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);
    int rc=elden_camera_cci_stop(&c->core);if(!rc)__atomic_store_n(&c->stopped,1u,__ATOMIC_RELEASE);
    else record_error(c,rc);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_cci_status(unsigned controller,EldenCciStatus *out){
    if(controller>1||!out)return -22;Context *c=__atomic_load_n(&contexts[controller],__ATOMIC_ACQUIRE);
    if(!c||!c->lock||!__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE))return -19;
    if(ml_at_interrupt_context())return -16;IOLockLock(c->lock);int rc=elden_camera_cci_status(&c->core,out);
    if(!rc&&!out->error)out->error=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_native_cci_ready(uint64_t owner,uint64_t epoch,unsigned controller){
    Context *c=find(owner,epoch,controller);if(!c)return -116;
    if(!c->lock||!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)||ml_at_interrupt_context())return -19;
    IOLockLock(c->lock);int rc=admission(c,EldenCciTransfer);IOLockUnlock(c->lock);return rc;
}
