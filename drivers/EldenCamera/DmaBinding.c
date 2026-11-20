/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaBinding.h"
static int result(int rc){return rc<=0?rc:-13;}
static int valid(EldenCameraDmaCapture *c,unsigned sensor){return c&&c->initialized&&sensor<2;}
static int admit(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){EldenCameraDmaCapture *c=v;
    if(!valid(c,sensor)||owner!=c->owner||epoch!=c->epoch)return -116;
    int rc=elden_camera_dma_admit(c->dma,owner,epoch,op==EldenCameraCleanup);
    if(!rc)rc=result(c->physical.admit(c->physical.context,owner,epoch,sensor,op));return rc;
}
static int delay(void *v,unsigned us){EldenCameraDmaCapture *c=v;return result(c->physical.delay_us(c->physical.context,us));}
static int drain(void *v,const EldenCameraPipelineConfig *cfg){EldenCameraDmaCapture *c=v;
    if(cfg->owner!=c->owner||cfg->epoch!=c->epoch||cfg->route_epoch!=c->route_epoch)return -116;
    return result(c->physical.physical_drain(c->physical.context,cfg));
}
static int buffer(void *v,const EldenCameraPipelineConfig *cfg,const EldenCameraBuffer *b,int completion){EldenCameraDmaCapture *c=v;
    if(cfg->route_epoch!=c->route_epoch)return -116;
    return elden_camera_dma_buffer(c->dma,cfg->owner,cfg->epoch,cfg->sensor,b,completion);
}
static int resources(void *v,const EldenCameraPipelineConfig *cfg,int action){EldenCameraDmaCapture *c=v;
    if(cfg->owner!=c->owner||cfg->epoch!=c->epoch||cfg->route_epoch!=c->route_epoch)return -116;
    int cleanup=action==EldenCamDependencyRelease||(c->board.cleanup_mask&(1u<<cfg->sensor));
    int rc=elden_camera_dma_admit(c->dma,c->owner,c->epoch,cleanup);
    if(!rc)rc=result(c->physical.receiver_resources(c->physical.context,cfg,action));return rc;
}
int elden_camera_dma_capture_bind(EldenCameraDmaCapture *c,EldenCameraNativeDma *dma,EldenCameraNativeClocks *clocks,EldenCameraPowerSession *power,const EldenCameraBoardOwner *physical){
    if(!c||c->initialized||!dma||!physical||!physical->owner||!physical->epoch||!physical->admit||!physical->delay_us||!physical->physical_drain||!physical->receiver_resources)return -22;
    EldenCameraDmaView view;int rc=elden_camera_dma_view(dma,physical->owner,physical->epoch,&view);if(rc)return rc;
    if(view.stream_id!=0x1c00||!view.lease.route_epoch)return -116;
    c->dma=dma;c->physical=*physical;c->owner=physical->owner;c->epoch=physical->epoch;c->route_epoch=view.lease.route_epoch;c->initialized=1;
    EldenCameraBoardOwner p={c,c->owner,c->epoch,admit,delay,drain,buffer,resources};return elden_camera_board_bind(&c->board,clocks,power,&p);
}
int elden_camera_dma_capture_prepare(EldenCameraDmaCapture *c,const EldenCameraPipelineConfig *cfg,const EldenCameraBoardClockPlan *plan){
    if(!c||!c->initialized||!cfg||cfg->owner!=c->owner||cfg->epoch!=c->epoch||cfg->route_epoch!=c->route_epoch)return -116;
    EldenCameraDmaView view;int rc=elden_camera_dma_view(c->dma,c->owner,c->epoch,&view);if(rc)return rc;unsigned count=0;
    for(unsigned i=0;i<view.count;i++)count+=view.slots[i].sensor==cfg->sensor;if(count<2)return -19;
    return elden_camera_board_prepare(&c->board,cfg,plan);
}
int elden_camera_dma_capture_probe(EldenCameraDmaCapture *c,unsigned sensor){return valid(c,sensor)?elden_camera_board_probe(&c->board,sensor):-22;}
int elden_camera_dma_capture_queue_initial(EldenCameraDmaCapture *c,unsigned sensor){
    if(!valid(c,sensor)||!c->board.prepared[sensor]||c->board.stopped[sensor])return -22;
    EldenCameraDmaView view;int rc=elden_camera_dma_view(c->dma,c->owner,c->epoch,&view);if(rc)return rc;unsigned count=0;
    for(unsigned i=0;i<view.count;i++)if(view.slots[i].sensor==sensor){rc=elden_camera_native_pipeline_queue(c->owner,c->epoch,sensor,&view.slots[i].buffer);if(rc)return rc;count++;}
    return count>=2?0:-19;
}
int elden_camera_dma_capture_start(EldenCameraDmaCapture *c,unsigned sensor){return valid(c,sensor)?elden_camera_board_start(&c->board,sensor):-22;}
int elden_camera_dma_capture_service(EldenCameraDmaCapture *c,unsigned sensor){
    return valid(c,sensor)?elden_camera_native_pipeline_service(c->owner,c->epoch,sensor):-22;
}
int elden_camera_dma_capture_frame(EldenCameraDmaCapture *c,unsigned sensor,EldenCameraFrame *out){
    if(!valid(c,sensor)||!out)return -22;EldenCameraFrame frame;int rc=elden_camera_native_pipeline_frame(c->owner,c->epoch,sensor,&frame);if(rc)return rc;
    rc=elden_camera_dma_claim_frame(c->dma,c->owner,c->epoch,sensor,&frame);if(!rc)*out=frame;return rc;
}
int elden_camera_dma_capture_release(EldenCameraDmaCapture *c,unsigned sensor,const EldenCameraFrame *frame,int requeue){
    if(!valid(c,sensor)||(requeue!=0&&requeue!=1))return -22;
    int rc=elden_camera_dma_release_frame(c->dma,c->owner,c->epoch,sensor,frame);if(rc||!requeue)return rc;
    return elden_camera_native_pipeline_queue(c->owner,c->epoch,sensor,&frame->buffer);
}
int elden_camera_dma_capture_stop(EldenCameraDmaCapture *c,unsigned sensor){return valid(c,sensor)?elden_camera_board_stop(&c->board,sensor):-22;}
int elden_camera_dma_capture_shutdown(EldenCameraDmaCapture *c){
    if(!c||!c->initialized)return -22;
    int rc=elden_camera_board_shutdown(&c->board);if(rc)return rc;
    // No active source/receiver/IRQ/clock remains after real board shutdown.
    // Owner retire samples actual DMA/TLB/context/immutable route. Failure
    // keeps all descriptors/table maps and callback lifetimes to full reset.
    return elden_camera_dma_stop(c->dma,c->owner,c->epoch);
}
