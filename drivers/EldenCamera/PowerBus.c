/* SPDX-License-Identifier: GPL-2.0-only */
#include "Power.h"
static int gate(void *p,uint64_t owner,uint64_t epoch,int cleanup){EldenCameraPowerBus *b=p;return b->authority.admit(b->authority.context,owner,epoch,cleanup);}
static int read(void *p,uint8_t reg,uint8_t *out,int cleanup){EldenCameraPowerBus *b=p;return elden_camera_i2c10_read(b->port,b->authority.owner,b->authority.epoch,reg,out,cleanup);}
static int write(void *p,uint8_t reg,uint8_t value,int cleanup){EldenCameraPowerBus *b=p;return elden_camera_i2c10_write(b->port,b->authority.owner,b->authority.epoch,reg,value,cleanup);}
static int delay(void *p,unsigned us){EldenCameraPowerBus *b=p;return b->delay_us(b->context,us);}
static int inactive(void *p,uint64_t owner,uint64_t epoch,unsigned sensor){EldenCameraPowerBus *b=p;return b->inactive(b->context,owner,epoch,sensor);}
static int rail_unused(void *p,uint64_t owner,uint64_t epoch,unsigned ldo){EldenCameraPowerBus *b=p;return b->rail_unused(b->context,owner,epoch,ldo);}
int elden_camera_power_bind_bus(EldenCameraPower *out,EldenCameraPowerBus *bus){
    if(!out||!bus||!bus->port||!bus->authority.admit||!bus->inactive||!bus->delay_us||!bus->rail_unused)return -22;
    EldenCameraPowerIo io={bus,bus->authority.owner,bus->authority.epoch,gate,read,write,delay,inactive,rail_unused};return elden_camera_power_open(out,&io);
}
static int session_admit(void *p,uint64_t owner,uint64_t epoch,int cleanup){
    EldenCameraPowerSession *s=p;
    if(owner!=s->owner.owner||epoch!=s->owner.epoch)return -116;
    return s->owner.admit(s->owner.context,owner,epoch,cleanup);
}
static int session_inactive(void *p,uint64_t owner,uint64_t epoch,unsigned sensor){
    EldenCameraPowerSession *s=p;return s->owner.inactive(s->owner.context,owner,epoch,sensor);
}
static int session_delay(void *p,unsigned us){EldenCameraPowerSession *s=p;return s->owner.delay_us(s->owner.context,us);}
static int session_unused(void *p,uint64_t owner,uint64_t epoch,unsigned ldo){EldenCameraPowerSession *s=p;return s->owner.rail_unused(s->owner.context,owner,epoch,ldo);}
static int session_retired(void *p,uint64_t owner,uint64_t epoch){
    EldenCameraPowerSession *s=p;int rc=session_admit(p,owner,epoch,1);if(rc)return rc;
    // Failure before binding the WL core made no regulator writes.
    if(s->initialized){EldenCameraPowerStatus status;rc=elden_camera_power_status(&s->power,&status);
        if(rc)return rc;if(!status.stopped||status.changed)return -16;
        for(unsigned i=0;i<7;i++)if(status.users[i]||status.active_users[i])return -16;
    }
    for(unsigned sensor=0;sensor<2;sensor++){rc=session_inactive(p,owner,epoch,sensor);if(rc)return rc;}
    return 0;
}
int elden_camera_power_session_open(EldenCameraPowerSession *s,const EldenCameraPowerOwner *owner){
    if(!s||s->attempted||!owner||!owner->owner||!owner->epoch||!owner->admit||!owner->inactive||!owner->delay_us||!owner->rail_unused)return -22;
    int rc=owner->admit(owner->context,owner->owner,owner->epoch,0);if(rc)return rc<0?rc:-13;
    s->attempted=1;s->owner=*owner;
    s->bus.authority=(EldenCameraI2c10Authority){s,owner->owner,owner->epoch,session_admit,session_retired};
    s->bus.context=s;s->bus.inactive=session_inactive;s->bus.delay_us=session_delay;
    s->bus.rail_unused=session_unused;
    rc=elden_camera_i2c10_open(&s->bus.authority,&s->bus.port);if(rc)return rc;
    rc=elden_camera_power_bind_bus(&s->power,&s->bus);
    // Core lifetime exists even after an initial ID/read failure; its no-write
    // stop path still requires real admission and both inactive proofs.
    EldenCameraPowerStatus status;if(!elden_camera_power_status(&s->power,&status))s->initialized=1;
    return rc;
}
int elden_camera_power_session_resource(EldenCameraPowerSession *s,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned resource,int on){
    if(!s||!s->initialized||s->closed)return -125;
    return elden_camera_power_resource(&s->power,owner,epoch,sensor,resource,on);
}
int elden_camera_power_session_stop(EldenCameraPowerSession *s){
    if(!s||!s->attempted)return -22;if(s->closed)return 0;
    int rc=s->initialized?elden_camera_power_stop(&s->power):session_retired(s,s->owner.owner,s->owner.epoch);
    if(rc)return rc;
    if(s->bus.port)rc=elden_camera_i2c10_close(s->bus.port,s->owner.owner,s->owner.epoch);
    if(!rc)s->closed=1;return rc;
}
