/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual snapshot methods with modeled IOKit descriptors and DMA producer.
 * Existing real pipeline/DMA composition is tested separately. */
#include "FrameOutput.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"output line%u %s\n",__LINE__,#x);exit(1);}}while(0)
using IOLock=std::mutex;using IOOptionBits=uint32_t;using IOReturn=int;
enum {kIODirectionInOut=3,kIOMemoryKernelUserShared=0x100,kIOMemoryMapperNone=0x200};
static void *kernel_task=(void *)1;static bool interrupt;static int fail_alloc,allocations,release_error,queue_error,admit_error,done_error;
static unsigned releases,queues,claims;static bool event_ready,borrowed;static uint64_t sequence=UINT64_MAX;static bool retired;
static unsigned peer_sensor;
static uint32_t frame_stride(){return peer_sensor?4080u:5104u;}
static uint32_t frame_height(){return peer_sensor?2448u:3072u;}
#if defined(ELDEN_CAMERA_FRAME_PEER_API)
static uint32_t frame_width(){return peer_sensor?3264u:4080u;}
#endif
static uint64_t frame_size(){return (uint64_t)frame_stride()*frame_height();}
static std::vector<void *> heaps;static std::vector<IOLock *> locks;
static bool ml_at_interrupt_context(){return interrupt;}
static void *IOMallocData(size_t n){void *p=calloc(1,n);if(p)heaps.push_back(p);return p;}
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
class IOBufferMemoryDescriptor {public:uint8_t *bytes;size_t length;unsigned refs=1;bool prepared=false;
    void retain(){refs++;}
    static IOBufferMemoryDescriptor *inTaskWithOptions(void *task,unsigned flags,uint64_t n,unsigned alignment);
};
static std::vector<IOBufferMemoryDescriptor *> descriptors;static uint8_t *raw;
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::inTaskWithOptions(void *task,unsigned flags,uint64_t n,unsigned alignment){
    CHECK(task==kernel_task&&flags==(kIODirectionInOut|kIOMemoryKernelUserShared|kIOMemoryMapperNone)&&alignment==4096&&n<=16000000);
    if(++allocations==fail_alloc)return nullptr;auto *p=new IOBufferMemoryDescriptor;p->length=n;p->bytes=(uint8_t *)malloc(n);descriptors.push_back(p);return p;
}
static void *elden_camera_output_bytes(IOBufferMemoryDescriptor *m){CHECK(m&&m->prepared);return m->bytes;}
static IOReturn elden_camera_output_prepare(IOBufferMemoryDescriptor *m,unsigned flags){CHECK(m&&flags==3&&!m->prepared);m->prepared=true;return 0;}
static EldenCameraDmaCapture capture{};
extern "C" int elden_camera_dma_admit(EldenCameraNativeDma *,uint64_t o,uint64_t e,int cleanup){CHECK(o==7&&e==9&&(cleanup==0||cleanup==1));return cleanup?0:admit_error;}
extern "C" int elden_camera_native_pipeline_status(unsigned sensor,EldenCameraPipelineStatus *out){CHECK(sensor==peer_sensor&&out);*out={};out->started=1;out->stopped=retired;out->active=!retired;out->state=retired?EldenPipeStopped:EldenPipeStreaming;out->stride=frame_stride();out->frame_bytes=(uint32_t)frame_size();return 0;}
extern "C" int elden_camera_dma_view(EldenCameraNativeDma *,uint64_t o,uint64_t e,EldenCameraDmaView *out){CHECK(o==7&&e==9&&out);out->lease.route_epoch=11;return 0;}
extern "C" int elden_camera_dma_capture_service(EldenCameraDmaCapture *p,unsigned sensor){CHECK(p==&capture&&sensor==peer_sensor);return done_error;}
extern "C" int elden_camera_dma_capture_frame(EldenCameraDmaCapture *p,unsigned sensor,EldenCameraFrame *out){CHECK(p==&capture&&sensor==peer_sensor&&!borrowed&&out);if(!event_ready)return -11;
    event_ready=false;borrowed=true;claims++;*out={{1,7,9,11,0x20001000,frame_size(),raw},sequence,(sequence+1)*1000000,(uint32_t)frame_size(),0};return 0;}
