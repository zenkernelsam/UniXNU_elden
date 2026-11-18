/* SPDX-License-Identifier: GPL-2.0-only */
static struct CameraInternal *core(EldenCameraCore *s){return (struct CameraInternal *)(void *)s;}
static int enter(EldenCameraCore *s){
    if(!s||core(s)->magic!=CAMERA_MAGIC)return EldenCameraArgument;
    uint32_t empty=0;return __atomic_compare_exchange_n(&core(s)->busy,&empty,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:EldenCameraBusy;
}
static int leave(struct CameraInternal *c,int rc){__atomic_store_n(&c->busy,0u,__ATOMIC_RELEASE);return rc;}
int elden_camera_bind(EldenCameraCore *storage,const EldenCameraIo *io,unsigned target){
    if(!storage||!io||!io->admit||!io->resource||!io->read||!io->write||!io->delay_us||!io->receiver_ready||!io->receiver_drain||target>1)return EldenCameraArgument;
    if(core(storage)->magic)return EldenCameraBusy;
    EldenCameraCore zero={0};*storage=zero;struct CameraInternal *c=core(storage);c->magic=CAMERA_MAGIC;c->io=*io;c->target=(uint8_t)target;
    c->sensor.sd.dev=&c->device;c->device.data=&c->sensor.sd;c->regmap.core=c;c->clock.core=c;c->reset.core=c;
    c->sensor.regmap=&c->regmap;c->sensor.xclk=&c->clock;c->sensor.reset=&c->reset;c->sensor.ctrls.core=c;
    for(unsigned i=0;i<3;i++){c->supply[i]=(struct supply){c,i};c->sensor.supplies[i].consumer=&c->supply[i];}
    c->exposure=target?0x0904:0x0100;c->gain=target?1024:32;return 0;
}
int elden_camera_probe(EldenCameraCore *storage){
    int rc=enter(storage);if(rc)return rc;struct CameraInternal *c=core(storage);
    if(c->held||c->identified||c->power_valid)return leave(c,EldenCameraHeld);
    c->error=0;rc=camera_admit(c,EldenCameraAcquire);
    if(rc)return leave(c,rc);c->owned=1;
    rc=c->target?gc08a8_power_on(&c->device):s5kjns_power_on(&c->device);
    if(!rc)rc=c->error;
    uint32_t id=0;
    if(!rc){c->power_valid=1;rc=camera_admit(c,EldenCameraConfigure);}
    if(!rc){rc=c->io.read(c->io.context,c->target,c->target?0x31:0x56,c->target?0x03f0:0x0000,2,&id);c->reads++;}
    int error=rc;
    c->cleanup=1;c->error=0;int off=camera_power_off(c);c->cleanup=0;
    if(off){c->held=1;c->error=off;return leave(c,off);}
    if(error)return leave(c,error);
    if(id!=(c->target?0x08a8:0x38ee))return leave(c,EldenCameraMissing);
    c->identified=1;return leave(c,0);
}
int elden_camera_start(EldenCameraCore *storage){
    int rc=enter(storage);if(rc)return rc;struct CameraInternal *c=core(storage);
    if(!c->identified||c->held)return leave(c,EldenCameraHeld);
    if(c->sensor.streaming)return leave(c,0);
    rc=camera_admit(c,EldenCameraStream);
    if(!rc){c->receiver_leased=1;rc=c->io.receiver_ready(c->io.context,c->target);}
    if(rc){if(c->receiver_leased)c->held=1;return leave(c,rc);}
    c->error=0;rc=c->target?gc08a8_s_stream(&c->sensor.sd,1):s5kjns_s_stream(&c->sensor.sd,1);if(!rc)rc=c->error;
    if(rc){c->held=1;c->error=rc;}return leave(c,rc);
}
int elden_camera_control(EldenCameraCore *storage,uint32_t exposure,uint32_t gain){
    int rc=enter(storage);if(rc)return rc;struct CameraInternal *c=core(storage);
    if(!c->owned)return leave(c,EldenCameraMissing);
    if(c->held||!c->sensor.streaming)return leave(c,EldenCameraMissing);
    if(exposure<(c->target?4u:2u)||exposure>(c->target?GC08A8_EXPOSURE_MAX:S5KJNS_EXPOSURE_MAX)||
       gain<(c->target?1024u:32u)||gain>(c->target?16384u:2048u))return leave(c,EldenCameraArgument);
    c->error=0;struct v4l2_ctrl ctrl={&c->sensor.ctrls,V4L2_CID_EXPOSURE,exposure};
    rc=c->target?gc08a8_set_ctrl(&ctrl):s5kjns_set_ctrl(&ctrl);
    if(!rc){ctrl.id=V4L2_CID_ANALOGUE_GAIN;ctrl.val=gain;rc=c->target?gc08a8_set_ctrl(&ctrl):s5kjns_set_ctrl(&ctrl);}
    if(!rc){c->exposure=exposure;c->gain=gain;}else{c->held=1;c->error=rc;}return leave(c,rc);
}
int elden_camera_stop(EldenCameraCore *storage){
    int rc=enter(storage);if(rc)return rc;struct CameraInternal *c=core(storage);
    if(!c->owned)return leave(c,0);
    c->cleanup=1;c->error=0;
    // A failed stream-on write can still have started the physical source.
    if(c->go_attempted)c->sensor.streaming=true;
    if(c->go_attempted&&!c->references){
        rc=cci_write(c->sensor.regmap,CCI_REG8(0x0100),0,NULL);
        int off=camera_power_off(c);if(!rc)rc=off;if(!off)c->sensor.streaming=false;
    }
    else if(c->sensor.streaming)rc=c->target?gc08a8_s_stream(&c->sensor.sd,0):s5kjns_s_stream(&c->sensor.sd,0);
    else rc=camera_power_off(c);
    if(!rc)rc=c->error;
    if(rc||c->rails||c->clock_on||!c->reset_known||!c->reset_asserted||c->go_attempted||c->receiver_leased){c->held=1;if(!rc)rc=EldenCameraHeld;}
    else c->held=0;c->cleanup=0;return leave(c,rc);
}
int elden_camera_mode(EldenCameraCore *storage,EldenCameraMode *out){
    if(!out)return EldenCameraArgument;int rc=enter(storage);if(rc)return rc;struct CameraInternal *c=core(storage);
    EldenCameraMode m={0};m.width=c->target?GC08A8_WIDTH:S5KJNS_WIDTH;m.height=c->target?GC08A8_HEIGHT:S5KJNS_HEIGHT;
    m.link_hz=c->target?336000000:672000000;m.pixel_hz=c->target?GC08A8_PIXEL_RATE:S5KJNS_PIXEL_RATE;
    m.expected_sensor_id=c->target?0x08a8:0x38ee;m.rotation=c->target?270:90;m.master=c->target;m.slave=c->target?0x31:0x56;
    m.reset_gpio=c->target?7:11;m.mclk=c->target?4:3;m.lanes=4;m.raw_bits=10;m.bayer=c->target?EldenCameraRggb:EldenCameraGrbg;
    *out=m;return leave(c,0);
}
int elden_camera_terminal(EldenCameraCore *storage){
    int rc=enter(storage);if(rc)return 0;struct CameraInternal *c=core(storage);
    int done=!c->owned||(!c->held&&!c->rails&&!c->clock_on&&c->reset_known&&c->reset_asserted&&!c->go_attempted&&!c->receiver_leased&&!c->sensor.streaming&&!c->references);
    leave(c,0);return done;
}
