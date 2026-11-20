/* SPDX-License-Identifier: GPL-2.0-only */
#include "PowerBinding.h"
static int valid(EldenCameraPoweredCapture *p){return p&&p->initialized&&p->power&&p->power->initialized&&!p->power->closed;}
static int admit(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){
    EldenCameraPoweredCapture *p=v;if(!valid(p)||p->sensor!=sensor||owner!=p->board.owner||epoch!=p->board.epoch||op>EldenCameraCleanup)return -116;
    int rc=p->board.admit(p->board.context,owner,epoch,sensor,op);if(rc)return rc;
    rc=p->power->owner.admit(p->power->owner.context,owner,epoch,op==EldenCameraCleanup);return rc<=0?rc:-13;
}
static int resource(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on){
    EldenCameraPoweredCapture *p=v;if(!valid(p)||sensor!=p->sensor||owner!=p->board.owner||epoch!=p->board.epoch)return -116;
    if(r==EldenCameraReset||r==EldenCameraClock)return p->board.resource(p->board.context,owner,epoch,sensor,r,on);
    if(r<EldenCameraDovdd||r>EldenCameraAvdd)return -22;
    return elden_camera_power_session_resource(p->power,owner,epoch,sensor,r,on);
}
static int delay(void *v,unsigned us){EldenCameraPoweredCapture *p=v;return valid(p)?p->board.delay_us(p->board.context,us):-125;}
int elden_camera_powered_capture_bind(EldenCameraPoweredCapture *p,EldenCameraPowerSession *power,const EldenCameraSensorProvider *board,unsigned sensor){
    if(!p||p->initialized||!power||!power->initialized||power->closed||!board||sensor>1||!board->admit||!board->resource||!board->delay_us)return -22;
    if(power->owner.owner!=board->owner||power->owner.epoch!=board->epoch)return -116;
    EldenCameraPowerStatus status;int rc=elden_camera_power_status(&power->power,&status);if(rc)return rc;
    if(!status.ready||status.stopped||status.error)return -125;
    p->board=*board;p->power=power;p->sensor=sensor;p->initialized=1;
    EldenCameraSensorProvider provider={p,board->owner,board->epoch,admit,resource,delay,0,0};
    return elden_camera_capture_bind(&p->capture,&provider,sensor);
}
int elden_camera_powered_capture_probe(EldenCameraPoweredCapture *p){return valid(p)?elden_camera_capture_probe(&p->capture):-125;}
int elden_camera_powered_capture_start(EldenCameraPoweredCapture *p){return valid(p)?elden_camera_capture_start(&p->capture):-125;}
int elden_camera_powered_capture_stop(EldenCameraPoweredCapture *p){return valid(p)?elden_camera_capture_stop(&p->capture):-125;}
