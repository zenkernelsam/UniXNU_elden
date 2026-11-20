/* SPDX-License-Identifier: GPL-2.0-only */
#include "Board.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
struct board_clock_constraint {unsigned clock,count;uint32_t hz[8];};
struct board_clock_list {const struct board_clock_constraint *items;unsigned count;};
#include "ClockBoard.inc"
static int result(int rc){return rc<=0?rc:-13;}
static int good(EldenCameraBoard *b,unsigned sensor){return b&&b->initialized&&sensor<2;}
static int authority(EldenCameraBoard *b,unsigned sensor,unsigned op){if(!good(b,sensor))return -22;
    if(op!=EldenCameraCleanup){if(b->error)return b->error;EldenCameraClockStatus status;int rc=elden_camera_native_clocks_status(b->clocks,&status);
        if(rc)return rc;if(!status.ready||status.error||status.stopped)return -125;}
    return result(b->owner.admit(b->owner.context,b->owner.owner,b->owner.epoch,sensor,op));}
static int failure(EldenCameraBoard *b,int rc){if(rc&&!b->error)b->error=rc<0?rc:-13;return rc<0?rc:-13;}
static int clock(EldenCameraBoard *b,unsigned sensor,unsigned which,uint32_t hz,int on){
    if(on){b->attempted[sensor]|=UINT64_C(1)<<which;b->rates[sensor][which]=hz;}
    int rc=elden_camera_native_clock(b->clocks,b->owner.owner,b->owner.epoch,sensor,which,hz,on);
    if(!rc&&!on)b->attempted[sensor]&=~(UINT64_C(1)<<which);return rc;
}
static int domain(EldenCameraBoard *b,unsigned sensor,unsigned which,int on){if(on)b->domains[sensor]|=1u<<which;
    int rc=elden_camera_native_domain(b->clocks,b->owner.owner,b->owner.epoch,sensor,which,on);
    if(!rc&&!on)b->domains[sensor]&=~(1u<<which);return rc;}
static int sensor_admit(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){EldenCameraBoard *b=v;
    if(!good(b,sensor)||owner!=b->owner.owner||epoch!=b->owner.epoch)return -116;
    return authority(b,sensor,op);
}
static int sensor_resource(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned resource,int on){EldenCameraBoard *b=v;
    if(!good(b,sensor)||owner!=b->owner.owner||epoch!=b->owner.epoch)return -116;
    if(resource==EldenCameraReset)return elden_camera_native_sensor_reset(b->clocks,owner,epoch,sensor,on);
    if(resource==EldenCameraClock)return clock(b,sensor,sensor?EldenCamClockMclk4:EldenCamClockMclk3,19200000,on);
    return -22; // PoweredCapture exclusively handles the real WL supplies.
}
static int sensor_delay(void *v,unsigned us){EldenCameraBoard *b=v;return result(b->owner.delay_us(b->owner.context,us));}
static int cci_admit(void *v,uint64_t owner,uint64_t epoch,unsigned controller,unsigned op){EldenCameraBoard *b=v;
    if(owner!=b->owner.owner||epoch!=b->owner.epoch||controller>1)return -116;
    // Original CCI's transfer entry is also used for actual sensor STOP
    // register writes. Only the serialized board_stop scope permits that
    // retained path after an earlier drain/clock fault. No public new starts.
    return authority(b,controller,op==EldenCciCleanup||(b->cleanup_mask&(1u<<controller))?EldenCameraCleanup:EldenCameraConfigure);
}
static uint32_t cci_hz(void *v,uint64_t owner,uint64_t epoch,unsigned controller){EldenCameraBoard *b=v;uint32_t hz=0;
    // Native CCI checks normal admission before transfers, but its register
    // access/STOP restore always needs real retained cleanup clock telemetry.
    if(controller>1||elden_camera_native_clock_observe_cleanup(b->clocks,owner,epoch,controller?EldenCamClockCci1:EldenCamClockCci0,&hz))return 0;return hz;
}
static int pipeline_admit(void *v,const EldenCameraPipelineConfig *config,unsigned op){EldenCameraBoard *b=v;
    if(!good(b,config->sensor)||config->owner!=b->owner.owner||config->epoch!=b->owner.epoch)return -116;
    int rc=authority(b,config->sensor,op==EldenPipeCleanup?EldenCameraCleanup:EldenCameraConfigure);
    if(!rc)rc=result(b->owner.receiver_resources(b->owner.context,config,EldenCamDependencyObserve));return rc;
}
static int pipeline_buffer(void *v,const EldenCameraPipelineConfig *config,const EldenCameraBuffer *buffer,int complete){EldenCameraBoard *b=v;
    return result(b->owner.buffer(b->owner.context,config,buffer,complete));}
