/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual NativePipelineMethods plus exact core, explicit MMIO/GIC/bus peers. */
#include "NativePipeline.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
static unsigned checks,maps,attached;static int deny,map_fail,attach_fail,drain_error;static uint32_t gic[0x10000/4];
static bool active_irq,irq_mode,rearm_edge,rearm_error,attach_irq;static uint32_t armcpu_gic_ready=1;static int mask_failure;
#define CHECK(v) do{checks++;if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using IOReturn=int;using IOLock=std::mutex;
static std::vector<IOLock *> locks;static void *peer;
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static bool ml_at_interrupt_context(){return irq_mode;}
#include "TestIrqPeer.inc"
static void *IOMalloc(size_t bytes){return std::malloc(bytes);}static void IOFree(void *p,size_t){std::free(p);}
extern "C" {
void *elden_camera_pipeline_peer_create(unsigned,unsigned);int elden_camera_pipeline_peer_read(void *,unsigned,uint32_t,uint32_t *);
int elden_camera_pipeline_peer_write(void *,unsigned,uint32_t,uint32_t);void elden_camera_pipeline_peer_event(void *,int,int,uint64_t,uint64_t);
uint64_t elden_camera_pipeline_peer_now(void *);void elden_camera_pipeline_peer_delay(void *,unsigned);void elden_camera_pipeline_peer_destroy(void *);
}
static void IODelay(unsigned us){elden_camera_pipeline_peer_delay(peer,us);}
struct Handler {unsigned id;void (*fn)(void *);void *context;};static std::vector<Handler> handlers;
static IOReturn armcpu_spi_attach(uint32_t id,bool edge,void (*fn)(void *),void *ctx,uint32_t *flags,uint64_t *router){
    CHECK(edge&&fn&&ctx);attached++;if(attach_fail==(int)attached)return -16;handlers.push_back({id,fn,ctx});gic[(0x100+id/32*4)/4]|=1u<<(id%32);*flags=0xa007;*router=0;
    if(attach_irq){attach_irq=false;irq_mode=true;fn(ctx);irq_mode=false;}return 0;}
static IOReturn armcpu_spi_mask(uint32_t id,bool on){
    CHECK(!camera_test_enrolled||irq_mode);if(mask_failure==(int)id)return -5;
    if(on)gic[(0x100+id/32*4)/4]&=~(1u<<(id%32));else{
        if(rearm_error)return -5;gic[(0x100+id/32*4)/4]|=1u<<(id%32);
        if(rearm_edge){rearm_edge=false;for(auto h:handlers)if(h.id==id){irq_mode=true;h.fn(h.context);irq_mode=false;}}
    }return 0;
}
namespace {
struct PipelineWindow {
    unsigned source=9;
    bool map(uint64_t pa,uint32_t bytes){maps++;if(map_fail==(int)maps)return false;
        if(pa==0x16000000){CHECK(bytes==0x10000);source=3;return true;}
        if(pa==0xadaf000||pa==0xadb1000){CHECK(bytes==0x2000);source=0;return true;}
        if(pa==0xad27000||pa==0xad6d000){CHECK(bytes==0x2b00||bytes==0xa00);source=1;return true;}
        if(pa==0xac86000||pa==0xad6e000){CHECK(bytes==0x10000||bytes==0x3000);source=2;return true;}CHECK(false);return false;
    }
    int read(uint32_t reg,uint32_t *out){CHECK(!irq_mode&&!camera_test_enrolled);if(source<3)return elden_camera_pipeline_peer_read(peer,source,reg,out);CHECK(source==3);
        if(active_irq&&reg>=0x300&&reg<0x380){*out=~0u;return 0;}*out=gic[reg/4];return 0;}
    int write(uint32_t reg,uint32_t value){CHECK(!irq_mode&&!camera_test_enrolled);if(source<3)return elden_camera_pipeline_peer_write(peer,source,reg,value);CHECK(source==3&&reg>=0x280&&reg<0x300);gic[(reg-0x80)/4]&=~value;return 0;}
};
static uint64_t pipeline_now_us(void *){return elden_camera_pipeline_peer_now(peer);}
}
#include "NativePipelineMethods.inc"
static int provider(void *,const EldenCameraPipelineConfig *c,unsigned op){CHECK(!irq_mode&&!camera_test_enrolled&&c->owner==7&&c->epoch==9&&op<=3);return deny?-13:0;}
static int buffer(void *,const EldenCameraPipelineConfig *,const EldenCameraBuffer *b,int complete){CHECK(b&&b->mapping&&(complete==0||complete==1));return 0;}
static int drain(void *,const EldenCameraPipelineConfig *){return drain_error;}
static unsigned event_sensor;
static void irq_event(){
    elden_camera_pipeline_peer_event(peer,1,1,200,210);
    auto h=handlers[1];gic[(0x300+h.id/32*4)/4]|=1u<<(h.id%32);irq_mode=true;h.fn(h.context);irq_mode=false;
    gic[(0x300+h.id/32*4)/4]&=~(1u<<(h.id%32));
}
static void stop_during_wait(){camera_test_enrolled=false;CHECK(!elden_camera_native_pipeline_stop(7,9,event_sensor));camera_test_enrolled=true;}
static EldenCameraPipelineConfig config(unsigned sensor,unsigned decoder){EldenCameraPipelineConfig c{};c.owner=7;c.epoch=9;c.route_epoch=11;c.sensor=sensor;c.decoder=decoder;c.timer_hz=400000000;
    c.mode={sensor?3264u:4080u,sensor?2448u:3072u,sensor?336000000u:672000000u,sensor?281600000u:558933333u,(uint16_t)(sensor?0x08a8:0x38ee),(uint16_t)(sensor?270:90),
        (uint8_t)sensor,(uint8_t)(sensor?0x31:0x56),(uint8_t)(sensor?7:11),(uint8_t)(sensor?4:3),4,10,(uint8_t)(sensor?EldenCameraRggb:EldenCameraGrbg),0};return c;}
