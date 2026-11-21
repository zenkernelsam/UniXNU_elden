/* SPDX-License-Identifier: BSD-2-Clause-Patent */
struct EldenCameraFrameOutput {
    EldenCameraDmaCapture *capture;unsigned sensor,count;uint64_t owner,epoch,route_epoch,next_ticket;
    IOLock *lock;IOBufferMemoryDescriptor *memory[4];uint8_t *bytes[4];
    uint32_t state[4];EldenCameraOutputFrame frame[4];EldenCameraFrame input;
    uint32_t input_held,seen,irq_enabled;EldenCameraOutputStatus status;uint64_t frame_bytes;
};
namespace {
static uint32_t camera_output_attempted;
static uint32_t output_bayer(const EldenCameraMode &mode){
    return mode.bayer==EldenCameraGrbg?EldenRawGrbg:EldenRawRggb;
}
static int output_fail(EldenCameraFrameOutput *c,int rc){if(!c->status.error)c->status.error=rc<0?rc:-13;return rc<0?rc:-13;}
static int output_admit(EldenCameraFrameOutput *c,int cleanup){
    if(!c||!c->capture||ml_at_interrupt_context())return -22;
    if(!cleanup&&(c->status.error||c->status.stopping))return c->status.error?c->status.error:-125;
    if(!cleanup&&!c->status.prepared)return -19;
    return elden_camera_dma_admit(c->capture->dma,c->owner,c->epoch,cleanup);
}
static int output_return_input(EldenCameraFrameOutput *c,int requeue){
    if(!c->input_held)return 0;
    int rc=elden_camera_dma_capture_release(c->capture,c->sensor,&c->input,0);
    if(rc)return rc;c->input_held=0;
    return requeue?elden_camera_native_pipeline_queue(c->owner,c->epoch,c->sensor,&c->input.buffer):0;
}
}
extern "C" int elden_camera_output_open(EldenCameraDmaCapture *capture,unsigned sensor,unsigned count,EldenCameraFrameOutput **out){
    if(!capture||!capture->initialized||sensor>1||count<2||count>4||!out||*out||ml_at_interrupt_context())return -22;
    if(!capture->board.prepared[sensor]||capture->board.stopped[sensor])return -19;
    EldenCameraPipelineStatus live={};int rc=elden_camera_native_pipeline_status(sensor,&live);
    if(rc||live.error||!live.started||live.stopped||live.state!=EldenPipeStreaming)return rc?rc:-19;
    EldenCameraDmaView memory={};rc=elden_camera_dma_view(capture->dma,capture->owner,capture->epoch,&memory);if(rc)return rc;
    const auto &mode=capture->board.configs[sensor].mode;uint32_t stride=sensor?4080u:5104u;
    uint64_t bytes=(uint64_t)stride*mode.height;
    if(bytes!=live.frame_bytes||stride!=live.stride||mode.raw_bits!=10||mode.width!=(sensor?3264u:4080u)||mode.height!=(sensor?2448u:3072u)||
       mode.bayer!=(sensor?EldenCameraRggb:EldenCameraGrbg)||mode.rotation!=(sensor?270:90)||capture->route_epoch!=memory.lease.route_epoch)return -116;
    uint32_t old=__atomic_load_n(&camera_output_attempted,__ATOMIC_ACQUIRE),mask=1u<<sensor;
    for(;;){if(old&mask)return -16;if(__atomic_compare_exchange_n(&camera_output_attempted,&old,old|mask,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;}
    auto *c=(EldenCameraFrameOutput *)IOMallocData(sizeof(EldenCameraFrameOutput));if(!c)return -12;bzero(c,sizeof(*c));
    c->capture=capture;c->sensor=sensor;c->count=count;c->owner=capture->owner;c->epoch=capture->epoch;c->route_epoch=capture->route_epoch;c->frame_bytes=bytes;*out=c;
    c->lock=IOLockAlloc();if(!c->lock)return output_fail(c,-12);
    for(unsigned i=0;i<count;i++){
        c->memory[i]=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,(IOOptionBits)kIODirectionInOut|(IOOptionBits)kIOMemoryKernelUserShared|(IOOptionBits)kIOMemoryMapperNone,bytes,4096);
        if(!c->memory[i]||elden_camera_output_prepare(c->memory[i],kIODirectionInOut))return output_fail(c,-12);
        c->bytes[i]=(uint8_t *)elden_camera_output_bytes(c->memory[i]);if(!c->bytes[i])return output_fail(c,-12);
        bzero(c->bytes[i],(size_t)bytes);
    }
    c->status.prepared=1;return 0;
}
extern "C" int elden_camera_output_pump(EldenCameraFrameOutput *c){
    if(!c||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=output_admit(c,0);
    if(rc){IOLockUnlock(c->lock);return rc;}if(c->input_held){rc=output_fail(c,-16);IOLockUnlock(c->lock);return rc;}
    rc=elden_camera_dma_capture_service(c->capture,c->sensor);
    if(!rc)rc=elden_camera_dma_capture_frame(c->capture,c->sensor,&c->input);
    if(rc){if(rc!=-11)output_fail(c,rc);IOLockUnlock(c->lock);return rc;}c->input_held=1;
    const auto &f=c->input;
    if(f.error||f.buffer.owner!=c->owner||f.buffer.epoch!=c->epoch||f.buffer.route_epoch!=c->route_epoch||!f.buffer.mapping||
       f.bytesused!=c->frame_bytes||f.buffer.bytes!=c->frame_bytes||!f.timestamp_ns||(c->seen&&f.sequence<=c->status.last_sequence)||f.timestamp_ns<c->status.last_ns){
        rc=output_fail(c,-116);int returned=output_return_input(c,0);if(returned)rc=output_fail(c,returned);IOLockUnlock(c->lock);return rc;}
    unsigned slot=c->count;
    for(unsigned i=0;i<c->count;i++)if(!c->state[i]){slot=i;break;}
    if(slot==c->count)for(unsigned i=0;i<c->count;i++)if(c->state[i]==1&&(slot==c->count||c->frame[i].sequence<c->frame[slot].sequence))slot=i;
    if(slot==c->count){c->status.dropped++;rc=output_return_input(c,1);if(rc)output_fail(c,rc);IOLockUnlock(c->lock);return rc?rc:-11;}
    if(c->state[slot]==1)c->status.dropped++;c->state[slot]=3;
    if(c->next_ticket==UINT64_MAX){rc=output_fail(c,-75);(void)output_return_input(c,0);IOLockUnlock(c->lock);return rc;}
    // Source is genuinely CPU-borrowed after real done/visibility. Keep that
    // borrow through the whole copy; no overwrite of any consumer-held slot.
    memcpy(c->bytes[slot],f.buffer.mapping,(size_t)c->frame_bytes);
    c->frame[slot]={++c->next_ticket,f.sequence,f.timestamp_ns,c->frame_bytes,slot,c->capture->board.configs[c->sensor].mode.width,
        c->capture->board.configs[c->sensor].mode.height,c->sensor?4080u:5104u,output_bayer(c->capture->board.configs[c->sensor].mode),c->sensor?270u:90u,c->sensor};
    uint64_t sequence=f.sequence,time=f.timestamp_ns;rc=output_return_input(c,1);
    if(!rc){__atomic_thread_fence(__ATOMIC_RELEASE);c->state[slot]=1;c->status.copied++;c->seen=1;c->status.last_sequence=sequence;c->status.last_ns=time;}
    else {c->state[slot]=3;output_fail(c,rc);}IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_output_enable_irq(EldenCameraFrameOutput *c){
    if(!c||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=output_admit(c,0);
    if(!rc&&c->irq_enabled)rc=-16;if(!rc)rc=elden_camera_native_pipeline_listen(c->owner,c->epoch,c->sensor);
    if(!rc)c->irq_enabled=1;IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_output_pump_irq(EldenCameraFrameOutput *c,unsigned ms){
    if(!c||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=output_admit(c,0);
    if(!rc&&!c->irq_enabled)rc=-19;IOLockUnlock(c->lock);if(rc)return rc;
    // No output lock across the real original worker's wait. The pending bit
    // is consumed by service, not wait; an event before sleep cannot disappear.
    rc=elden_camera_native_pipeline_wait(c->owner,c->epoch,c->sensor,ms);
    if(rc<0)return rc;if(!rc)return -11;return elden_camera_output_pump(c);
}
extern "C" int elden_camera_output_acquire(EldenCameraFrameOutput *c,EldenCameraOutputFrame *out){
    if(!c||!out||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=output_admit(c,0);unsigned slot=c->count;
    if(!rc)for(unsigned i=0;i<c->count;i++)if(c->state[i]==1&&(slot==c->count||c->frame[i].sequence>c->frame[slot].sequence))slot=i;
    if(!rc&&slot==c->count)rc=-11;
    if(!rc){
        /* This single producer lends the newest completed snapshot. Older
         * ready snapshots can no longer be delivered afterward; otherwise
         * the real client's monotonic sequence check would terminate capture.
         * Already-held snapshots remain untouched until their actual return. */
        for(unsigned i=0;i<c->count;i++)if(i!=slot&&c->state[i]==1&&c->frame[i].sequence<c->frame[slot].sequence){c->state[i]=0;c->status.dropped++;}
        c->state[slot]=2;*out=c->frame[slot];__atomic_thread_fence(__ATOMIC_ACQUIRE);
    }
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_output_release(EldenCameraFrameOutput *c,uint64_t ticket){
    if(!c||!ticket||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=-116;
    for(unsigned i=0;i<c->count;i++)if(c->state[i]==2&&c->frame[i].ticket==ticket){c->state[i]=0;rc=0;break;}
    IOLockUnlock(c->lock);return rc;
}
extern "C" void *elden_camera_output_memory(EldenCameraFrameOutput *c,uint64_t ticket,unsigned slot){
    if(!c||!c->lock||!ticket||slot>=c->count||ml_at_interrupt_context())return nullptr;IOLockLock(c->lock);IOBufferMemoryDescriptor *out=nullptr;
    if(c->state[slot]==2&&c->frame[slot].ticket==ticket){out=c->memory[slot];out->retain();}IOLockUnlock(c->lock);return out;
}
extern "C" int elden_camera_output_stop(EldenCameraFrameOutput *c){
    if(!c||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);c->status.stopping=1;
    int rc=output_return_input(c,0);EldenCameraDmaStatus dma={};EldenCameraPipelineStatus pipe={};
    if(!rc)rc=elden_camera_dma_status(c->capture->dma,&dma);if(!rc)rc=elden_camera_native_pipeline_status(c->sensor,&pipe);
    if(!rc&&(!dma.retired||!pipe.stopped||pipe.active||c->input_held))rc=-16;
    if(!rc){for(unsigned i=0;i<c->count;i++)if(c->state[i]!=2)c->state[i]=0;c->status.retired=1;}
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_output_status(EldenCameraFrameOutput *c,EldenCameraOutputStatus *out){
    if(!c||!out||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);*out=c->status;out->ready=out->held=0;
    for(unsigned i=0;i<c->count;i++){out->ready+=c->state[i]==1;out->held+=c->state[i]==2;}IOLockUnlock(c->lock);return 0;
}
extern "C" int elden_camera_output_info(EldenCameraFrameOutput *c,uint64_t out[8]){
    if(!c||!out||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);int rc=output_admit(c,0);
    if(!rc&&!c->status.prepared)rc=-19;
    if(!rc){const auto &m=c->capture->board.configs[c->sensor].mode;uint64_t values[]={1,c->sensor,m.width,m.height,c->sensor?4080u:5104u,m.raw_bits,output_bayer(m),m.rotation};
        for(unsigned i=0;i<8;i++)out[i]=values[i];}
    IOLockUnlock(c->lock);return rc;
}
