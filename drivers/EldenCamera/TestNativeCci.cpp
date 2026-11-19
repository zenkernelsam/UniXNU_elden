/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual NativeCciMethods + Cci + original source-derived camera control.
 * OS/MMIO/GIC/clock and sensor rails/receiver are explicit peers, not hardware. */
#include "NativeCci.h"
#include "Core.h"
#include "SensorBinding.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks,scenarios,maps,attach_calls,mask_calls,delay_calls;static uint32_t gic[0x10000/4];
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
extern "C" {
void *elden_camera_cci_peer_create(unsigned,int);int elden_camera_cci_peer_read(void *,unsigned,uint32_t *);
int elden_camera_cci_peer_write(void *,unsigned,uint32_t);uint64_t elden_camera_cci_peer_now(void *);
void elden_camera_cci_peer_delay(void *,unsigned);void elden_camera_cci_peer_fault(void *,unsigned);
unsigned elden_camera_cci_peer_stat(void *,unsigned);void elden_camera_cci_peer_destroy(void *);
}
using IOReturn=int;using boolean_t=bool;
static uint32_t armcpu_gic_ready=1;static bool irq_context,busy_gic,active_gic,fail_lock;
static int provider_error,attach_error,mask_error,map_failure;static unsigned actual_clock[2]={37500000,37500000};
static void (*handlers[2])(void *);static void *handler_contexts[2],*peers[2];
static std::vector<void *> allocations;
static void *IOMalloc(size_t n){void *p=std::malloc(n);CHECK(p);allocations.push_back(p);return p;}
static void IOFree(void *p,size_t){std::free(p);}
struct IOLock{std::mutex mutex;};static std::vector<IOLock *> locks;
static IOLock *IOLockAlloc(){if(fail_lock)return nullptr;auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->mutex.lock();}static void IOLockUnlock(IOLock *p){p->mutex.unlock();}
static bool ml_at_interrupt_context(){return irq_context;}
static void IODelay(unsigned us){CHECK(us==1||us==10);delay_calls++;for(void *p:peers)if(p)elden_camera_cci_peer_delay(p,us);}
static uint64_t now_us(void *p){(void)p;return peers[0]?elden_camera_cci_peer_now(peers[0]):0;}
namespace {
constexpr uint64_t CciBase[2]={0x0ac7b000,0x0ac7c000};constexpr unsigned CciIntid[2]={458,459};
struct Window {
    unsigned source=9;void *peer=nullptr;
    bool map(uint64_t pa,unsigned bytes){maps++;if(map_failure==(int)maps)return false;
        if(pa==0x16000000){CHECK(bytes==0x10000);source=2;return true;}
        CHECK((pa==CciBase[0]||pa==CciBase[1])&&bytes==0x1000);source=pa==CciBase[1];peer=peers[source];return peer!=nullptr;
    }
    int read(unsigned reg,uint32_t *out){
        if(source<2)return elden_camera_cci_peer_read(peer,reg,out);
        CHECK(source==2&&reg<0x10000&&!(reg&3));
        if(reg==0&&busy_gic){*out=1u<<31;return 0;}
        if((reg==0x300+4*(458/32))&&active_gic){*out=(1u<<(458%32))|(1u<<(459%32));return 0;}
        *out=gic[reg/4];return 0;
    }
    int write(unsigned reg,uint32_t value){
        if(source<2)return elden_camera_cci_peer_write(peer,reg,value);
        CHECK(source==2&&reg==0x280+4*(458/32)&&(value==(1u<<(458%32))||value==(1u<<(459%32))));
        gic[(0x200+4*(458/32))/4]&=~value;return 0;
    }
};
}
static IOReturn armcpu_spi_attach(unsigned id,bool edge,void(*fn)(void *),void *context,uint32_t *observed,uint64_t *router){
    CHECK((id==458||id==459)&&edge&&fn&&context);attach_calls++;if(attach_error)return attach_error;
    handlers[id-458]=fn;handler_contexts[id-458]=context;gic[(0x100+4*(id/32))/4]|=1u<<(id%32);*observed=0xa007;*router=0;return 0;
}
static IOReturn armcpu_spi_mask(unsigned id,bool on){CHECK((id==458||id==459)&&on);mask_calls++;if(mask_error)return mask_error;
    gic[(0x100+4*(id/32))/4]&=~(1u<<(id%32));return 0;}
#include "NativeCciMethods.inc"
static int provider_admit(void *context,uint64_t owner,uint64_t epoch,unsigned controller,unsigned operation){
    CHECK(context==(void *)1&&owner==11&&epoch==7&&controller<2&&operation<=EldenCciCleanup);return provider_error;
}
static uint32_t provider_clock(void *,uint64_t owner,uint64_t epoch,unsigned controller){CHECK(owner==11&&epoch==7&&controller<2);return actual_clock[controller];}
static EldenCameraCciProvider config(){return {(void *)1,11,7,provider_admit,provider_clock};}
static void clear(){for(void *p:peers)if(p)elden_camera_cci_peer_destroy(p);peers[0]=peers[1]=nullptr;
    for(void *p:allocations)std::free(p);allocations.clear();for(auto *p:locks)delete p;locks.clear();}