int main(int argc,char **argv){CHECK(argc==2);std::string test=argv[1];bool front=test=="front"||test=="irq-front";unsigned sensor=front?1:0,decoder=front?3:0;event_sensor=sensor;peer=elden_camera_pipeline_peer_create(sensor,decoder);
    auto cfg=config(sensor,decoder);EldenCameraNativePipelineOwner own{nullptr,provider,buffer,drain};
    if(test=="map-fail")map_fail=3;if(test=="attach-fail")attach_fail=2;if(test=="deny")deny=1;if(test=="irq-during-attach")attach_irq=true;
    int opened=elden_camera_native_pipeline_open(&cfg,&own);
    if(test=="map-fail"||test=="attach-fail"||test=="deny"||test=="irq-during-attach"){
        CHECK(opened<0);if(test=="deny")CHECK(maps==0&&attached==0);
        if(test=="irq-during-attach"){CHECK(opened==-125&&camera_test_wakes==1);auto id=handlers[0].id;CHECK(!(gic[(0x100+id/32*4)/4]&(1u<<(id%32))));}
    }
    else {
        CHECK(!opened&&maps==4&&attached==3);CHECK(!elden_camera_native_pipeline_prepared(7,9,sensor));
        CHECK(elden_camera_native_pipeline_prepared(7,10,sensor)==-19);CHECK(elden_camera_native_pipeline_open(&cfg,&own)==-16);
        EldenCameraPipelineStatus s{};CHECK(!elden_camera_native_pipeline_status(sensor,&s));
        for(unsigned i=0;i<3;i++){EldenCameraBuffer b{(uint64_t)i+1,7,9,11,UINT64_C(0x20000000)+0x2000000*i,s.frame_bytes,(void *)(uintptr_t)(i+1)};CHECK(!elden_camera_native_pipeline_queue(7,9,sensor,&b));}
        CHECK(!elden_camera_native_pipeline_start(7,9,sensor)&&!elden_camera_native_pipeline_ready(7,9,sensor));
        if(test=="unexpected-irq"){irq_mode=true;handlers[0].fn(handlers[0].context);irq_mode=false;CHECK(elden_camera_native_pipeline_ready(7,9,sensor)==-125);CHECK(!elden_camera_native_pipeline_status(sensor,&s)&&s.error==(uint32_t)-71);}
        else {
            bool listening=test.rfind("irq-",0)==0;
            if(listening){
                CHECK(!elden_camera_native_pipeline_listen(7,9,sensor)&&camera_test_refs==1);
                CHECK(elden_camera_native_pipeline_listen(7,9,sensor)==-16);
                CHECK(elden_camera_native_pipeline_wait(7,10,sensor,20)==-19);
                CHECK(elden_camera_native_pipeline_wait(7,9,sensor,0)==-22);
                CHECK(elden_camera_native_pipeline_wait(7,9,sensor,5001)==-22);
                camera_test_thread=(void *)2;CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==-13);
                CHECK(elden_camera_native_pipeline_service(7,9,sensor)==-13);camera_test_thread=(void *)1;
                CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==1);
                CHECK(!elden_camera_native_pipeline_service(7,9,sensor));
                CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==0&&camera_test_blocks==1);
                if(test=="irq-before-sleep")camera_test_wait_hook=irq_event;
                else if(test=="irq-stop-wait")camera_test_wait_hook=stop_during_wait;
                else if(test=="irq-during-sleep")camera_test_block_hook=irq_event;
                else irq_event();
                if(test=="irq-stop-wait"){
                    CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==-125&&camera_test_clears==1);
                    CHECK(!elden_camera_native_pipeline_status(sensor,&s)&&s.stopped);goto finished;
                }
                // During assert_wait the primary IRQ may only mask/latch/wake,
                // so allow the IRQ-safe mask peer but no provider or MMIO.
                CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==1);
                camera_test_wait_hook=nullptr;camera_test_block_hook=nullptr;
                if(test=="irq-before-sleep")CHECK(camera_test_clears==1&&camera_test_blocks==1);
                if(test=="irq-during-sleep")CHECK(camera_test_blocks==2);
                uint32_t id=handlers[1].id;gic[(0x200+id/32*4)/4]|=1u<<(id%32);
                if(test=="irq-rearm-race")rearm_edge=true;
                if(test=="irq-rearm-fail")rearm_error=true;
                if(test=="irq-mask-fail")mask_failure=(int)handlers[1].id;
                int rc=elden_camera_native_pipeline_service(7,9,sensor);
                if(test=="irq-rearm-fail"){
                    CHECK(rc==-5&&elden_camera_native_pipeline_wait(7,9,sensor,20)==-125);
                    rearm_error=false;CHECK(!elden_camera_native_pipeline_stop(7,9,sensor));goto finished;
                }
                if(test=="irq-mask-fail"){
                    CHECK(rc==-5&&elden_camera_native_pipeline_wait(7,9,sensor,20)==-125);mask_failure=0;
                    CHECK(!elden_camera_native_pipeline_stop(7,9,sensor));goto finished;
                }
                CHECK(!rc&&(gic[(0x200+id/32*4)/4]&(1u<<(id%32)))); // real pending edge was not cleared
                if(test=="irq-rearm-race"){
                    CHECK(elden_camera_native_pipeline_wait(7,9,sensor,20)==1);
                    CHECK(!elden_camera_native_pipeline_service(7,9,sensor));
                }
            }
            else elden_camera_pipeline_peer_event(peer,1,1,100,110);
            CHECK(!elden_camera_native_pipeline_service(7,9,sensor));EldenCameraFrame f{};
            CHECK(!elden_camera_native_pipeline_frame(7,9,sensor,&f)&&f.buffer.id==1&&!f.error&&f.sequence==0);
            if(test=="drain-fail"){drain_error=-16;CHECK(elden_camera_native_pipeline_stop(7,9,sensor)==-16);drain_error=0;}
            if(test=="active-irq"||test=="irq-active-stop"){active_irq=true;CHECK(elden_camera_native_pipeline_stop(7,9,sensor)==-110);active_irq=false;
                CHECK(elden_camera_native_pipeline_ready(7,9,sensor)==-125);CHECK(!elden_camera_native_pipeline_status(sensor,&s)&&!s.stopped);}
            CHECK(!elden_camera_native_pipeline_stop(7,9,sensor));CHECK(!elden_camera_native_pipeline_stop(7,9,sensor));
            CHECK(!elden_camera_native_pipeline_status(sensor,&s)&&s.stopped);
        }
    }
finished:
    for(auto *p:locks)delete p;for(auto *p:camera_pipelines)delete p;elden_camera_pipeline_peer_destroy(peer);
    std::printf("PASS native camera pipeline %s: %u checks; actual core/adapter, MMIO/GIC/power/DMA peers modeled\n",test.c_str(),checks);return 0;
}
