/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual capture session lifecycle over real Board/DMA/output/service methods.
 * Only OS MMIO/GIC/IRQ/IOKit/physical sensor/power peers are modeled. */
#include "CaptureSession.h"
#include "NativePipeline.h"
#include "PowerBinding.h"
#include "Board.h"
#include "DmaBinding.h"
#include "FrameOutput.h"
#include "Platform.h"
#include "Aw99706.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(v) do{checks++;if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
extern "C" {
EldenCameraNativeDma *elden_camera_dma_peer_open(void);void elden_camera_dma_peer_fault(unsigned);unsigned elden_camera_dma_peer_completion_count(void);void elden_camera_dma_peer_reset(void);
void elden_camera_clock_peer_setup(void);int elden_camera_clock_peer_read(unsigned,uint32_t,uint32_t *);int elden_camera_clock_peer_write(unsigned,uint32_t,uint32_t);
void elden_camera_clock_peer_delay(unsigned);uint64_t elden_camera_clock_peer_now(void);EldenCameraClockIo elden_camera_clock_peer_io(void);
int elden_camera_clock_peer_reset(unsigned);int elden_camera_clock_peer_mclk(unsigned);
void *elden_camera_i2c10_peer_create(EldenBlPlatform *);unsigned elden_camera_i2c10_peer_stat(void *,unsigned);void elden_camera_i2c10_peer_destroy(void *);
void *elden_camera_cci_peer_create(unsigned,int);int elden_camera_cci_peer_read(void *,unsigned,uint32_t *);
int elden_camera_cci_peer_write(void *,unsigned,uint32_t);uint64_t elden_camera_cci_peer_now(void *);
void elden_camera_cci_peer_delay(void *,unsigned);unsigned elden_camera_cci_peer_stat(void *,unsigned);void elden_camera_cci_peer_destroy(void *);
void *elden_camera_pipeline_peer_create(unsigned,unsigned);int elden_camera_pipeline_peer_read(void *,unsigned,uint32_t,uint32_t *);
int elden_camera_pipeline_peer_write(void *,unsigned,uint32_t,uint32_t);void elden_camera_pipeline_peer_event(void *,int,int,uint64_t,uint64_t);
uint64_t elden_camera_pipeline_peer_now(void *);void elden_camera_pipeline_peer_delay(void *,unsigned);void elden_camera_pipeline_peer_destroy(void *);
void elden_camera_output_peer_fail_alloc(unsigned,unsigned);void elden_camera_output_peer_cleanup(void);void elden_camera_output_peer_interrupt(int);
}
using IOReturn=int;using IOLock=std::mutex;static uint32_t armcpu_gic_ready=1,gic[0x10000/4];static std::vector<IOLock *> locks;
static void *cci_peers[2],*capture_peers[2];static bool drain_fail,capture_irq_mode;static unsigned mappings;
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){CHECK(!capture_irq_mode);p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static void IOLockFree(IOLock *p){for(unsigned i=0;i<locks.size();i++)if(locks[i]==p){locks[i]=locks.back();locks.pop_back();break;}delete p;}
static bool ml_at_interrupt_context(){return capture_irq_mode;}
#include "TestIrqPeer.inc"
static void *IOMalloc(size_t bytes){return std::malloc(bytes);}static void IOFree(void *p,size_t){std::free(p);}
static void IODelay(unsigned us){elden_camera_clock_peer_delay(us);for(auto p:cci_peers)if(p)elden_camera_cci_peer_delay(p,us);for(auto p:capture_peers)if(p)elden_camera_pipeline_peer_delay(p,us);}
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
#define Context CciContext
#include "NativeCciMethods.inc"
#undef Context
#include "NativePipelineMethods.inc"
static void camera_frame_irq(unsigned sensor){
    uint32_t id=sensor?637u:633u,word=id/32*4,bit=1u<<(id%32);bool found=false;
    CHECK(gic[(0x100+word)/4]&bit);
    for(auto handler:handlers)if(handler.id==id){
        found=true;gic[(0x300+word)/4]|=bit;capture_irq_mode=true;
        handler.callback(handler.context);capture_irq_mode=false;gic[(0x300+word)/4]&=~bit;
    }CHECK(found&&!(gic[(0x100+word)/4]&bit));
}
namespace {
struct CameraClockWindow {unsigned space=9;
    bool map(uint64_t pa,uint32_t bytes){mappings++;if(pa==0xade0000){CHECK(bytes==0x20000);space=0;}
        else if(pa==0x1760000){CHECK(bytes==0x6000);space=1;}else{CHECK(pa==0xf100000&&bytes==0x78000);space=2;}return true;}
    int read(uint32_t reg,uint32_t *out){return elden_camera_clock_peer_read(space,reg,out);}
    int write(uint32_t reg,uint32_t value){return elden_camera_clock_peer_write(space,reg,value);}
};
static uint64_t camera_clock_now(void *){return elden_camera_clock_peer_now();}
}
#include "NativeClockMethods.inc"
namespace {
static unsigned lease=1;
struct Context {EldenBlPlatform platform{};IOLock *serial=nullptr;uint32_t shared_camera_refs=0;
    bool shared_closing=false,shared_published=false,shared_released=false;};
}
#include "SharedI2c10Methods.inc"
using SharedContext=Context;
struct SensorState {unsigned sensor=0,rails=0,clock=0,reset=1;};
static SensorState *sensor_states;static EldenCameraPowerSession shared_power;static void *i2c_peer;

static int pipeline_owner_buffer(void *,const EldenCameraPipelineConfig *cfg,const EldenCameraBuffer *b,int complete){CHECK(b&&b->mapping&&cfg->owner==b->owner&&(complete==0||complete==1));return 0;}
static int pipeline_owner_drain(void *,const EldenCameraPipelineConfig *cfg){CHECK(!elden_camera_cci_peer_stat(cci_peers[cfg->sensor],2));return drain_fail&&cfg->sensor==0?-16:0;}
static int power_admit(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int power_inactive(void *,uint64_t owner,uint64_t epoch,unsigned sensor){CHECK(owner==7&&epoch==9&&sensor<2);
    if(!sensor_states||!elden_camera_clock_peer_reset(sensor)||elden_camera_clock_peer_mclk(sensor)||elden_camera_cci_peer_stat(cci_peers[sensor],2))return -16;
    EldenCameraPipelineStatus status{};int rc=elden_camera_native_pipeline_status(sensor,&status);
    if(rc)return rc==-19?0:rc;
    return status.state==EldenPipeReady||status.stopped?0:-16;}
static int power_unused(void *,uint64_t owner,uint64_t epoch,unsigned ldo){CHECK(owner==7&&epoch==9&&ldo>=1&&ldo<=7);return 0;} // model has no lens consumer
static int power_delay(void *,unsigned us){CHECK(us==150);IODelay(us);return 0;}
static unsigned receiver_refs[2];
static int receiver_resources(void *,const EldenCameraPipelineConfig *cfg,int action){CHECK(cfg->owner==7&&cfg->epoch==9&&cfg->sensor<2);
    if(action==1){CHECK(!receiver_refs[cfg->sensor]);receiver_refs[cfg->sensor]=1;}
    else if(action==2)CHECK(receiver_refs[cfg->sensor]);else{CHECK(!elden_camera_cci_peer_stat(cci_peers[cfg->sensor],2));receiver_refs[cfg->sensor]=0;}return 0;}
static int deny_stream,deny_prepare;
static int board_admit(void *,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){CHECK(owner==7&&epoch==9&&sensor<2&&op<=3);
    if(deny_prepare&&op==EldenCameraAcquire&&sensor==1)return -13;
    if(deny_stream&&op==EldenCameraStream)return -13;return 0;}
static int board_delay(void *,unsigned us){IODelay(us);return 0;}

using UInt32=uint32_t;using IOOptionBits=uint32_t;using task_t=void *;
enum{kIOReturnSuccess=0,kIOReturnBusy=1,kIOReturnBadArgument=2,kIOReturnNotPermitted=3,kIOReturnNotFound=4,kIOReturnNotReady=5,kIOReturnNoMemory=6,kIOReturnIOError=7,kIOReturnError=8,kIOMapReadOnly=0x100};
struct OSObject{virtual ~OSObject()=default;};struct OSDictionary:OSObject{};struct IOExternalMethodDispatch{};
struct IOExternalMethodArguments{uint32_t scalarInputCount=0,scalarOutputCount=0;const uint64_t *scalarInput=nullptr;uint64_t *scalarOutput=nullptr;
    size_t structureInputSize=0,structureOutputSize=0;void *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;};
class IOUserClient;
static int fail_service_start;
class IOService:public OSObject{public:virtual bool start(IOService *){return fail_service_start?false:true;}virtual void stop(IOService *){}
    virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **){return 0;}virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **){return 0;}
    virtual void free(){}bool init(){return true;}bool attach(IOService *){return true;}void detach(IOService *){}void release(){delete this;}
    void setName(const char *){}bool setProperty(const char *,unsigned,unsigned){return true;}bool setProperty(const char *,const char *){return true;}void registerService(){}
    static IOService *getPlatform(){static IOService s;return &s;}};