static void setup(){clear();scenarios++;std::memset(gic,0,sizeof(gic));contexts[0]=contexts[1]=nullptr;attempts[0]=attempts[1]=0;
    handlers[0]=handlers[1]=nullptr;provider_error=attach_error=mask_error=map_failure=0;irq_context=busy_gic=active_gic=fail_lock=false;
    maps=attach_calls=mask_calls=delay_calls=0;actual_clock[0]=actual_clock[1]=37500000;peers[0]=elden_camera_cci_peer_create(0,1);peers[1]=elden_camera_cci_peer_create(1,1);
}
struct Sensor {EldenCameraCore core={};unsigned target=0,rails=0,clock=0,reset=1,drains=0,ready_calls=0;bool drain_failed=false;};
static int sensor_admit(void *v,unsigned target,EldenCameraOperation operation){Sensor *s=(Sensor *)v;CHECK(target==s->target);
    return provider_admit((void *)1,11,7,target,operation==EldenCameraCleanup?EldenCciCleanup:EldenCciTransfer);
}
static int resource(void *v,unsigned target,EldenCameraResource r,int on){Sensor *s=(Sensor *)v;CHECK(target==s->target&&r<=EldenCameraAvdd);
    if(r==EldenCameraReset)s->reset=(unsigned)on;else if(r==EldenCameraClock)s->clock=(unsigned)on;
    else if(on)s->rails|=1u<<(r-EldenCameraDovdd);else s->rails&=~(1u<<(r-EldenCameraDovdd));return 0;
}
static int sensor_read(void *v,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t *out){Sensor *s=(Sensor *)v;
    CHECK(controller==s->target&&s->rails==7&&s->clock&&!s->reset&&slave==(controller?0x31:0x56));
    return elden_camera_native_cci_read(11,7,controller,slave,reg,width,out);
}
static int sensor_write(void *v,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t value){Sensor *s=(Sensor *)v;
    CHECK(controller==s->target&&s->rails==7&&s->clock&&!s->reset&&slave==(controller?0x31:0x56));
    return elden_camera_native_cci_write(11,7,controller,slave,reg,width,value);
}
static int sensor_delay(void *,unsigned us){for(void *p:peers)elden_camera_cci_peer_delay(p,us);return 0;}
static int receiver_ready(void *v,unsigned target){Sensor *s=(Sensor *)v;CHECK(s->target==target);s->ready_calls++;return 0;}
static int receiver_drain(void *v,unsigned target){Sensor *s=(Sensor *)v;CHECK(s->target==target);s->drains++;return s->drain_failed?-110:0;}
static int binding_admit(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){CHECK(owner==11&&epoch==7);return sensor_admit(v,sensor,(EldenCameraOperation)op);}
static int binding_resource(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on){CHECK(owner==11&&epoch==7);return resource(v,sensor,(EldenCameraResource)r,on);}
static int binding_ready(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){CHECK(owner==11&&epoch==7);return receiver_ready(v,sensor);}
static int binding_drain(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){CHECK(owner==11&&epoch==7);return receiver_drain(v,sensor);}
int main(){
    auto c=config();setup();provider_error=-13;CHECK(elden_camera_native_cci_open(&c,0)==-13&&!maps&&!attach_calls);
    setup();actual_clock[0]=19200000;CHECK(elden_camera_native_cci_open(&c,0)==-95&&!maps&&!attach_calls);
    setup();attach_error=-16;CHECK(elden_camera_native_cci_open(&c,0)==-16&&contexts[0]&&!contexts[0]->attached&&!mask_calls); // do not mask peer
    EldenCciStatus unready;CHECK(elden_camera_native_cci_status(0,&unready)==-19&&elden_camera_native_cci_stop(11,7,0)==-19);
    setup();map_failure=2;CHECK(elden_camera_native_cci_open(&c,0)==-12&&contexts[0]&&!attach_calls);
    setup();CHECK(!elden_camera_native_cci_open(&c,0)&&!elden_camera_native_cci_open(&c,1)&&contexts[0]->ready&&contexts[1]->ready&&attach_calls==2);
    CHECK(elden_camera_native_cci_open(&c,0)==-16);
    uint32_t value=99;CHECK(elden_camera_native_cci_read(21,7,0,0x56,0,2,&value)==-116&&value==99);
    CHECK(!elden_camera_native_cci_read(11,7,0,0x56,0,2,&value)&&value==0x38ee);
    CHECK(!elden_camera_native_cci_read(11,7,1,0x31,0x03f0,2,&value)&&value==0x08a8);
    unsigned before=elden_camera_cci_peer_stat(peers[0],6);CHECK(elden_camera_native_cci_write(11,7,0,0x56,0,1,256)==-22&&before==elden_camera_cci_peer_stat(peers[0],6));
    CHECK(!elden_camera_native_cci_stop(11,7,0)&&!contexts[0]->ready&&contexts[0]->stopped&&contexts[1]->ready);
    CHECK(elden_camera_native_cci_read(11,7,0,0x56,0,2,&value)==-19);CHECK(!elden_camera_native_cci_stop(11,7,1));
    setup();CHECK(!elden_camera_native_cci_open(&c,0));irq_context=true;handlers[0](handler_contexts[0]);irq_context=false;
    value=99;CHECK(contexts[0]->error==-71&&elden_camera_native_cci_read(11,7,0,0x56,0,2,&value)<0&&value==99);
    EldenCciStatus report;CHECK(!elden_camera_native_cci_status(0,&report)&&report.error==-71&&contexts[0]->error==-71);
    setup();CHECK(!elden_camera_native_cci_open(&c,0));active_gic=true;
    CHECK(elden_camera_native_cci_stop(11,7,0)==-110&&contexts[0]&&!contexts[0]->stopped);
    setup();CHECK(!elden_camera_native_cci_open(&c,0));elden_camera_cci_peer_fault(peers[0],1);
    CHECK(elden_camera_native_cci_stop(11,7,0)==-110&&elden_camera_cci_peer_stat(peers[0],1)==1);
    setup();CHECK(!elden_camera_native_cci_open(&c,0));actual_clock[0]=40000000;value=99;
    CHECK(elden_camera_native_cci_read(11,7,0,0x56,0,2,&value)==-95&&value==99); // real clock proof revoked
    // Entire original power/control/stream path feeds actual native + CCI,
    // not a direct sensor callback that hides command/packing/controller bugs.
    setup();CHECK(!elden_camera_native_cci_open(&c,0)&&!elden_camera_native_cci_open(&c,1));
    for(unsigned target=0;target<2;target++){
        Sensor s;s.target=target;EldenCameraIo io={&s,sensor_admit,resource,sensor_read,sensor_write,sensor_delay,receiver_ready,receiver_drain};
        CHECK(!elden_camera_bind(&s.core,&io,target));CHECK(!elden_camera_probe(&s.core)&&!s.rails&&!s.clock&&s.reset);
        CHECK(!elden_camera_start(&s.core)&&s.rails==7&&s.clock&&!s.reset&&elden_camera_cci_peer_stat(peers[target],2));
        CHECK(!elden_camera_control(&s.core,target?1000:100,target?2048:64));
        CHECK(elden_camera_cci_peer_stat(peers[target],4)==1&&elden_camera_cci_peer_stat(peers[target],3)>300);
        if(!target)CHECK(elden_camera_cci_peer_stat(peers[target],5)>0);
        s.drain_failed=true;unsigned rails=s.rails;CHECK(elden_camera_stop(&s.core)==-110&&s.rails==rails&&s.clock&&!elden_camera_terminal(&s.core));
        s.drain_failed=false;CHECK(!elden_camera_stop(&s.core)&&elden_camera_terminal(&s.core)&&!s.rails&&!s.clock&&s.reset&&!elden_camera_cci_peer_stat(peers[target],2));
        CHECK(!elden_camera_native_cci_stop(11,7,target));
    }
    setup();CHECK(!elden_camera_native_cci_open(&c,0)&&!elden_camera_native_cci_open(&c,1));
    for(unsigned target=0;target<2;target++){
        Sensor s;s.target=target;EldenCameraSensor bound={};
        EldenCameraSensorProvider p={&s,11,7,binding_admit,binding_resource,sensor_delay,binding_ready,binding_drain};
        CHECK(!elden_camera_sensor_bind(&bound,&p,target));
        CHECK(!elden_camera_sensor_probe(&bound)&&!s.rails&&!s.clock&&s.reset);
        CHECK(!elden_camera_sensor_start(&bound)&&elden_camera_cci_peer_stat(peers[target],2));
        CHECK(!elden_camera_sensor_control(&bound,target?1000:100,target?2048:64));
        s.drain_failed=true;CHECK(elden_camera_sensor_shutdown(&bound)==-110&&!bound.stopped&&contexts[target]->ready&&s.rails==7);
        s.drain_failed=false;CHECK(!elden_camera_sensor_shutdown(&bound)&&bound.stopped&&!contexts[target]->ready&&!s.rails&&!s.clock&&s.reset);
        CHECK(elden_camera_sensor_start(&bound)==-125);
    }
    clear();std::printf("PASS: %u actual native CCI scenarios/%u checks; map/clock/IRQ/controller isolation/fault/stop and complete source-derived front-rear control chain; OS/MMIO/sensor power/receiver modeled\n",scenarios,checks);return 0;
}
