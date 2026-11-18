/* SPDX-License-Identifier: GPL-2.0-only */
/* Adapter for actual selected Linux sensor bodies, not a Linux/IOKit emulator.
 * Power/refcounts/serialization are the retained core's real ownership model;
 * all hardware operations and receiver proofs are mandatory external providers.
 */
#ifndef ELDEN_CAMERA_CORE_SHIM_H
#define ELDEN_CAMERA_CORE_SHIM_H
#include "Core.h"
#include <stdbool.h>
#include <stddef.h>
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof(*(a)))) /*these fixed tables fit int*/
#define CCI_REG8(r) ((uint32_t)(r)|(1u<<16))
#define CCI_REG16(r) ((uint32_t)(r)|(2u<<16))
#define V4L2_CID_EXPOSURE 1u
#define V4L2_CID_ANALOGUE_GAIN 2u
#define V4L2_CID_TEST_PATTERN 3u
typedef uint16_t u16;
typedef uint64_t u64;
struct CameraInternal;
struct device {void *data;};
struct regmap {struct CameraInternal *core;};
struct clk {struct CameraInternal *core;};
struct gpio_desc {struct CameraInternal *core;};
struct supply {struct CameraInternal *core;unsigned index;};
struct regulator_bulk_data {struct supply *consumer;};
struct v4l2_subdev {struct device *dev;};
struct v4l2_subdev_state {unsigned held;};
struct v4l2_ctrl_handler {struct CameraInternal *core;};
struct v4l2_ctrl {struct v4l2_ctrl_handler *handler;uint32_t id,val;};
struct cci_reg_sequence {uint32_t reg;uint64_t val;};
struct camera_sensor {
    struct v4l2_subdev sd;struct v4l2_ctrl_handler ctrls;
    struct regmap *regmap;struct clk *xclk;struct gpio_desc *reset;
    struct regulator_bulk_data supplies[3];bool streaming;
};
#define s5kjns camera_sensor
#define gc08a8 camera_sensor
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define to_s5kjns(p) container_of((struct v4l2_subdev *)(p),struct camera_sensor,sd)
#define to_gc08a8(p) to_s5kjns(p)
#define dev_get_drvdata(d) ((d)->data)
struct CameraInternal {
    uint32_t magic,busy;EldenCameraIo io;struct camera_sensor sensor;
    struct device device;struct regmap regmap;struct clk clock;struct gpio_desc reset;
    struct supply supply[3];struct v4l2_subdev_state state;
    uint32_t references,exposure,gain,reads,writes;
    int error;uint8_t target,rails,clock_on,reset_asserted,reset_known,power_valid,identified,held,cleanup,go_attempted,owned,receiver_leased;
};
_Static_assert(sizeof(struct CameraInternal)<=sizeof(EldenCameraCore),"camera storage too small");
#define CAMERA_MAGIC 0x43414d34u
static int camera_admit(struct CameraInternal *c,enum EldenCameraOperation op){
    if(!c->owned&&op!=EldenCameraAcquire)return EldenCameraHeld;
    return c->io.admit(c->io.context,c->target,c->cleanup?EldenCameraCleanup:op);
}
static int camera_error(struct CameraInternal *c,int rc){if(rc&&!c->error)c->error=rc;return rc;}
static int camera_resource(struct CameraInternal *c,enum EldenCameraResource r,int on){
    if(c->error&&!c->cleanup)return c->error;
    int rc=camera_admit(c,c->cleanup?EldenCameraCleanup:EldenCameraAcquire);
    if(!rc)rc=c->io.resource(c->io.context,c->target,r,on);
    return camera_error(c,rc);
}
static void gpiod_set_value_cansleep(struct gpio_desc *d,int value){
    d->core->reset_known=0;
    if(!camera_resource(d->core,EldenCameraReset,value)){d->core->reset_asserted=(uint8_t)value;d->core->reset_known=1;}
}
static int regulator_enable(struct supply *s){
    s->core->rails|=(uint8_t)(1u<<s->index); // an unsuccessful enable may have applied
    int rc=camera_resource(s->core,(enum EldenCameraResource)(EldenCameraDovdd+s->index),1);
    if(!rc)s->core->rails|=(uint8_t)(1u<<s->index);return rc;
}
static void regulator_disable(struct supply *s){
    // The retained lease only releases refs actually acquired by this core.
    if(!(s->core->rails&(1u<<s->index)))return;
    if(!camera_resource(s->core,(enum EldenCameraResource)(EldenCameraDovdd+s->index),0))s->core->rails&=(uint8_t)~(1u<<s->index);
}
static int clk_prepare_enable(struct clk *c){
    c->core->clock_on=1; // keep an uncertain enable in the cleanup ledger
    int rc=camera_resource(c->core,EldenCameraClock,1);if(!rc)c->core->clock_on=1;return rc;
}
static void clk_disable_unprepare(struct clk *c){
    if(c->core->clock_on&&!camera_resource(c->core,EldenCameraClock,0))c->core->clock_on=0;
}
static void camera_sleep(struct camera_sensor *s,unsigned us){
    struct CameraInternal *c=s->regmap->core;if(c->error&&!c->cleanup)return;
    int rc=camera_admit(c,c->cleanup?EldenCameraCleanup:EldenCameraAcquire);
    if(!rc)rc=c->io.delay_us(c->io.context,us);camera_error(c,rc);
}
#define fsleep(us) camera_sleep(sensor,(us))
static int cci_write(struct regmap *m,uint32_t reg,uint64_t value,int *error){
    struct CameraInternal *c=m->core;if(error&&*error)return *error;
    unsigned width=reg>>16;uint16_t address=(uint16_t)reg;
    bool stop=(address==0x0104&&!value)||(address==0x0100&&!value);
    int rc=0;
    if(width<1||width>2||value>(width==1?255u:65535u))rc=EldenCameraArgument;
    else if(c->error&&!c->cleanup&&!stop)rc=c->error;
    else{
        enum EldenCameraOperation op=stop?EldenCameraCleanup:address==0x0100?EldenCameraStream:EldenCameraConfigure;
        rc=camera_admit(c,op);
        if(!rc){if(address==0x0100&&value)c->go_attempted=1;
            rc=c->io.write(c->io.context,c->target,c->target?0x31:0x56,address,width,(uint32_t)value);c->writes++;}
    }
    camera_error(c,rc);if(error&&rc)*error=rc;return rc;
}
static void cci_multi_reg_write(struct regmap *m,const struct cci_reg_sequence *s,size_t n,int *error){
    for(size_t i=0;i<n&&!*error;i++)cci_write(m,s[i].reg,s[i].val,error);
}
static struct v4l2_subdev_state *v4l2_subdev_lock_and_get_active_state(struct v4l2_subdev *sd){
    struct CameraInternal *c=to_s5kjns(sd)->regmap->core;c->state.held=1;return &c->state;
}
static void v4l2_subdev_unlock_state(struct v4l2_subdev_state *s){s->held=0;}
static int pm_runtime_get_if_in_use(struct device *d){
    struct CameraInternal *c=to_s5kjns(dev_get_drvdata(d))->regmap->core;
    if(!c->power_valid)return 0;c->references++;return 1;
}
static void pm_runtime_put(struct device *d){
    struct CameraInternal *c=to_s5kjns(dev_get_drvdata(d))->regmap->core;
    if(c->references)c->references--;else camera_error(c,-71);
}
static int pm_runtime_resume_and_get(struct device *);
static int pm_runtime_put_sync(struct device *);
static int __v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *);
#endif
