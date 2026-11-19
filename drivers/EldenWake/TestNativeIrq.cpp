/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute the SAME native IRQ methods with explicit allocation/map/GIC/time
 * peers. Callback readback/drain results do not prove a real hardware IRQ. */
#include "Irq.h"
#include "NativeIrq.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <cstring>
static unsigned checks,scenarios,wakes,mask_calls,attach_calls,drain_writes;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using IOReturn=int;using boolean_t=bool;using event_t=void *;
static volatile uint32_t armcpu_gic_ready=1;
static int attach_error,mask_error,gate_error,map_failure;static bool interrupt_context,gic_busy,gic_active,gic_pending_stuck,irq_on_unmask;
static unsigned delays,maps;static uint32_t attach_flags=0xa007;
static void (*handler)(void *);static void *handler_context;
static std::vector<void *> allocations;
static void *IOMalloc(size_t n){void *p=std::malloc(n);allocations.push_back(p);return p;}
static void IOFree(void *p,size_t){std::free(p);}
static bool ml_at_interrupt_context(){return interrupt_context;}
static void IODelay(unsigned us){CHECK(us==1);delays++;}
static void thread_wakeup(event_t event){CHECK(event);wakes++;}
namespace {
enum {PdcBase=0x0b220000,TlmmGpio=0x0f100000+162*0x1000,GicBase=0x16000000};
static uint32_t tlmm[4],pdc[0x2000/4],gic[0x10000/4];
struct Window {
    uintptr_t address=0;unsigned bytes=0;
    bool map(uint64_t pa,unsigned n){maps++;if(map_failure==(int)maps)return false;
        CHECK((pa==TlmmGpio&&n==0x1000)||(pa==PdcBase&&n==0x2000)||(pa==GicBase&&n==0x10000));
        address=pa==TlmmGpio?(uintptr_t)tlmm:pa==PdcBase?(uintptr_t)pdc:(uintptr_t)gic;bytes=n;return true;
    }
    int read(unsigned reg,uint32_t *out){
        CHECK(out&&address&&!(reg&3)&&reg<bytes);
        if(address==(uintptr_t)gic){
            if(reg==0&&gic_busy){*out=1u<<31;return 0;}
            if(reg==0x300+4*(652/32)&&gic_active){*out=1u<<(652%32);return 0;}
        }
        *out=((uint32_t *)address)[reg/4];return 0;
    }
    int write(unsigned reg,uint32_t value){
        CHECK(address&&!(reg&3)&&reg<bytes);
        if(address==(uintptr_t)gic){CHECK(reg==0x280+4*(652/32)&&value==(1u<<(652%32)));drain_writes++;
            if(!gic_pending_stuck)gic[(0x200+4*(652/32))/4]&=~value;return 0;}
        CHECK((address==(uintptr_t)tlmm&&reg==8)||(address==(uintptr_t)pdc&&(reg==0x110+4*109||reg==0x1c)));
        ((uint32_t *)address)[reg/4]=value;return 0;
    }
};
}
static IOReturn armcpu_spi_mask(unsigned id,bool on){
    CHECK(id==652);mask_calls++;if(mask_error)return mask_error;
    uint32_t bit=1u<<(652%32);if(on)gic[(0x100+4*(652/32))/4]&=~bit;else {
        gic[(0x100+4*(652/32))/4]|=bit;
        if(irq_on_unmask&&handler){irq_on_unmask=false;handler(handler_context);}
    }return 0;
}
static IOReturn armcpu_spi_attach(unsigned id,bool edge,void (*callback)(void *),void *context,uint32_t *flags,uint64_t *router){
    CHECK(id==652&&edge&&callback&&context);attach_calls++;if(attach_error)return attach_error;
    handler=callback;handler_context=context;*flags=attach_flags;*router=0;gic[(0x100+4*(652/32))/4]|=1u<<(652%32);return 0;
}
#include "NativeIrqMethods.inc"
static int owner_gate(void *context,uint64_t owner,uint64_t epoch,int cleanup){
    CHECK(context==(void *)1&&owner==11&&epoch==7&&(cleanup==0||cleanup==1));return gate_error;
}
static void setup(){
    scenarios++;for(void *p:allocations)std::free(p);allocations.clear();active=nullptr;attempted=0;
    attach_error=mask_error=gate_error=map_failure=0;gic_busy=gic_active=gic_pending_stuck=interrupt_context=irq_on_unmask=false;
    wakes=mask_calls=attach_calls=drain_writes=maps=delays=0;handler=nullptr;handler_context=nullptr;attach_flags=0xa007;armcpu_gic_ready=1;
    std::memset(tlmm,0,sizeof(tlmm));std::memset(pdc,0,sizeof(pdc));std::memset(gic,0,sizeof(gic));
    tlmm[0]=3u|3u<<6;tlmm[2]=0x340;pdc[0x1000/4]=0x30200;pdc[(0x110+4*109)/4]=0x100;
}
static int acquire(){return elden_wake_native_irq_acquire(11,7,(void *)1,owner_gate);}
int main(){
    setup();gate_error=-13;CHECK(acquire()==-13&&!maps&&!attach_calls&&!attempted);
    setup();map_failure=2;CHECK(acquire()==-12&&active&&maps==2&&!attach_calls);CHECK(acquire()==-16);
    setup();attach_error=-16;CHECK(acquire()==-16&&!mask_calls&&active&&!active->attached); // do not mask peer's652
    setup();pdc[0x1000/4]=0x30100;CHECK(acquire()==-95&&!attach_calls&&!mask_calls&&!drain_writes);
    setup();attach_flags=0xa003;CHECK(acquire()==-71&&active&&mask_calls==1&&active->route.state==EldenWakeRouteHeld);
    setup();CHECK(!acquire()&&active->ready&&active->attached&&handler);uint32_t n=9;
    handler(handler_context);CHECK(!wakes&&!active->pending&&!active->inflight); // owned but not armed
    CHECK(!elden_wake_native_irq_arm(11,7));gic[(0x200+4*(652/32))/4]|=1u<<(652%32);
    interrupt_context=true;handler(handler_context);interrupt_context=false;
    CHECK(wakes==1&&active->pending==1&&!active->inflight&&!(gic[(0x100+4*(652/32))/4]&(1u<<(652%32))));
    CHECK(elden_wake_native_irq_arm(11,7)==-16);CHECK(elden_wake_native_irq_event()!=nullptr);
    CHECK(!elden_wake_native_irq_disarm(11,7)&&drain_writes==2);CHECK(!elden_wake_native_irq_take(11,7,&n)&&n==1&&!active->pending);
    CHECK(!elden_wake_native_irq_arm(11,7));CHECK(!elden_wake_native_irq_stop(11,7)&&!active->ready&&active->route.state==EldenWakeRouteStopped);
    CHECK(tlmm[2]==0x340&&pdc[(0x110+4*109)/4]==0x100&&allocations.size()==1);
    setup();CHECK(!acquire());gic_active=true;CHECK(elden_wake_native_irq_arm(11,7)==-110&&delays==2000&&active->route.state==EldenWakeRouteHeld&&!drain_writes);
    setup();CHECK(!acquire());gic[(0x200+4*(652/32))/4]=1u<<(652%32);gic_pending_stuck=true;
    CHECK(elden_wake_native_irq_arm(11,7)==-16&&active->route.state==EldenWakeRouteHeld);
    setup();CHECK(!acquire());gic_busy=true;CHECK(elden_wake_native_irq_arm(11,7)==-110&&active->route.state==EldenWakeRouteHeld&&delays>=2000);
    setup();CHECK(!acquire());CHECK(!elden_wake_native_irq_arm(11,7));mask_error=-5;
    handler(handler_context);CHECK(active->error==-5&&!active->pending&&wakes==1);CHECK(elden_wake_native_irq_take(11,7,&n)==-5);
    setup();CHECK(!acquire());CHECK(elden_wake_native_irq_arm(21,7)==-125);gate_error=-13;CHECK(elden_wake_native_irq_arm(11,7)==-13);
    setup();CHECK(!acquire());irq_on_unmask=true;
    CHECK(!elden_wake_native_irq_arm(11,7)&&active->pending==1&&wakes==1&&!delays);
    handler(handler_context);CHECK(active->pending==1&&wakes==2); // one latched wake; cannot overflow/replay a count
    CHECK(!elden_wake_native_irq_disarm(11,7)&&!elden_wake_native_irq_take(11,7,&n)&&n==1);
    for(void *p:allocations)std::free(p);allocations.clear();
    std::printf("PASS: %u actual native IRQ scenarios/%u checks; attach/refusal/no-peer-mask/handler/drain/pending/RWP/ownership/retention; OS/MMIO/GIC modeled\n",scenarios,checks);return 0;
}