static int pipeline_drain(void *v,const EldenCameraPipelineConfig *config){EldenCameraBoard *b=v;return result(b->owner.physical_drain(b->owner.context,config));}
int elden_camera_board_bind(EldenCameraBoard *b,EldenCameraNativeClocks *clocks,EldenCameraPowerSession *power,const EldenCameraBoardOwner *owner){
    if(!b||b->initialized||!clocks||!power||!power->initialized||power->closed||!owner||!owner->owner||!owner->epoch||!owner->admit||!owner->delay_us||
       !owner->physical_drain||!owner->buffer||!owner->receiver_resources)return -22;
    if(power->owner.owner!=owner->owner||power->owner.epoch!=owner->epoch)return -116;
    EldenCameraClockStatus status;int rc=elden_camera_native_clocks_status(clocks,&status);if(rc)return rc;if(!status.ready||!status.pins_ready||status.error)return -19;
    b->clocks=clocks;b->power=power;b->owner=*owner;b->initialized=1;return 0;
}
static int validate_clock_list(const struct board_clock_list *list,const EldenCameraBoardClockPlan *plan){
    for(unsigned i=0;i<list->count;i++){const struct board_clock_constraint *r=&list->items[i];uint32_t hz=plan->hz[r->clock];
        if(!hz)return -22;int allowed=r->count==1&&r->hz[0]==0;
        for(unsigned j=0;j<r->count;j++)allowed|=r->hz[j]==hz;
        if(!allowed||!elden_camera_clock_rate_supported(r->clock,hz))return -34;
    }return 0;
}
static int prepare_clock_list(EldenCameraBoard *b,unsigned sensor,const struct board_clock_list *list,const EldenCameraBoardClockPlan *plan){
    for(unsigned i=0;i<list->count;i++){const struct board_clock_constraint *r=&list->items[i];uint32_t hz=plan->hz[r->clock];
        int rc=clock(b,sensor,r->clock,hz,1);if(rc)return rc;
    }return 0;
}
int elden_camera_board_prepare(EldenCameraBoard *b,const EldenCameraPipelineConfig *config,const EldenCameraBoardClockPlan *plan){
    if(!b||!b->initialized||!config||!plan||config->sensor>1||config->decoder>4||config->owner!=b->owner.owner||config->epoch!=b->owner.epoch||
       config->timer_hz!=400000000||b->prepared[config->sensor]||b->stopped[config->sensor]||b->error)return -22;
    uint64_t physical[3];uint32_t bytes[3],intids[3];int checked=elden_camera_pipeline_resources(config,physical,bytes,intids);
    if(checked)return checked; // exact mode/SID epoch/RDI before any rail/clock write
    for(unsigned peer=0;peer<2;peer++)if(peer!=config->sensor&&b->receivers[peer]&&b->configs[peer].decoder==config->decoder)return -16;
    unsigned sensor=config->sensor;int rc=validate_clock_list(&phy_plans[sensor?4:3],plan);
    if(!rc)rc=validate_clock_list(&csid_plans[config->decoder],plan);if(!rc)rc=validate_clock_list(&vfe_plans[config->decoder],plan);
    if(rc)return rc;rc=authority(b,sensor,EldenCameraAcquire);if(rc)return rc;b->configs[sensor]=*config;
    b->receivers[sensor]=1;rc=result(b->owner.receiver_resources(b->owner.context,config,EldenCamDependencyAcquire));if(rc)return failure(b,rc);
    rc=domain(b,sensor,0,1);if(!rc&&config->decoder<3)rc=domain(b,sensor,config->decoder+1,1);if(rc)return failure(b,rc);
    rc=prepare_clock_list(b,sensor,&phy_plans[sensor?4:3],plan);
    if(!rc)rc=prepare_clock_list(b,sensor,&csid_plans[config->decoder],plan);
    if(!rc)rc=prepare_clock_list(b,sensor,&vfe_plans[config->decoder],plan);
    if(!rc)rc=clock(b,sensor,sensor?EldenCamClockCci1:EldenCamClockCci0,37500000,1);
    if(rc)return failure(b,rc);
    EldenCameraCciProvider cci={b,b->owner.owner,b->owner.epoch,cci_admit,cci_hz};b->cci_open[sensor]=1;
    rc=elden_camera_native_cci_open(&cci,sensor);if(rc)return failure(b,rc);
    EldenCameraNativePipelineOwner pipeline={b,pipeline_admit,pipeline_buffer,pipeline_drain};b->pipeline_open[sensor]=1;
    rc=elden_camera_native_pipeline_open(config,&pipeline);if(rc)return failure(b,rc);
    EldenCameraSensorProvider provider={b,b->owner.owner,b->owner.epoch,sensor_admit,sensor_resource,sensor_delay,0,0};
    rc=elden_camera_powered_capture_bind(&b->captures[sensor],b->power,&provider,sensor);if(rc)return failure(b,rc);
    b->prepared[sensor]=1;return 0;
}
int elden_camera_board_probe(EldenCameraBoard *b,unsigned sensor){if(!good(b,sensor)||!b->prepared[sensor]||b->error)return -22;int rc=elden_camera_powered_capture_probe(&b->captures[sensor]);return rc?failure(b,rc):0;}
int elden_camera_board_start(EldenCameraBoard *b,unsigned sensor){if(!good(b,sensor)||!b->prepared[sensor]||b->error)return -22;int rc=elden_camera_powered_capture_start(&b->captures[sensor]);return rc?failure(b,rc):0;}
static int stop_target(EldenCameraBoard *b,unsigned sensor){
    if(b->stopped[sensor])return 0;int rc=authority(b,sensor,EldenCameraCleanup);if(rc)return rc;
    if(b->captures[sensor].initialized){rc=elden_camera_powered_capture_stop(&b->captures[sensor]);if(rc)return failure(b,rc);}
    else{
        if(b->pipeline_open[sensor]){rc=elden_camera_native_pipeline_stop(b->owner.owner,b->owner.epoch,sensor);if(rc)return failure(b,rc);}
        if(b->cci_open[sensor]){rc=elden_camera_native_cci_stop(b->owner.owner,b->owner.epoch,sensor);if(rc)return failure(b,rc);}
    }
    for(unsigned i=EldenCamClockCount;i>0;){i--;if(b->attempted[sensor]&(UINT64_C(1)<<i)){rc=clock(b,sensor,i,b->rates[sensor][i],0);if(rc)return failure(b,rc);}}
    b->stopped[sensor]=1;return 0;
}
int elden_camera_board_stop(EldenCameraBoard *b,unsigned sensor){
    if(!good(b,sensor))return -22;b->cleanup_mask|=1u<<sensor;int rc=stop_target(b,sensor);b->cleanup_mask&=~(1u<<sensor);return rc;
}
int elden_camera_board_shutdown(EldenCameraBoard *b){if(!b||!b->initialized)return -22;
    for(unsigned sensor=0;sensor<2;sensor++){int rc=elden_camera_board_stop(b,sensor);if(rc)return rc;}
    // Domains are shared: both capture engines/CCI must retire before child
    // TFE domains and finally common TITAN_TOP. Sensor0's top reference must
    // not deadlock behind sensor1's still-live child domain during shutdown.
    for(unsigned domain_index=4;domain_index>0;){domain_index--;
        for(unsigned sensor=0;sensor<2;sensor++)if(b->domains[sensor]&(1u<<domain_index)){
            int rc=domain(b,sensor,domain_index,0);if(rc)return failure(b,rc);}}
    for(unsigned sensor=0;sensor<2;sensor++)if(b->receivers[sensor]){
        int rc=result(b->owner.receiver_resources(b->owner.context,&b->configs[sensor],EldenCamDependencyRelease));if(rc)return failure(b,rc);b->receivers[sensor]=0;}
    int rc=elden_camera_power_session_stop(b->power);if(!rc)rc=elden_camera_native_clocks_stop(b->clocks,b->owner.owner,b->owner.epoch);return rc;
}
