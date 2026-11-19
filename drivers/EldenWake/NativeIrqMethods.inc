/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native methods shared with explicit host OS/MMIO peers. */
namespace {
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Window windows[3];EldenWakeIrq route={};
    void *owner_context=nullptr;int (*owner_gate)(void *,uint64_t,uint64_t,int)=nullptr;
    uint64_t owner=0,epoch=0;uint32_t pending=0,inflight=0,attached=0,ready=0;int error=0;
};
static Context *active;static uint32_t attempted;
constexpr unsigned IrqWord=(EldenWakeIntid/32)*4,IrqBit=1u<<(EldenWakeIntid%32);
static int admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){
    Context *c=(Context *)v;if(!c||c->owner!=owner||c->epoch!=epoch||!c->owner_gate||ml_at_interrupt_context())return -13;
    if(!armcpu_gic_ready)return -19;return c->owner_gate(c->owner_context,owner,epoch,cleanup);
}
static int read(void *v,unsigned s,uint32_t reg,uint32_t *value){return s<3?((Context *)v)->windows[s].read(reg,value):-22;}
static int write(void *v,unsigned s,uint32_t reg,uint32_t value){
    // Strict own registers. All GIC status/enable writes stay in callbacks.
    if(s==EldenWakeTlmm&&reg!=8)return -13;
    if(s==EldenWakePdc&&reg!=0x110+4*109&&reg!=0x10+4*(109/32))return -13;
    return s<2?((Context *)v)->windows[s].write(reg,value):-13;
}
static int mask(void *v,unsigned intid,int on){
    Context *c=(Context *)v;if(intid!=EldenWakeIntid||(on!=0&&on!=1)||!c->attached)return -13;
    IOReturn rc=armcpu_spi_mask(intid,on!=0);if(rc)return (int)rc;
    for(unsigned i=0;i<2000;i++){
        uint32_t ctl,enable;int e=c->windows[EldenWakeGic].read(0,&ctl);
        if(!e)e=c->windows[EldenWakeGic].read(0x100+IrqWord,&enable);if(e)return e;
        // A real already-pending edge may run our handler immediately on
        // unmask and mask itself again. That observed own delivery is accepted,
        // rather than waiting2ms for an enable bit the handler rightly cleared.
        bool delivered=!on&&__atomic_load_n(&c->pending,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
        if(!(ctl&(1u<<31))&&(((enable&IrqBit)!=0)==!on||delivered))return 0;IODelay(1);
    }
    return -110;
}
static void irq(void *v){
    Context *c=(Context *)v;__atomic_fetch_add(&c->inflight,1u,__ATOMIC_ACQ_REL);
    if(__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)&&__atomic_load_n(&c->route.state,__ATOMIC_ACQUIRE)==EldenWakeRouteArmed){
        IOReturn rc=armcpu_spi_mask(EldenWakeIntid,true);
        if(rc)__atomic_store_n(&c->error,(int)rc,__ATOMIC_RELEASE);
        else __atomic_store_n(&c->pending,1u,__ATOMIC_RELEASE); // latched until owning worker; never wraps
        thread_wakeup((event_t)&c->pending);
    }
    __atomic_fetch_sub(&c->inflight,1u,__ATOMIC_RELEASE);
}
static int attach(void *v,unsigned intid,int edge){
    Context *c=(Context *)v;if(intid!=EldenWakeIntid||edge!=1)return -22;uint32_t observed=0;uint64_t router=0;
    IOReturn rc=armcpu_spi_attach(intid,true,irq,c,&observed,&router);
    // A refused attach may mean a peer owns652. Never mask that peer's route.
    if(rc)return (int)rc;c->attached=1;
    // Actual Group1/enabled/edge/priority readback from the provider. Router
    // is observed but not a claim about another CPU's current DAIF.
    return (observed&0xff07)==0xa007?0:-71;
}
static int drain(void *v,unsigned intid){
    Context *c=(Context *)v;if(intid!=EldenWakeIntid)return -13;
    int rc=mask(v,intid,1);if(rc)return rc;
    for(unsigned i=0;i<2000;i++){
        uint32_t active_bits;if((rc=c->windows[EldenWakeGic].read(0x300+IrqWord,&active_bits)))return rc;
        if(!(active_bits&IrqBit)&&!__atomic_load_n(&c->inflight,__ATOMIC_ACQUIRE)){
            if((rc=c->windows[EldenWakeGic].write(0x280+IrqWord,IrqBit)))return rc;
            uint32_t pending;if((rc=c->windows[EldenWakeGic].read(0x200+IrqWord,&pending)))return rc;
            return pending&IrqBit?-16:0;
        }
        IODelay(1);
    }
    return -110;
}
static Context *find(uint64_t owner,uint64_t epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c&&c->owner==owner&&c->epoch==epoch?c:nullptr;
}
}
extern "C" int elden_wake_native_irq_acquire(uint64_t owner,uint64_t epoch,void *context,int (*gate)(void *,uint64_t,uint64_t,int)){
    if(!owner||!epoch||!gate||ml_at_interrupt_context()||!armcpu_gic_ready)return -22;
    int rc=gate(context,owner,epoch,0);if(rc)return rc;uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&attempted,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    Context *c=new Context;if(!c)return -12;c->owner=owner;c->epoch=epoch;c->owner_gate=gate;c->owner_context=context;
    __atomic_store_n(&active,c,__ATOMIC_RELEASE);
    if(!c->windows[EldenWakeTlmm].map(TlmmGpio,0x1000)||!c->windows[EldenWakePdc].map(PdcBase,0x2000)||
       !c->windows[EldenWakeGic].map(GicBase,0x10000))return -12;
    // The native adapter owns a per-pin CFG, not a global legacy enable-bank
    // lock shared by every PDC writer. Refuse older hardware before attach or
    // writes until that real shared provider exists. Portable legacy semantics
    // remain tested for adapters that do supply such serialization.
    uint32_t version=0;
    if((rc=c->windows[EldenWakePdc].read(0x1000,&version)))return rc;
    if(version<0x30200)return -95;
    EldenWakeIrqIo io={c,admit,read,write,attach,mask,drain};rc=elden_wake_irq_acquire(&c->route,&io,owner,epoch);
    __atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);if(!rc)__atomic_store_n(&c->ready,1u,__ATOMIC_RELEASE);return rc;
}
extern "C" int elden_wake_native_irq_arm(uint64_t owner,uint64_t epoch){
    Context *c=find(owner,epoch);if(!c||!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE))return -125;
    // Must drain any prior IRQ/gesture packet before rearming; no silent loss.
    if(__atomic_load_n(&c->pending,__ATOMIC_ACQUIRE))return -16;
    int rc=elden_wake_irq_arm(&c->route,owner,epoch);if(rc)__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);return rc;
}
extern "C" int elden_wake_native_irq_disarm(uint64_t owner,uint64_t epoch){
    Context *c=find(owner,epoch);if(!c)return -116;
    int rc=elden_wake_irq_disarm(&c->route,owner,epoch);if(rc)__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);return rc;
}
extern "C" int elden_wake_native_irq_stop(uint64_t owner,uint64_t epoch){
    Context *c=find(owner,epoch);if(!c)return -116;__atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);
    int rc=elden_wake_irq_stop(&c->route,owner,epoch);if(rc)__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);return rc;
}
extern "C" const void *elden_wake_native_irq_event(void){Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?&c->pending:nullptr;}
extern "C" int elden_wake_native_irq_take(uint64_t owner,uint64_t epoch,uint32_t *events){
    Context *c=find(owner,epoch);if(!c||!events)return -22;
    if(!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE))return -125;
    int rc=admit(c,owner,epoch,0);if(rc)return rc;
    rc=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);if(rc)return rc;
    uint32_t n=__atomic_exchange_n(&c->pending,0u,__ATOMIC_ACQ_REL);*events=n;return 0;
}
