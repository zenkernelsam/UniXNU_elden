/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included after the actual native touch transfer/DMA/release methods. Only
 * that original thread executes Flow and all SPI/HID operations. PM threads
 * publish bounded commands and signal its IRQ/command wait events. */
static EldenTouchWakePower installed_wake_power={};static uint32_t wake_power_installed;
static Context *wake_touch_active;
static void wake_publish_status(Context *c){
    if(!c||!c->wake_lock)return;
    IOLockLock(c->wake_lock);
    c->wake_status={1,__atomic_load_n(&c->wake_ready,__ATOMIC_ACQUIRE),c->wake_flow.state,
        __atomic_load_n(&c->wake_paused,__ATOMIC_ACQUIRE),__atomic_load_n(&c->wake_sequence,__ATOMIC_ACQUIRE),
        __atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE),__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE),
        c->wake_flow.packets,c->wake_flow.gestures,__atomic_load_n(&c->wake_error,__ATOMIC_ACQUIRE)};
    IOLockUnlock(c->wake_lock);
}
static int wake_closed_result(Context *c){int rc=__atomic_load_n(&c->wake_error,__ATOMIC_ACQUIRE);return rc?rc:-125;}
static int wake_board_gate(Context *c,unsigned phase){
    if(!c||!c->wake_enabled||!c->wake_power.admit||ml_at_interrupt_context()||
       !__atomic_load_n(&lease,__ATOMIC_ACQUIRE)||c->platform.quarantined||c->device.poisoned||
       !elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted)return -13;
    if(phase!=EldenWakeAdmissionCleanup&&(__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)||elden_touch_run_state(&run_control)!=EldenTouchRunning))return -125;
    if((phase==EldenWakeAdmissionPaused||phase==EldenWakeAdmissionResume)&&!__atomic_load_n(&c->wake_paused,__ATOMIC_ACQUIRE))return -13;
    if(c->dma_mode){
        if(c->dma_transfer.active||c->dma_transfer.quarantined||c->route.poisoned||!c->route.attached||
           elden_touch_route_observe(&c->route,c->route.epoch)||dma_idle(c))return -16;
    }else if(c->region[EldenTouchSe].read(SE_GENI_STATUS)||c->region[EldenTouchSe].read(SE_GENI_TX_FIFO_STATUS)||
             c->region[EldenTouchSe].read(SE_GENI_RX_FIFO_STATUS))return -16;
    return c->wake_power.admit(c->wake_power.context,c->wake_power.owner,c->wake_power.epoch,phase);
}
static int wake_gate(void *v,uint64_t owner,uint64_t epoch,unsigned phase){
    Context *c=(Context *)v;return owner==c->wake_power.owner&&epoch==c->wake_power.epoch?wake_board_gate(c,phase):-116;
}
static int wake_pause(void *v,uint64_t owner,uint64_t epoch){
    Context *c=(Context *)v;
    unsigned phase=elden_touch_run_state(&run_control)==EldenTouchRunning?EldenWakeAdmissionNormal:EldenWakeAdmissionCleanup;
    int rc=wake_gate(c,owner,epoch,phase);if(!rc)__atomic_store_n(&c->wake_paused,1u,__ATOMIC_RELEASE);return rc;
}
static int wake_release(void *v){
    Context *c=(Context *)v;if(!__atomic_load_n(&c->wake_paused,__ATOMIC_ACQUIRE))return -13;
    if(c->previous.count){elden_nvt_frame empty={};int rc=elden_touch_hid_publish(&empty);if(rc)return rc;c->previous=empty;}
    return elden_pen_native_pause(); // same nonterminal release/clock/service
}
static int wake_irq_gate(void *v,uint64_t owner,uint64_t epoch,int cleanup){
    return wake_gate(v,owner,epoch,cleanup?EldenWakeAdmissionCleanup:EldenWakeAdmissionPaused);
}
static int wake_irq_acquire(void *v,uint64_t owner,uint64_t epoch){return elden_wake_native_irq_acquire(owner,epoch,v,wake_irq_gate);}
static int wake_irq_arm(void *,uint64_t owner,uint64_t epoch){return elden_wake_native_irq_arm(owner,epoch);}
static int wake_irq_disarm(void *,uint64_t owner,uint64_t epoch){return elden_wake_native_irq_disarm(owner,epoch);}
static int wake_irq_stop(void *,uint64_t owner,uint64_t epoch){return elden_wake_native_irq_stop(owner,epoch);}
static int wake_irq_take(void *,uint64_t owner,uint64_t epoch,uint32_t *events){return elden_wake_native_irq_take(owner,epoch,events);}
static int wake_command(void *v,int gesture){
    Context *c=(Context *)v;int rc=wake_board_gate(c,EldenWakeAdmissionPaused);
    if(!rc)rc=elden_nvt_low_power_command(&c->device,gesture!=0);
    if(!rc){delay(c,50000);rc=wake_board_gate(c,EldenWakeAdmissionPaused);} // exact OEM suspend delay
    return rc;
}
static int wake_packet(void *v,uint8_t out[109]){Context *c=(Context *)v;
    int rc=wake_board_gate(c,EldenWakeAdmissionPaused);return rc?rc:elden_nvt_points(&c->device,out);}