extern "C" int elden_camera_dma_capture_release(EldenCameraDmaCapture *p,unsigned sensor,const EldenCameraFrame *f,int requeue){CHECK(p==&capture&&sensor==peer_sensor&&f&&!requeue&&borrowed);releases++;if(release_error)return release_error;borrowed=false;return 0;}
extern "C" int elden_camera_native_pipeline_queue(uint64_t o,uint64_t e,unsigned sensor,const EldenCameraBuffer *b){CHECK(o==7&&e==9&&sensor==peer_sensor&&b&&!borrowed);queues++;return queue_error;}
extern "C" int elden_camera_native_pipeline_listen(uint64_t o,uint64_t e,unsigned sensor){CHECK(o==7&&e==9&&sensor==peer_sensor);return -19;}
extern "C" int elden_camera_native_pipeline_wait(uint64_t o,uint64_t e,unsigned sensor,unsigned ms){CHECK(o==7&&e==9&&sensor==peer_sensor&&ms);return -19;}
extern "C" int elden_camera_dma_status(EldenCameraNativeDma *,EldenCameraDmaStatus *out){CHECK(out);*out={};out->retired=retired;return 0;}
#include "FrameOutputMethods.inc"
static void frame(uint8_t value){CHECK(!borrowed);memset(raw,value,5104*3072);event_ready=true;sequence++;}
static void latest_frames(EldenCameraFrameOutput *output){
    frame(0x41);CHECK(!elden_camera_output_pump(output));frame(0x42);CHECK(!elden_camera_output_pump(output));
    EldenCameraOutputFrame newest{};CHECK(!elden_camera_output_acquire(output,&newest)&&newest.sequence==1);
    auto *m=(IOBufferMemoryDescriptor *)elden_camera_output_memory(output,newest.ticket,newest.slot);CHECK(m&&m->bytes[0]==0x42);
    CHECK(!elden_camera_output_release(output,newest.ticket));EldenCameraOutputFrame stale{};
    CHECK(elden_camera_output_acquire(output,&stale)==-11);EldenCameraOutputStatus status{};
    CHECK(!elden_camera_output_status(output,&status)&&status.dropped==1);
    frame(0x43);CHECK(!elden_camera_output_pump(output));CHECK(!elden_camera_output_acquire(output,&newest)&&newest.sequence==2);
    CHECK(!elden_camera_output_release(output,newest.ticket));retired=true;CHECK(!elden_camera_output_stop(output));
}
#if defined(ELDEN_CAMERA_FRAME_PEER_API)
extern "C" int elden_camera_frame_peer_open(unsigned sensor,EldenCameraFrameOutput **out){
    CHECK(sensor<2&&!raw);peer_sensor=sensor;capture={};capture.initialized=1;capture.dma=(EldenCameraNativeDma *)1;
    capture.owner=7;capture.epoch=9;capture.route_epoch=11;capture.board.prepared[sensor]=1;
    capture.board.configs[sensor].mode={frame_width(),frame_height(),sensor?336000000u:672000000u,sensor?281600000u:558933333u,
        (uint16_t)(sensor?0x08a8:0x38ee),(uint16_t)(sensor?270:90),(uint8_t)sensor,(uint8_t)(sensor?0x31:0x56),(uint8_t)(sensor?7:11),
        (uint8_t)(sensor?4:3),4,10,(uint8_t)(sensor?EldenCameraRggb:EldenCameraGrbg),0};
    raw=(uint8_t *)malloc((size_t)frame_size());CHECK(raw);bzero(raw,(size_t)frame_size());
    return elden_camera_output_open(&capture,sensor,2,out);
}
extern "C" int elden_camera_frame_peer_event(EldenCameraFrameOutput *out,unsigned marker){
    CHECK(out&&marker<4&&!borrowed);memset(raw,0,(size_t)frame_size());
    /* Independent synthetic fixture encoded by source sensor sites: real
     * expected GRBG is G R / B G, RGGB is R G / G B. Not output ABI enums. */
    const unsigned sites[2][4]={{500,900,40,500},{900,500,500,40}};
    for(unsigned y=0;y<frame_height();y++)for(unsigned x=0;x<frame_width();x++){
        unsigned value=sites[peer_sensor][(y&1)*2+(x&1)]-marker*5;
        uint8_t *group=raw+(size_t)y*frame_stride()+(size_t)(x/4)*5;unsigned slot=x&3;
        group[slot]=(uint8_t)(value/4);group[4]=(uint8_t)((group[4]&~(3u<<(slot*2)))|((value&3)<<(slot*2)));
    }
    event_ready=true;sequence++;return elden_camera_output_pump(out);
}
extern "C" int elden_camera_frame_peer_map(void *memory,uintptr_t *address,uint64_t *bytes){
    auto *m=(IOBufferMemoryDescriptor *)memory;CHECK(m&&address&&bytes&&m->prepared);
    *address=(uintptr_t)m->bytes;*bytes=m->length;return 0;
}
extern "C" void elden_camera_frame_peer_unmap(void *memory){
    auto *m=(IOBufferMemoryDescriptor *)memory;CHECK(m&&m->refs>1);m->refs--;
}
extern "C" void elden_camera_frame_peer_finish(EldenCameraFrameOutput *out){
    retired=true;CHECK(!elden_camera_output_stop(out));free(raw);raw=nullptr;
    for(auto *p:descriptors){free(p->bytes);delete p;}for(auto *p:locks)delete p;for(void *p:heaps)free(p);
}
#endif
int main(int argc,char **argv){CHECK(argc==2);capture.initialized=1;capture.dma=(EldenCameraNativeDma *)1;capture.owner=7;capture.epoch=9;capture.route_epoch=11;capture.board.prepared[0]=1;
    capture.board.configs[0].mode={4080,3072,672000000,558933333,0x38ee,90,0,0x56,11,3,4,10,EldenCameraGrbg,0};
    raw=(uint8_t *)malloc(5104*3072);CHECK(raw);memset(raw,0,5104*3072);EldenCameraFrameOutput *output=nullptr;
    if(!strcmp(argv[1],"allocation"))fail_alloc=2;
    int rc=elden_camera_output_open(&capture,0,2,&output);CHECK(output);
    if(fail_alloc){CHECK(rc==-12&&!output->status.prepared&&descriptors.size()==1);CHECK(elden_camera_output_pump(output)==-12&&!claims);}
    else if(!strcmp(argv[1],"latest")){CHECK(!rc);latest_frames(output);}
    else {
        CHECK(!rc);EldenCameraOutputFrame f1{},f2{};CHECK(elden_camera_output_acquire(output,&f1)==-11);
        uint64_t info[8]={};CHECK(!elden_camera_output_info(output,info)&&info[6]==EldenRawGrbg);
        frame(0x12);CHECK(!elden_camera_output_pump(output));CHECK(!elden_camera_output_acquire(output,&f1)&&f1.sequence==0);
        auto *memory=(IOBufferMemoryDescriptor *)elden_camera_output_memory(output,f1.ticket,f1.slot);CHECK(memory&&memory->bytes[0]==0x12&&memory->bytes[memory->length-1]==0x12);
        frame(0x23);CHECK(!elden_camera_output_pump(output));CHECK(!elden_camera_output_acquire(output,&f2));CHECK(f1.slot!=f2.slot&&f1.ticket!=f2.ticket);
        frame(0x34);CHECK(elden_camera_output_pump(output)==-11&&!borrowed);CHECK(memory->bytes[0]==0x12);CHECK(!elden_camera_output_memory(output,f2.ticket,f1.slot));
        CHECK(elden_camera_output_release(output,f1.ticket+1000)==-116);CHECK(!elden_camera_output_release(output,f1.ticket));CHECK(elden_camera_output_release(output,f1.ticket)==-116);
        if(!strcmp(argv[1],"release"))release_error=-16;if(!strcmp(argv[1],"queue"))queue_error=-71;
        frame(0x45);rc=elden_camera_output_pump(output);CHECK(rc==(release_error?-16:queue_error?-71:0));
        CHECK(borrowed==!!release_error);if(release_error){CHECK(elden_camera_output_stop(output)==-16);release_error=0;}
        if(!release_error&&!queue_error&&strcmp(argv[1],"release")){CHECK(!elden_camera_output_acquire(output,&f1));CHECK(!elden_camera_output_release(output,f1.ticket));}
        CHECK(elden_camera_output_stop(output)==-16);retired=true;CHECK(!elden_camera_output_stop(output));
        CHECK(elden_camera_output_acquire(output,&f1)<0);CHECK(!elden_camera_output_release(output,f2.ticket));
        EldenCameraOutputStatus status{};CHECK(!elden_camera_output_status(output,&status)&&status.retired&&!status.held);
        CHECK(claims==releases-(strcmp(argv[1],"release")==0?2:0));
    }
    free(raw);for(auto *p:descriptors){free(p->bytes);delete p;}for(auto *p:locks)delete p;for(void *p:heaps)free(p);
    printf("PASS real frame snapshot %s: %u checks; IOKit/physical completion peers modeled\n",argv[1],checks);return 0;
}
