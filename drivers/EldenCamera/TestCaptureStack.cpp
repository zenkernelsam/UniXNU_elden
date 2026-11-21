/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual sensor/Core/CCI/native pipeline and capture binding. Only OS MMIO,
 * GIC, physical sensor/DMA/power are explicit peers; no generated frames. */
#include "NativePipeline.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(v) do{checks++;if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
extern "C" {
void *elden_camera_cci_peer_create(unsigned,int);int elden_camera_cci_peer_read(void *,unsigned,uint32_t *);
int elden_camera_cci_peer_write(void *,unsigned,uint32_t);uint64_t elden_camera_cci_peer_now(void *);
void elden_camera_cci_peer_delay(void *,unsigned);unsigned elden_camera_cci_peer_stat(void *,unsigned);void elden_camera_cci_peer_destroy(void *);
void *elden_camera_pipeline_peer_create(unsigned,unsigned);int elden_camera_pipeline_peer_read(void *,unsigned,uint32_t,uint32_t *);
int elden_camera_pipeline_peer_write(void *,unsigned,uint32_t,uint32_t);void elden_camera_pipeline_peer_event(void *,int,int,uint64_t,uint64_t);
uint64_t elden_camera_pipeline_peer_now(void *);void elden_camera_pipeline_peer_delay(void *,unsigned);void elden_camera_pipeline_peer_destroy(void *);
}
using IOReturn=int;using IOLock=std::mutex;static uint32_t armcpu_gic_ready=1,gic[0x10000/4];static std::vector<IOLock *> locks;
static void *cci_peers[2],*capture_peers[2];static bool drain_fail,capture_irq_mode;static unsigned mappings;
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){CHECK(!capture_irq_mode);p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static bool ml_at_interrupt_context(){return capture_irq_mode;}
#include "TestIrqPeer.inc"
static void *IOMalloc(size_t bytes){return std::malloc(bytes);}static void IOFree(void *p,size_t){std::free(p);}
static void IODelay(unsigned us){for(auto p:cci_peers)if(p)elden_camera_cci_peer_delay(p,us);for(auto p:capture_peers)if(p)elden_camera_pipeline_peer_delay(p,us);}
static uint64_t now_us(void *){return elden_camera_cci_peer_now(cci_peers[0]);}
static uint64_t pipeline_now_us(void *){return elden_camera_pipeline_peer_now(capture_peers[0]);}
struct Handler{uint32_t id;void (*callback)(void *);void *context;};static std::vector<Handler> handlers;
static IOReturn armcpu_spi_attach(uint32_t id,bool edge,void (*callback)(void *),void *context,uint32_t *flags,uint64_t *router){
    CHECK(edge&&callback&&context);for(auto h:handlers)CHECK(h.id!=id);handlers.push_back({id,callback,context});gic[(0x100+id/32*4)/4]|=1u<<(id%32);*flags=0xa007;*router=0;return 0;}
static IOReturn armcpu_spi_mask(uint32_t id,bool on){if(on)gic[(0x100+id/32*4)/4]&=~(1u<<(id%32));else gic[(0x100+id/32*4)/4]|=1u<<(id%32);return 0;}
namespace {
constexpr uint64_t CciBase[2]={0xac7b000,0xac7c000};constexpr uint32_t CciIntid[2]={458,459};
struct Window {unsigned source=9;
    bool map(uint64_t pa,uint32_t size){mappings++;if(pa==0x16000000){CHECK(size==0x10000);source=2;return true;}
        CHECK((pa==CciBase[0]||pa==CciBase[1])&&size==0x1000);source=pa==CciBase[1];return true;}
    int read(uint32_t reg,uint32_t *out){if(source<2)return elden_camera_cci_peer_read(cci_peers[source],reg,out);*out=gic[reg/4];return 0;}
    int write(uint32_t reg,uint32_t value){if(source<2)return elden_camera_cci_peer_write(cci_peers[source],reg,value);CHECK(source==2&&reg>=0x280&&reg<0x300);gic[(reg-0x80)/4]&=~value;return 0;}
};
struct PipelineWindow {unsigned source=9,sensor=9;
    bool map(uint64_t pa,uint32_t size){mappings++;if(pa==0x16000000){CHECK(size==0x10000);source=3;return true;}
        if(pa==0xadaf000||pa==0xadb1000){source=0;sensor=pa==0xadb1000;CHECK(size==0x2000);return true;}
        if(pa==0xad27000||pa==0xad6d000){source=1;sensor=pa==0xad6d000;CHECK(size==(sensor?0xa00u:0x2b00u));return true;}
        if(pa==0xac86000||pa==0xad6e000){source=2;sensor=pa==0xad6e000;CHECK(size==(sensor?0x3000u:0x10000u));return true;}CHECK(false);return false;
    }
    int read(uint32_t reg,uint32_t *out){if(source<3)return elden_camera_pipeline_peer_read(capture_peers[sensor],source,reg,out);CHECK(source==3);*out=gic[reg/4];return 0;}
    int write(uint32_t reg,uint32_t value){if(source<3)return elden_camera_pipeline_peer_write(capture_peers[sensor],source,reg,value);CHECK(source==3&&reg>=0x280&&reg<0x300);gic[(reg-0x80)/4]&=~value;return 0;}
};
}
#include "NativeCciMethods.inc"
#include "NativePipelineMethods.inc"
struct SensorState {unsigned sensor=0,rails=0,clock=0,reset=1;};
static int cci_admit(void *,uint64_t owner,uint64_t epoch,unsigned controller,unsigned op){CHECK(owner==7&&epoch==9&&controller<2&&op<=2);return 0;}
static uint32_t cci_clock(void *,uint64_t owner,uint64_t epoch,unsigned controller){CHECK(owner==7&&epoch==9&&controller<2);return 37500000;}
static int pipeline_owner(void *,const EldenCameraPipelineConfig *cfg,unsigned op){CHECK(cfg->owner==7&&cfg->epoch==9&&op<=3);return 0;}
static int pipeline_owner_buffer(void *,const EldenCameraPipelineConfig *cfg,const EldenCameraBuffer *b,int complete){CHECK(b&&b->mapping&&cfg->owner==b->owner&&(complete==0||complete==1));return 0;}
static int pipeline_owner_drain(void *,const EldenCameraPipelineConfig *cfg){CHECK(!elden_camera_cci_peer_stat(cci_peers[cfg->sensor],2));return drain_fail?-16:0;}
static int sensor_admit(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){auto *s=(SensorState *)p;CHECK(s->sensor==sensor&&owner==7&&epoch==9&&op<=3);return 0;}
static int sensor_resource(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on){auto *s=(SensorState *)p;CHECK(owner==7&&epoch==9&&sensor==s->sensor&&r<=EldenCameraAvdd);
    if(r==EldenCameraReset)s->reset=(unsigned)on;else if(r==EldenCameraClock)s->clock=(unsigned)on;else if(on)s->rails|=1u<<(r-EldenCameraDovdd);else s->rails&=~(1u<<(r-EldenCameraDovdd));return 0;}
static int sensor_delay(void *,unsigned us){IODelay(us);return 0;}
int main(int argc,char **argv){CHECK(argc==2);drain_fail=std::strcmp(argv[1],"drain-fail")==0;
    for(unsigned i=0;i<2;i++){cci_peers[i]=elden_camera_cci_peer_create(i,1);capture_peers[i]=elden_camera_pipeline_peer_create(i,i?3:0);}
    EldenCameraCciProvider cci{nullptr,7,9,cci_admit,cci_clock};EldenCameraNativePipelineOwner owner{nullptr,pipeline_owner,pipeline_owner_buffer,pipeline_owner_drain};
    SensorState states[2];EldenCameraCapture captures[2]{};
    for(unsigned target=0;target<2;target++){
        states[target].sensor=target;CHECK(!elden_camera_native_cci_open(&cci,target));EldenCameraPipelineConfig cfg{};
        cfg.owner=7;cfg.epoch=9;cfg.route_epoch=11;cfg.sensor=target;cfg.decoder=target?3:0;cfg.timer_hz=400000000;
        cfg.mode={target?3264u:4080u,target?2448u:3072u,target?336000000u:672000000u,target?281600000u:558933333u,(uint16_t)(target?0x08a8:0x38ee),(uint16_t)(target?270:90),
            (uint8_t)target,(uint8_t)(target?0x31:0x56),(uint8_t)(target?7:11),(uint8_t)(target?4:3),4,10,(uint8_t)(target?EldenCameraRggb:EldenCameraGrbg),0};
        CHECK(!elden_camera_native_pipeline_open(&cfg,&owner));EldenCameraSensorProvider board{&states[target],7,9,sensor_admit,sensor_resource,sensor_delay,nullptr,nullptr};
        CHECK(!elden_camera_capture_bind(&captures[target],&board,target));CHECK(!elden_camera_capture_probe(&captures[target])&&!states[target].rails&&!states[target].clock&&states[target].reset);
        CHECK(!elden_camera_mode(&captures[target].sensor.core,&cfg.mode));
        EldenCameraPipelineStatus status{};CHECK(!elden_camera_native_pipeline_status(target,&status));
        for(unsigned i=0;i<3;i++){EldenCameraBuffer b{(uint64_t)i+1,7,9,11,UINT64_C(0x20000000)+target*0x8000000u+i*0x2000000u,status.frame_bytes,(void *)(uintptr_t)(i+1)};CHECK(!elden_camera_native_pipeline_queue(7,9,target,&b));}
        CHECK(!elden_camera_capture_start(&captures[target])&&states[target].rails==7&&states[target].clock&&!states[target].reset);
        CHECK(elden_camera_cci_peer_stat(cci_peers[target],2));elden_camera_pipeline_peer_event(capture_peers[target],1,1,100,110);
        CHECK(!elden_camera_native_pipeline_service(7,9,target));EldenCameraFrame frame{};CHECK(!elden_camera_native_pipeline_frame(7,9,target,&frame)&&frame.buffer.id==1&&!frame.error);
        if(drain_fail){CHECK(elden_camera_capture_stop(&captures[target])==-16&&states[target].rails==7&&states[target].clock);drain_fail=false;}
        CHECK(!elden_camera_capture_stop(&captures[target])&&!states[target].rails&&!states[target].clock&&states[target].reset);
        CHECK(!elden_camera_capture_stop(&captures[target]));
        CHECK(!elden_camera_native_pipeline_status(target,&status)&&status.stopped);CHECK(captures[target].sensor.stopped);
    }
    for(auto *p:locks)delete p;for(auto *p:contexts)delete p;for(auto *p:camera_pipelines)delete p;
    for(unsigned i=0;i<2;i++){elden_camera_cci_peer_destroy(cci_peers[i]);elden_camera_pipeline_peer_destroy(capture_peers[i]);}
    std::printf("PASS composed Gen4 front/rear capture %s: %u checks; actual sensor/CCI/native PHY-CSID-VFE binding; power/MMIO/GIC/DMA peers modeled\n",argv[1],checks);return 0;
}
