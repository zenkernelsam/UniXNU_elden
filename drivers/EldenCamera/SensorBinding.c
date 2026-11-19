/* SPDX-License-Identifier: GPL-2.0-only */
#include "SensorBinding.h"
static int valid(EldenCameraSensor *s){return s&&s->initialized&&!s->stopped;}
static int admit(void *v,unsigned target,enum EldenCameraOperation op){
    EldenCameraSensor *s=v;if(!valid(s)||target!=s->sensor)return -125;
    return s->provider.admit(s->provider.context,s->provider.owner,s->provider.epoch,target,op);
}
static int resource(void *v,unsigned target,enum EldenCameraResource r,int on){
    EldenCameraSensor *s=v;if(!valid(s)||target!=s->sensor)return -125;
    return s->provider.resource(s->provider.context,s->provider.owner,s->provider.epoch,target,r,on);
}
static int read(void *v,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t *out){
    EldenCameraSensor *s=v;if(!valid(s)||controller!=s->sensor||slave!=(controller?0x31:0x56))return -13;
    return elden_camera_native_cci_read(s->provider.owner,s->provider.epoch,controller,slave,reg,width,out);
}
static int write(void *v,unsigned controller,uint8_t slave,uint16_t reg,unsigned width,uint32_t value){
    EldenCameraSensor *s=v;if(!valid(s)||controller!=s->sensor||slave!=(controller?0x31:0x56))return -13;
    return elden_camera_native_cci_write(s->provider.owner,s->provider.epoch,controller,slave,reg,width,value);
}
static int delay(void *v,unsigned us){EldenCameraSensor *s=v;return valid(s)?s->provider.delay_us(s->provider.context,us):-125;}
static int ready(void *v,unsigned target){EldenCameraSensor *s=v;if(!valid(s)||target!=s->sensor)return -125;
    return s->provider.receiver_ready(s->provider.context,s->provider.owner,s->provider.epoch,target);}
static int drain(void *v,unsigned target){EldenCameraSensor *s=v;if(!valid(s)||target!=s->sensor)return -125;
    return s->provider.receiver_drain(s->provider.context,s->provider.owner,s->provider.epoch,target);}
int elden_camera_sensor_bind(EldenCameraSensor *s,const EldenCameraSensorProvider *p,unsigned sensor){
    if(!s||s->initialized||!p||!p->owner||!p->epoch||sensor>1||!p->admit||!p->resource||!p->delay_us||!p->receiver_ready||!p->receiver_drain)return -22;
    int rc=p->admit(p->context,p->owner,p->epoch,sensor,EldenCameraAcquire);if(rc)return rc;
    rc=elden_camera_native_cci_ready(p->owner,p->epoch,sensor);if(rc)return rc;
    EldenCciStatus status;rc=elden_camera_native_cci_status(sensor,&status);if(rc)return rc;if(status.state!=EldenCciReady)return -19;
    // Native same-session readiness was verified before any sensor power.
    s->provider=*p;s->sensor=sensor;s->initialized=1;
    EldenCameraIo io={s,admit,resource,read,write,delay,ready,drain};rc=elden_camera_bind(&s->core,&io,sensor);
    if(rc)s->stopped=1;return rc;
}
int elden_camera_sensor_probe(EldenCameraSensor *s){return valid(s)?elden_camera_probe(&s->core):-125;}
int elden_camera_sensor_start(EldenCameraSensor *s){return valid(s)?elden_camera_start(&s->core):-125;}
int elden_camera_sensor_control(EldenCameraSensor *s,uint32_t exposure,uint32_t gain){return valid(s)?elden_camera_control(&s->core,exposure,gain):-125;}
int elden_camera_sensor_stop(EldenCameraSensor *s){return valid(s)?elden_camera_stop(&s->core):-125;}
int elden_camera_sensor_shutdown(EldenCameraSensor *s){
    if(!valid(s))return -125;int rc=elden_camera_stop(&s->core);if(rc)return rc;
    if(!elden_camera_terminal(&s->core))return -125;
    rc=elden_camera_native_cci_stop(s->provider.owner,s->provider.epoch,s->sensor);if(!rc)s->stopped=1;return rc;
}