static int wake_request_power(void *v,uint64_t owner,uint64_t epoch,unsigned gesture){Context *c=(Context *)v;
    int rc=wake_gate(c,owner,epoch,EldenWakeAdmissionPaused);return rc?rc:c->wake_power.request_wake(c->wake_power.context,owner,epoch,gesture);}
static int wake_restore(void *v){
    Context *c=(Context *)v;int rc=wake_board_gate(c,EldenWakeAdmissionResume);
    if(!rc)rc=elden_nvt_upload(&c->device,&c->wake_image);
    if(!rc&&(!c->device.ready||c->device.pid!=0x6097||c->device.reset_state<0xa0||c->device.reset_state>0xaf))rc=-19;
    return rc?rc:wake_board_gate(c,EldenWakeAdmissionResume);
}
static int wake_resume(void *v,uint64_t owner,uint64_t epoch){
    Context *c=(Context *)v;int rc=wake_gate(c,owner,epoch,EldenWakeAdmissionResume);
    if(!rc)rc=elden_pen_native_resume();if(!rc){__atomic_store_n(&c->wake_paused,0u,__ATOMIC_RELEASE);c->errors=0;c->hidWaits=0;}return rc;
}
static int wake_bind(Context *c,const EldenNvtImage *image){
    unsigned mode=0;PE_parse_boot_argn("elden-touch-wake",&mode,sizeof(mode));if(mode!=1)return 0;
    if(__atomic_load_n(&wake_power_installed,__ATOMIC_ACQUIRE)!=2)return -19;
    c->wake_power=installed_wake_power;c->wake_image=*image;c->wake_enabled=1;c->wake_thread=current_thread();c->wake_lock=IOLockAlloc();if(!c->wake_lock)return -12;
    EldenWakeFlowIo io={c,wake_gate,wake_pause,wake_release,wake_irq_acquire,wake_irq_arm,wake_irq_disarm,wake_irq_stop,wake_irq_take,
        wake_command,wake_packet,wake_request_power,wake_restore,wake_resume};
    int rc=elden_wake_flow_init(&c->wake_flow,&io,c->wake_power.owner,c->wake_power.epoch);
    if(!rc){__atomic_store_n(&c->wake_ready,1u,__ATOMIC_RELEASE);wake_publish_status(c);__atomic_store_n(&wake_touch_active,c,__ATOMIC_RELEASE);}return rc;
}
static void wake_signal(Context *c){
    if(!c)return;thread_wakeup((event_t)&c->wake_sequence);
    const void *event=elden_wake_native_irq_event();if(event)thread_wakeup((event_t)event);
}
static int wake_round(Context *c){
    if(!c->wake_enabled)return 0;
    if(!__atomic_load_n(&c->wake_ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE))return wake_closed_result(c);
    uint32_t request=__atomic_load_n(&c->wake_sequence,__ATOMIC_ACQUIRE);int rc=0;
    if(request!=__atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE)){
        IOLockLock(c->wake_lock);request=__atomic_load_n(&c->wake_sequence,__ATOMIC_RELAXED);
        bool asleep=c->wake_asleep!=0,gesture=c->wake_gesture!=0;IOLockUnlock(c->wake_lock);
        bool same=(!asleep&&c->wake_flow.state==EldenWakeFlowNormal)||
            (asleep&&((gesture&&c->wake_flow.state==EldenWakeFlowGesture)||(!gesture&&c->wake_flow.state==EldenWakeFlowDeep)));
        rc=same?0:asleep?elden_wake_flow_suspend(&c->wake_flow,gesture):elden_wake_flow_resume(&c->wake_flow);
        __atomic_store_n(&c->wake_result,rc,__ATOMIC_RELEASE);__atomic_store_n(&c->wake_ack,request,__ATOMIC_RELEASE);
        if(rc){
            if(c->wake_flow.state==EldenWakeFlowHeld){__atomic_store_n(&c->wake_error,rc,__ATOMIC_RELEASE);__atomic_store_n(&c->wake_closed,1u,__ATOMIC_RELEASE);}
            wake_publish_status(c);return rc;
        }
    }
    if(c->wake_flow.state==EldenWakeFlowGesture){
        rc=elden_wake_flow_irq(&c->wake_flow);if(rc){__atomic_store_n(&c->wake_error,rc,__ATOMIC_RELEASE);__atomic_store_n(&c->wake_closed,1u,__ATOMIC_RELEASE);wake_publish_status(c);return rc;}
    }
    wake_publish_status(c);
    if(__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE))return wake_closed_result(c);
    if(elden_wake_flow_normal_input(&c->wake_flow))return 0;
    // Wait on the exact event the owned IRQ wakes; deep mode uses command
    // event. Recheck after assert_wait closes IRQ/PM/stop arrival-before-sleep.
    // A decoded wake request or deep sleep has no more gesture work. A late
    // already-latched IRQ must not spin those states: resume drains it once.
    const void *irq_event=c->wake_flow.state==EldenWakeFlowGesture?elden_wake_native_irq_event():nullptr;
    event_t event=(event_t)(irq_event?irq_event:&c->wake_sequence);
    assert_wait(event,THREAD_UNINT);
    int pending=irq_event?elden_wake_native_irq_pending(c->wake_power.owner,c->wake_power.epoch):0;
    if(pending<0){
        clear_wait(current_thread(),THREAD_AWAKENED);
        __atomic_store_n(&c->wake_error,pending,__ATOMIC_RELEASE);__atomic_store_n(&c->wake_closed,1u,__ATOMIC_RELEASE);
        wake_publish_status(c);return pending; // no immediate-spin on a lost power/IRQ provider
    }
    if(pending||__atomic_load_n(&c->wake_sequence,__ATOMIC_ACQUIRE)!=__atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)||elden_touch_run_state(&run_control)!=EldenTouchRunning)clear_wait(current_thread(),THREAD_AWAKENED);
    else thread_block(THREAD_CONTINUE_NULL);
    return 1; // no normal finger/pen frame while paused
}
static int wake_stop(Context *c){
    if(!c->wake_enabled)return 0;__atomic_store_n(&c->wake_ready,0u,__ATOMIC_RELEASE);
    int rc=elden_wake_flow_stop(&c->wake_flow);if(rc)__atomic_store_n(&c->wake_error,rc,__ATOMIC_RELEASE);
    __atomic_store_n(&c->wake_closed,1u,__ATOMIC_RELEASE);wake_publish_status(c);wake_signal(c);
    // Context is still referenced by retained IRQ/power commands to reset.
    // A successful SPI/IRQ stop does not authorize freeing those mappings.
    return rc;
}
extern "C" int elden_touch_wake_install_power(const EldenTouchWakePower *p){
    if(!p||!p->owner||!p->epoch||!p->admit||!p->request_wake||ml_at_interrupt_context())return -22;
    int rc=p->admit(p->context,p->owner,p->epoch,EldenWakeAdmissionNormal);if(rc)return rc;uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&wake_power_installed,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    installed_wake_power=*p;__atomic_store_n(&wake_power_installed,2u,__ATOMIC_RELEASE);return 0;
}
extern "C" int elden_touch_wake_request(uint64_t owner,uint64_t epoch,int asleep,int gesture){
    Context *c=__atomic_load_n(&wake_touch_active,__ATOMIC_ACQUIRE);
    if(!c||owner!=c->wake_power.owner||epoch!=c->wake_power.epoch||(asleep!=0&&asleep!=1)||(gesture!=0&&gesture!=1)||ml_at_interrupt_context())return -22;
    if(!__atomic_load_n(&c->wake_ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE))return -19;
    if(current_thread()==c->wake_thread)return -16; // provider must queue actual power request/resume off this worker
    IOLockLock(c->wake_lock);uint32_t sequence=__atomic_load_n(&c->wake_sequence,__ATOMIC_RELAXED);
    if(c->wake_busy||sequence!=__atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE)||sequence==UINT32_MAX){IOLockUnlock(c->wake_lock);return -16;}
    if(__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)||elden_touch_run_state(&run_control)!=EldenTouchRunning){IOLockUnlock(c->wake_lock);return -125;}
    c->wake_busy=1;c->wake_asleep=(uint32_t)asleep;c->wake_gesture=(uint32_t)gesture;sequence++;
    __atomic_store_n(&c->wake_sequence,sequence,__ATOMIC_RELEASE);IOLockUnlock(c->wake_lock);wake_signal(c);
    // Bounded API acknowledgment; actual upload/transport/power results remain
    // separate. Timeout permanently withdraws this coordinator, never success.
    for(unsigned i=0;i<2500;i++){
        if(__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE)==sequence){
            int rc=__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)?wake_closed_result(c):__atomic_load_n(&c->wake_result,__ATOMIC_ACQUIRE);
            IOLockLock(c->wake_lock);c->wake_busy=0;IOLockUnlock(c->wake_lock);return rc;
        }
        IOSleep(1);
    }
    __atomic_store_n(&c->wake_error,-110,__ATOMIC_RELEASE);__atomic_store_n(&c->wake_closed,1u,__ATOMIC_RELEASE);wake_signal(c);
    IOLockLock(c->wake_lock);c->wake_busy=0;IOLockUnlock(c->wake_lock);return -110;
}
extern "C" int elden_touch_wake_status(uint64_t owner,uint64_t epoch,EldenTouchWakeStatus *out){
    Context *c=__atomic_load_n(&wake_touch_active,__ATOMIC_ACQUIRE);if(!c||!out||owner!=c->wake_power.owner||epoch!=c->wake_power.epoch)return -116;
    if(ml_at_interrupt_context())return -16;IOLockLock(c->wake_lock);*out=c->wake_status;
    // Command/error fields are atomic and may have advanced since the last
    // worker publish. Only immutable/locked copy reads worker-owned flow.
    out->requested=__atomic_load_n(&c->wake_sequence,__ATOMIC_ACQUIRE);out->acknowledged=__atomic_load_n(&c->wake_ack,__ATOMIC_ACQUIRE);
    out->closed=__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE);out->error=__atomic_load_n(&c->wake_error,__ATOMIC_ACQUIRE);
    IOLockUnlock(c->wake_lock);return 0;
}
