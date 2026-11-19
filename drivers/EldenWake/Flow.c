/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Flow.h"
static int held(EldenWakeFlow *f,int error){
    f->error=error;f->state=EldenWakeFlowHeld;
    // Any ambiguous SPI/HID/clock/power result keeps the original maps/lease.
    // Best effort masks only the already-owned IRQ, never claims normal input.
    if(f->irq_owned)(void)f->io.irq_disarm(f->io.context,f->owner,f->epoch);return error;
}
static int gate(EldenWakeFlow *f,unsigned phase){
    if(!f||!f->initialized)return -22;
    if(f->state==EldenWakeFlowHeld)return f->error?f->error:-125;
    if(f->state==EldenWakeFlowStopped)return -125;
    return f->io.admit(f->io.context,f->owner,f->epoch,phase);
}
int elden_wake_flow_init(EldenWakeFlow *f,const EldenWakeFlowIo *io,uint64_t owner,uint64_t epoch){
    if(!f||f->initialized||!io||!owner||!epoch||!io->admit||!io->pause||!io->release_input||
       !io->irq_acquire||!io->irq_arm||!io->irq_disarm||!io->irq_stop||!io->irq_take||
       !io->command||!io->packet||!io->request_wake||!io->restore||!io->resume_worker)return -22;
    int rc=io->admit(io->context,owner,epoch,EldenWakeAdmissionNormal);if(rc)return rc;
    EldenWakeFlow next={0};next.io=*io;next.owner=owner;next.epoch=epoch;next.initialized=1;*f=next;return 0;
}
int elden_wake_flow_suspend(EldenWakeFlow *f,int enabled){
    if(enabled!=0&&enabled!=1)return -22;int rc=gate(f,EldenWakeAdmissionNormal);if(rc)return rc;
    if(f->state!=EldenWakeFlowNormal)return -16;f->state=EldenWakeFlowPausing;
    if((rc=f->io.pause(f->io.context,f->owner,f->epoch)))return held(f,rc);f->paused=1;
    if((rc=gate(f,EldenWakeAdmissionPaused))||(rc=f->io.release_input(f->io.context)))return held(f,rc);
    f->command_delivered=f->wake_requested=0;
    if(enabled&&!f->irq_owned){
        // Failed native acquisition may already retain maps/registration.
        f->irq_owned=1;if((rc=f->io.irq_acquire(f->io.context,f->owner,f->epoch)))return held(f,rc);
    }
    if(f->irq_owned&&(rc=f->io.irq_disarm(f->io.context,f->owner,f->epoch)))return held(f,rc);
    if((rc=f->io.command(f->io.context,enabled)))return held(f,rc);f->command_delivered=1;
    if(enabled){
        if((rc=f->io.irq_arm(f->io.context,f->owner,f->epoch)))return held(f,rc);f->state=EldenWakeFlowGesture;
    }else f->state=EldenWakeFlowDeep;
    return 0; // command/route delivered only, not a system-suspend certificate
}
int elden_wake_flow_irq(EldenWakeFlow *f){
    int rc=gate(f,EldenWakeAdmissionPaused);if(rc)return rc;
    if(f->state!=EldenWakeFlowGesture||!f->paused||!f->irq_owned)return -16;
    uint32_t pending=0;if((rc=f->io.irq_take(f->io.context,f->owner,f->epoch,&pending)))return held(f,rc);
    if(!pending)return 0;
    if((rc=f->io.irq_disarm(f->io.context,f->owner,f->epoch)))return held(f,rc);
    uint8_t packet[109];if((rc=f->io.packet(f->io.context,packet)))return held(f,rc);f->packets++;
    EldenWakeGesture g;rc=elden_wake_gesture(packet,sizeof(packet),1,&g);if(rc<0)return held(f,rc);
    if(rc==EldenWakeDoubleTap){
        f->gestures++;
        if((rc=f->io.request_wake(f->io.context,f->owner,f->epoch,15)))return held(f,rc);
        f->wake_requested=1;f->state=EldenWakeFlowRequested;
        return 0; // stay paused until actual power provider's resume callback
    }
    rc=f->io.irq_arm(f->io.context,f->owner,f->epoch);return rc?held(f,rc):0;
}
int elden_wake_flow_resume(EldenWakeFlow *f){
    int rc=gate(f,EldenWakeAdmissionResume);if(rc)return rc;
    if(f->state!=EldenWakeFlowGesture&&f->state!=EldenWakeFlowDeep&&f->state!=EldenWakeFlowRequested)return -16;
    f->state=EldenWakeFlowRestoring;
    if(f->irq_owned&&(rc=f->io.irq_disarm(f->io.context,f->owner,f->epoch)))return held(f,rc);
    // An independent display/power wake may race a gesture edge. After real
    // mask/drain, retire its latched request before restoring normal sampling.
    if(f->irq_owned){uint32_t pending=0;if((rc=f->io.irq_take(f->io.context,f->owner,f->epoch,&pending)))return held(f,rc);}
    if((rc=f->io.restore(f->io.context))||(rc=f->io.release_input(f->io.context)))return held(f,rc);
    if((rc=f->io.resume_worker(f->io.context,f->owner,f->epoch)))return held(f,rc);
    f->paused=0;f->state=EldenWakeFlowNormal;return 0;
}
int elden_wake_flow_stop(EldenWakeFlow *f){
    int rc=gate(f,EldenWakeAdmissionCleanup);if(rc)return rc;
    if(!f->paused){if((rc=f->io.pause(f->io.context,f->owner,f->epoch)))return held(f,rc);f->paused=1;}
    if(f->irq_owned&&(rc=f->io.irq_stop(f->io.context,f->owner,f->epoch)))return held(f,rc);
    if((rc=f->io.release_input(f->io.context)))return held(f,rc);f->state=EldenWakeFlowStopped;return 0;
}
int elden_wake_flow_normal_input(const EldenWakeFlow *f){return f&&f->initialized&&f->state==EldenWakeFlowNormal&&!f->paused;}
