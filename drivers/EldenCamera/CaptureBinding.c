/* SPDX-License-Identifier: GPL-2.0-only */
#include "NativePipeline.h"
static int valid(EldenCameraCapture *c){return c&&c->initialized;}
static int admit(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){EldenCameraCapture *c=p;return valid(c)&&sensor==c->target?c->board.admit(c->board.context,owner,epoch,sensor,op):-116;}
static int resource(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on){EldenCameraCapture *c=p;return valid(c)&&sensor==c->target?c->board.resource(c->board.context,owner,epoch,sensor,r,on):-116;}
static int delay(void *p,unsigned us){EldenCameraCapture *c=p;return c->board.delay_us(c->board.context,us);}
static int ready(void *p,uint64_t owner,uint64_t epoch,unsigned sensor){EldenCameraCapture *c=p;
    if(!valid(c)||sensor!=c->target)return -116;return elden_camera_native_pipeline_ready(owner,epoch,sensor);}
static int drain(void *p,uint64_t owner,uint64_t epoch,unsigned sensor){EldenCameraCapture *c=p;
    if(!valid(c)||sensor!=c->target)return -116;return elden_camera_native_pipeline_stop(owner,epoch,sensor);}
int elden_camera_capture_bind(EldenCameraCapture *c,const EldenCameraSensorProvider *board,unsigned sensor){
    if(!c||c->initialized||!board||!board->owner||!board->epoch||sensor>1||!board->admit||!board->resource||!board->delay_us)return -22;
    int rc=elden_camera_native_pipeline_prepared(board->owner,board->epoch,sensor);if(rc)return rc;
    EldenCameraPipelineStatus status;rc=elden_camera_native_pipeline_status(sensor,&status);if(rc)return rc;
    if(status.state!=EldenPipeReady)return -19;c->board=*board;c->target=sensor;c->initialized=1;
    EldenCameraSensorProvider provider={c,board->owner,board->epoch,admit,resource,delay,ready,drain};
    rc=elden_camera_sensor_bind(&c->sensor,&provider,sensor);if(!rc)c->bound=1;else c->error=rc;return rc;
}
int elden_camera_capture_probe(EldenCameraCapture *c){return valid(c)&&c->bound&&!c->stopped&&!c->error?elden_camera_sensor_probe(&c->sensor):-22;}
int elden_camera_capture_start(EldenCameraCapture *c){
    if(!valid(c)||!c->bound||c->stopped||c->error)return -22;c->receiver_attempted=1;
    int rc=elden_camera_native_pipeline_start(c->board.owner,c->board.epoch,c->target);
    if(!rc)rc=elden_camera_sensor_start(&c->sensor);if(rc)c->error=rc;return rc;
}
int elden_camera_capture_stop(EldenCameraCapture *c){
    if(!valid(c))return -22;if(c->stopped)return 0;int rc=c->bound?elden_camera_sensor_stop(&c->sensor):0;if(rc)return rc;
    rc=elden_camera_native_pipeline_stop(c->board.owner,c->board.epoch,c->target);if(rc)return rc;
    if(c->bound)rc=elden_camera_sensor_shutdown(&c->sensor);if(!rc)c->stopped=1;return rc;
}