static int privilege;
class IOUserClient:public IOService{public:virtual IOReturn clientClose(){return 0;}virtual IOReturn clientMemoryForType(UInt32,IOOptionBits *,class IOMemoryDescriptor **){return 0;}
    virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *){return 0;}
    bool initWithTask(task_t,void *,UInt32,OSDictionary *){return true;}static IOReturn clientHasPrivilege(task_t,const char *){return privilege;}void terminate(){}
};
class IOMemoryDescriptor{};static const char *kIOClientPrivilegeAdministrator="root";
class OSMetaClass{public:static void *preModLoad(const char *){return (void *)1;}static int postModLoad(void *){return 0;}};
static struct {const char *name;} g_kernel_kmod_info={"kernel"};static void elden_camera_service_static_init(){}
static std::vector<IOService *> retained_services;
template<class T> static T *service_alloc(){T *p=new T;retained_services.push_back(p);return p;}
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSTypeAlloc(x) (::service_alloc<x>())
#include "ServiceMethods.inc"

int main(int argc,char **argv){CHECK(argc==2);
    bool partial=std::strcmp(argv[1],"partial-start")==0,publish_fail=std::strcmp(argv[1],"publish-fail")==0;
    bool irq_timeout=std::strcmp(argv[1],"irq-timeout")==0;drain_fail=std::strcmp(argv[1],"drain-fail")==0;
    bool partial_open=std::strcmp(argv[1],"partial-open")==0,partial_prepare=std::strcmp(argv[1],"partial-prepare")==0;
    bool client_close=std::strcmp(argv[1],"client-close")==0,early_stop=std::strcmp(argv[1],"early-stop")==0;
    bool close_gate=std::strcmp(argv[1],"close-gate")==0,normal=!std::strcmp(argv[1],"normal");
    bool failed=partial||partial_open||partial_prepare||early_stop;
    CHECK(partial||publish_fail||irq_timeout||drain_fail||partial_open||partial_prepare||client_close||early_stop||close_gate||!std::strcmp(argv[1],"normal"));
    for(unsigned i=0;i<2;i++){cci_peers[i]=elden_camera_cci_peer_create(i,1);capture_peers[i]=elden_camera_pipeline_peer_create(i,i?3:0);}
    uint64_t retained_tickets[2]={};EldenCameraDmaCapture capture{};EldenCameraNativeDma *dma=elden_camera_dma_peer_open();EldenCameraDmaView memory{};CHECK(!elden_camera_dma_view(dma,7,9,&memory));
    SensorState states[2];sensor_states=states;
    SharedContext shared_context;shared_context.serial=IOLockAlloc();i2c_peer=elden_camera_i2c10_peer_create(&shared_context.platform);
    CHECK(!elden_bl_platform_acquire(&shared_context.platform));shared_bus_publish(&shared_context);
    EldenCameraPowerOwner power_owner{nullptr,7,9,power_admit,power_inactive,power_delay,power_unused};
    CHECK(!elden_camera_power_session_open(&shared_power,&power_owner));
    EldenAw99706 backlight{};EldenBacklightIo backlight_io{&shared_context,shared_backlight_read,shared_backlight_write};
    CHECK(!elden_aw99706_inspect(&backlight,&backlight_io));CHECK(!elden_aw99706_set_level(&backlight,511));
    CHECK(!elden_aw99706_set_level(&backlight,49));
    elden_camera_clock_peer_setup();auto cio=elden_camera_clock_peer_io();
    EldenCameraNativeClockOwner clock_owner{cio.context,7,9,cio.admit,cio.lock,cio.controllers,cio.dependency,cio.consumer_idle,cio.sensor_idle,cio.root_unused,cio.domain_unused,cio.pins_unused};
    EldenCameraNativeClocks *native_clock=nullptr;CHECK(!elden_camera_native_clocks_open(&clock_owner,&native_clock));
    CHECK(!elden_camera_native_pins_prepare(native_clock,7,9));
    EldenCameraBoardOwner board_owner{nullptr,7,9,board_admit,board_delay,pipeline_owner_drain,pipeline_owner_buffer,receiver_resources};
    CHECK(!elden_camera_dma_capture_bind(&capture,dma,native_clock,&shared_power,&board_owner));
    EldenCameraCaptureSession session{};CHECK(elden_camera_capture_session_bind(nullptr,&capture)==-22);
    CHECK(!elden_camera_capture_session_bind(&session,&capture)&&session.owner==7&&session.epoch==9&&session.route_epoch==11);
    CHECK(elden_camera_capture_session_bind(&session,&capture)==-22);
    /* Sensor indices outside front/rear refuse before any provider step. */
    CHECK(elden_camera_capture_session_begin(&session,2,nullptr,nullptr,0)==-22);
    CHECK(elden_camera_capture_session_serve(&session,2,20)==-22);
    CHECK(elden_camera_capture_session_publish(&session,2)==-22);
    CHECK(elden_camera_capture_session_stop(&session,2)==-22);
    if(early_stop){CHECK(!elden_camera_capture_session_stop(&session,1)&&session.stopped[1]);}
    EldenCameraBoardClockPlan plan{};for(unsigned i=0;i<EldenCamClockCount;i++)plan.hz[i]=400000000;
    plan.hz[EldenCamClockCpasAhb]=80000000;plan.hz[EldenCamClockVfeLiteAhb]=80000000;
    plan.hz[EldenCamClockCpasFastAhb]=300000000;plan.hz[EldenCamClockVfe0FastAhb]=plan.hz[EldenCamClockVfe1FastAhb]=plan.hz[EldenCamClockVfe2FastAhb]=300000000;
    plan.hz[EldenCamClockVfe0]=plan.hz[EldenCamClockVfe1]=plan.hz[EldenCamClockVfe2]=480000000;
    plan.hz[EldenCamClockCpasVfe0]=plan.hz[EldenCamClockCpasVfe1]=plan.hz[EldenCamClockCpasVfe2]=480000000;
    plan.hz[EldenCamClockCamnocNrtAxi]=plan.hz[EldenCamClockCamnocRtAxi]=200000000;plan.hz[EldenCamClockQdssDebugXo]=19200000;
    for(unsigned target=0;target<2;target++){
        states[target].sensor=target;EldenCameraPipelineConfig cfg{};
        cfg.owner=7;cfg.epoch=9;cfg.route_epoch=11;cfg.sensor=target;cfg.decoder=target?3:0;cfg.timer_hz=400000000;
        cfg.mode={target?3264u:4080u,target?2448u:3072u,target?336000000u:672000000u,target?281600000u:558933333u,(uint16_t)(target?0x08a8:0x38ee),(uint16_t)(target?270:90),
            (uint8_t)target,(uint8_t)(target?0x31:0x56),(uint8_t)(target?7:11),(uint8_t)(target?4:3),4,10,(uint8_t)(target?EldenCameraRggb:EldenCameraGrbg),0};
        if(target==0){auto bad=plan;bad.hz[EldenCamClockCsiphy3Timer]=0;
            CHECK(elden_camera_dma_capture_prepare(&capture,&cfg,&bad)==-22&&!receiver_refs[0]&&!capture.board.attempted[0]);
            bad=plan;bad.hz[EldenCamClockCpasAhb]=400000000;
            CHECK(elden_camera_dma_capture_prepare(&capture,&cfg,&bad)==-34&&!receiver_refs[0]&&!capture.board.attempted[0]);}
        if(partial&&target)deny_stream=1;
        if(partial_prepare&&target)deny_prepare=1;
        if(partial_open&&target)elden_camera_output_peer_fail_alloc(0,2);
        if(normal){
            /* A config bound to a foreign sensor/owner/epoch/route_epoch is
             * refused before the board is asked for anything; none of these
             * latches session error or marks the sensor. */
            EldenCameraPipelineConfig foreign=cfg;foreign.sensor^=1;
            CHECK(elden_camera_capture_session_begin(&session,target,&foreign,&plan,2)==-116);
            foreign=cfg;foreign.owner=1;CHECK(elden_camera_capture_session_begin(&session,target,&foreign,&plan,2)==-116);
            foreign=cfg;foreign.epoch=1;CHECK(elden_camera_capture_session_begin(&session,target,&foreign,&plan,2)==-116);
            foreign=cfg;foreign.route_epoch=1;CHECK(elden_camera_capture_session_begin(&session,target,&foreign,&plan,2)==-116);
            if(target)CHECK(elden_camera_capture_session_publish(&session,target)==-19);}
        int rc=elden_camera_capture_session_begin(&session,target,&cfg,&plan,2);
        if(partial&&target){
            CHECK(rc==-13&&session.error==-13&&session.prepared[1]&&session.probed[1]&&session.queued[1]&&!session.started[1]&&!session.opened[1]);
            CHECK(elden_camera_capture_session_begin(&session,1,&cfg,&plan,2)==-13);
            CHECK(elden_camera_capture_session_serve(&session,1,20)==-19);
            CHECK(elden_camera_capture_session_publish(&session,1)==-13);
            deny_stream=0;continue;
        }
        if(partial_prepare&&target){
            // The acquire authority denial leaves the board itself clean: the
            // session latches its own error while sensor0 keeps serving.
            CHECK(rc==-13&&session.error==-13&&!session.prepared[1]&&!session.output[1]&&!session.opened[1]&&!session.listening[1]);
            CHECK(!capture.board.error);
            CHECK(elden_camera_capture_session_begin(&session,1,&cfg,&plan,2)==-13);
            CHECK(elden_camera_capture_session_serve(&session,1,20)==-19);
            CHECK(elden_camera_capture_session_publish(&session,1)==-13);
            deny_prepare=0;continue;
        }
        if(partial_open&&target){
            // output_open failed at IOLock allocation after assigning *out:
            // the retained lockless partial object must stay explicit and
            // must not block this sensor's or the peer's teardown.
            CHECK(rc==-12&&session.error==-12&&session.output[1]&&!session.opened[1]&&!session.listening[1]);
            CHECK(session.prepared[1]&&session.probed[1]&&session.queued[1]&&session.started[1]);
            CHECK(elden_camera_capture_session_begin(&session,1,&cfg,&plan,2)==-12);
            CHECK(elden_camera_capture_session_serve(&session,1,20)==-19);
            CHECK(elden_camera_capture_session_publish(&session,1)==-12);
            continue;
        }
        if(early_stop&&target){CHECK(rc==-125&&session.stopped[1]&&!session.prepared[1]&&!session.output[1]&&!session.opened[1]);continue;}
        CHECK(!rc&&session.prepared[target]&&session.probed[target]&&session.queued[target]&&session.started[target]&&session.opened[target]&&session.listening[target]);
        if(normal)CHECK(elden_camera_capture_session_begin(&session,target,&cfg,&plan,2)==-16);
        CHECK((elden_camera_i2c10_peer_stat(i2c_peer,6)&0x40)&&elden_camera_clock_peer_mclk(target)&&!elden_camera_clock_peer_reset(target));
        EldenCameraPipelineStatus status{};CHECK(!elden_camera_native_pipeline_status(target,&status)&&status.started&&status.state==EldenPipeStreaming);
        if(publish_fail&&target==0)fail_service_start=1;
        int pub=elden_camera_capture_session_publish(&session,target);
        if(publish_fail&&target==0){
            CHECK(pub==-19&&!session.published[0]);fail_service_start=0;
            // The failed publish already committed outputs[0]; a retry is a
            // permanent -16 duplicate, and admission stays honestly closed.
            CHECK(elden_camera_capture_session_publish(&session,0)==-16&&!session.published[0]);
        }else CHECK(!pub&&session.published[target]);
    }
    for(unsigned target=0;target<2;target++){
        if(failed&&target)continue;
        CHECK(elden_camera_capture_session_serve(&session,target,20)==(partial?-13:-11));
        if(irq_timeout)CHECK(elden_camera_capture_session_serve(&session,target,20)==-11);
    }
    unsigned events=irq_timeout?2:3;unsigned completion_base=elden_camera_dma_peer_completion_count();
    for(unsigned target=0;target<2;target++){
        if(failed&&target)continue;
        // In partial-start the failed target latches the shared board error;
        // every capture admit then fails honestly at the real provider gate
        // and the CSID line stays masked. The other partial paths leave the
        // board clean, so the surviving sensor still serves real frames.
        for(unsigned number=1;number<=events;number++){
            if(partial){CHECK(elden_camera_capture_session_serve(&session,0,20)==-13);break;}
            elden_camera_pipeline_peer_event(capture_peers[target],1,1,100+100*number+target,110+100*number+target);camera_frame_irq(target);
            CHECK(!elden_camera_capture_session_serve(&session,target,20));
            EldenCameraOutputFrame frame{};
            CHECK(!elden_camera_output_acquire(session.output[target],&frame)&&frame.sensor==target&&frame.bytes==memory.slots[target?3:0].buffer.bytes&&frame.sequence==number-1);
            if(number==events-1)retained_tickets[target]=frame.ticket;else CHECK(!elden_camera_output_release(session.output[target],frame.ticket));
        }
    }
    if(client_close){
        elden_camera_pipeline_peer_event(capture_peers[0],1,1,500,510);camera_frame_irq(0);
        CHECK(!elden_camera_capture_session_serve(&session,0,20));
        EldenCameraService service;service.use(0);IOUserClient *user=nullptr;privilege=kIOReturnNotPermitted;
        CHECK(service.newUserClient((void *)1,nullptr,0,&user)==kIOReturnNotPermitted&&!user);privilege=0;
        CHECK(service.newUserClient((void *)1,nullptr,0,&user)==0&&user);
        uint64_t values[12]={};IOExternalMethodArguments args{};args.scalarOutput=values;args.scalarOutputCount=12;
        CHECK(user->externalMethod(EldenCameraServiceAcquire,&args,nullptr,nullptr,nullptr)==0&&values[0]);
        IOMemoryDescriptor *mapped=nullptr;IOOptionBits options=0;
        CHECK(user->clientMemoryForType((UInt32)values[1],&options,&mapped)==kIOReturnSuccess&&options==kIOMapReadOnly&&mapped);
        // clientClose withdraws admission and returns only its own ticket;
        // the output stays live until the serialized owner retires it, and
        // the still-held consumer ticket remains returnable.
        CHECK(user->clientClose()==kIOReturnSuccess);
        EldenCameraOutputStatus live{};CHECK(!elden_camera_output_status(session.output[0],&live)&&live.stopping&&!live.retired&&live.held==1);
        IOUserClient *next=nullptr;CHECK(service.newUserClient((void *)2,nullptr,0,&next)==kIOReturnNotReady&&!next);
        user->free();
    }
    CHECK(partial||elden_camera_dma_peer_completion_count()==completion_base+events*(failed?1u:2u)+(client_close?1u:0u));
    if(drain_fail){
        // Shutdown closes every opened output before any physical stop; the
        // failing first drain neither strands the peer's admission close nor
        // skips its physical stop. State reports retained, not terminal.
        int rc=elden_camera_capture_session_shutdown(&session);
        CHECK(rc==-16&&session.closed[0]&&session.closed[1]&&!session.stopped[0]&&session.stopped[1]);
        EldenCameraPipelineStatus live{};CHECK(!elden_camera_native_pipeline_status(0,&live)&&!live.stopped);
        EldenCameraPipelineStatus drained{};CHECK(!elden_camera_native_pipeline_status(1,&drained)&&drained.stopped);
        CHECK(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x40&&elden_camera_clock_peer_mclk(0));
        drain_fail=false;CHECK(shared_bus_release(&shared_context)==-16&&lease);
        CHECK(!elden_camera_capture_session_shutdown(&session)&&session.shutdown_done);
        EldenCameraPipelineStatus final0{};CHECK(!elden_camera_native_pipeline_status(0,&final0)&&final0.stopped);
    }else if(close_gate){
        // A fully opened output refusing its close (here: interrupt-context
        // rejection inside the real output owner) gates all physical
        // teardown: both closes were attempted, neither drain ran. Retry at
        // thread context completes the same ordered shutdown.
        elden_camera_output_peer_interrupt(1);int rc=elden_camera_capture_session_shutdown(&session);elden_camera_output_peer_interrupt(0);
        CHECK(rc==-22&&!session.closed[0]&&!session.closed[1]&&!session.stopped[0]&&!session.stopped[1]);
        EldenCameraPipelineStatus live{};CHECK(!elden_camera_native_pipeline_status(0,&live)&&!live.stopped);
        CHECK(shared_bus_release(&shared_context)==-16&&lease);
        CHECK(!elden_camera_capture_session_shutdown(&session)&&session.shutdown_done&&session.stopped[0]&&session.stopped[1]);
    }else{
        for(unsigned target=0;target<2;target++){
            int rc=elden_camera_capture_session_stop(&session,target);
            CHECK(!rc&&session.stopped[target]&&(!session.opened[target]||session.closed[target]));
            /* A repeated stop is idempotent: the closed output is not closed
             * again and the settled board stop is not replayed. */
            CHECK(!elden_camera_capture_session_stop(&session,target));
            if(!capture.board.captures[target].initialized)continue;
            EldenCameraPipelineStatus status{};CHECK(!elden_camera_native_pipeline_status(target,&status)&&status.stopped);
            CHECK(capture.board.captures[target].capture.sensor.stopped);
        }
        CHECK(shared_bus_release(&shared_context)==-16&&lease);elden_camera_dma_peer_fault(2);CHECK(elden_camera_capture_session_shutdown(&session)==-16);
        elden_camera_dma_peer_fault(0);CHECK(!elden_camera_capture_session_shutdown(&session)&&session.shutdown_done);
    }
    EldenCameraDmaStatus dma_status{};CHECK(!elden_camera_dma_status(dma,&dma_status)&&dma_status.retired);
    for(unsigned i=0;i<2;i++){
        if(!session.opened[i])continue;CHECK(session.retired[i]);
        EldenCameraOutputStatus status{};CHECK(!elden_camera_output_status(session.output[i],&status)&&status.retired);
        if(retained_tickets[i]){CHECK(status.held==1);CHECK(!elden_camera_output_release(session.output[i],retained_tickets[i]));}
        CHECK(elden_camera_output_pump_irq(session.output[i],20)<0);
    }
    if(partial_open)CHECK(session.closed[1]&&session.stopped[1]&&!session.opened[1]&&!session.retired[1]&&session.output[1]);
    if(partial_prepare)CHECK(session.stopped[1]&&!session.output[1]&&!session.retired[1]);
    if(early_stop)CHECK(session.stopped[1]&&!session.output[1]&&!session.retired[1]);
    CHECK(!elden_camera_capture_session_shutdown(&session));
    /* After shutdown nothing can start again: an unerrored session refuses at
     * the stopping gate and an errored one still reports its latched error. */
    EldenCameraPipelineConfig done{};
    CHECK(elden_camera_capture_session_begin(&session,0,&done,&plan,2)==(session.error?session.error:-19));
    CHECK(!elden_camera_dma_capture_shutdown(&capture));CHECK(!lease&&!elden_camera_i2c10_peer_stat(i2c_peer,6));
    elden_camera_dma_peer_reset();std::free(native_clock);std::free(shared_power.bus.port);elden_camera_i2c10_peer_destroy(i2c_peer);
    for(auto *p:locks)delete p;for(auto *p:contexts)delete p;for(auto *p:camera_pipelines)delete p;
    for(auto *p:retained_services)p->release();
    for(unsigned i=0;i<2;i++){elden_camera_cci_peer_destroy(cci_peers[i]);elden_camera_pipeline_peer_destroy(capture_peers[i]);}
    elden_camera_output_peer_cleanup();
    std::printf("PASS actual capture-session lifecycle front/rear %s: %u checks; real board/DMA/output/service methods, OS peers modeled\n",argv[1],checks);return 0;
}
