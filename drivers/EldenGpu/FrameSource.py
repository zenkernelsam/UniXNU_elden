"""Bind the existing Elden CPU framebuffer to an owned GPU handoff presenter."""
from pathlib import Path
import hashlib,json
from RuntimeSource import once
def presenter(raw):
    spec=json.loads((Path(__file__).resolve().parent/'upstream/presenter-source.json').read_text())
    if hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()!=spec['sha256']:raise ValueError('Presenter source changed; review required')
    s=raw.decode().replace('\r\n','\n')
    s=once(s,'volatile uint32_t native_gop_wait_reasons[4];\n',
           '''volatile uint32_t native_gop_wait_reasons[4];
/* The KMD fence state at the two hand-off edges, for the frame diagnostics: [0] the sequence the display's offer got and the fences the
 * KMD had [1] submitted and [2] retired at that edge, [3] the sequence completed and [4]/[5] the same counters, [6] the last completion
 * status, [7] snapshots refused (the GPU not published or its submit lock busy). The swap's sequence is the frame's id: the paired rows
 * bind each accepted frame's wait to the KMD work it produced (the same record the display's gpuWrote keeps for its direct scanout). */
volatile uint64_t native_gop_gpu_fences[8];
bool native_adreno_fences_detail(uint32_t *,uint32_t *,uint32_t *) __attribute__((weak_import));
''')
    s=once(s,'    bool gpuMode=false,gpuClient=false;',
           '    bool gpuMode=false,gpuClient=false;\n    task_t handoffOwner=TASK_NULL;uint64_t handoffEpoch=0;')
    s=once(s,'    void dropKept();','''    void dropKept();
    /* The fence snapshot for native_gop_gpu_fences[slot..slot+2]: the sequence always lands; the KMD counters only while the publisher's
     * gate lets the look succeed (weak import: absent on a link without the Adreno backend). */''')
    s=once(s,'    IOReturn waitSwap(task_t,uint64_t timeoutMs,NGPSwap *);',
           '    IOReturn waitSwap(task_t,uint64_t timeoutMs,NGPSwap *);\n    void noteFences(unsigned slot,uint64_t sequence);')
    s=once(s,'    void abandonSwap();','    void abandonSwap(task_t task=TASK_NULL);')
    s=once(s,'    IOReturn completeSwap(uint64_t sequence,int status);','    IOReturn completeSwap(uint64_t sequence,int status,task_t task=TASK_NULL);')
    s=once(s,'    static IOMemoryDescriptor *handoffMemory();','    static IOMemoryDescriptor *handoffMemory();\n    static void handoffClose(task_t);')
    s=once(s,'memorySource:provider)->mapDeviceMemoryWithIndex(0,kIOMapInhibitCache)',
           'memorySource:provider)->mapDeviceMemoryWithIndex(0,kIOMapWriteCombineCache)')
    s=once(s,'PE_parse_boot_argn("fb-gpu",&v,sizeof v)','PE_parse_boot_argn("elden-gpu",&v,sizeof v)')
    s=once(s,'        native_gop_gpu_offers=native_gop_gpu_offers+1;\n',
           '        native_gop_gpu_offers=native_gop_gpu_offers+1;\n        noteFences(0,swap.sequence);\n')
    s=once(s,'    gpuClient=true;','''    if(!task||(handoffOwner&&handoffOwner!=task)){IOLockUnlock(gate);return kIOReturnNotPermitted;}
    if(!handoffOwner){task_reference(task);handoffOwner=task;}
    const uint64_t epoch=handoffEpoch;
    gpuClient=true;''')
    s=once(s,'while(online && (!swap.offered || swap.delivered))',
           'while(online && handoffOwner==task && handoffEpoch==epoch && (!swap.offered || swap.delivered))')
    s=once(s,'    if(!online) {IOLockUnlock(gate);return kIOReturnNotReady;}',
           '    if(!online||handoffOwner!=task||handoffEpoch!=epoch) {IOLockUnlock(gate);return kIOReturnNotReady;}')
    s=once(s,'            Kept *victim=&kept[0];','''            if(!online||handoffOwner!=task||handoffEpoch!=epoch) {
                IOLockUnlock(gate);memory->release();map->release();return kIOReturnNotReady;
            }
            Kept *victim=&kept[0];''')
    s=once(s,'if(!map || !map->getVirtualAddress() || !swap.offered || swap.sequence!=sequence)',
           'if(!map || !map->getVirtualAddress() || !online || handoffOwner!=task || handoffEpoch!=epoch || !swap.offered || swap.sequence!=sequence)')
    s=once(s,'IOLockUnlock(gate);OSSafeReleaseNULL(map);__atomic_fetch_add(&native_gop_gpu_failures,1,__ATOMIC_RELAXED);return kIOReturnNoMemory;',
           '''bool ours=online&&handoffOwner==task&&handoffEpoch==epoch&&swap.offered&&swap.sequence==sequence;
        IOLockUnlock(gate);OSSafeReleaseNULL(map);
        if(ours)(void)completeSwap(sequence,kIOReturnNoMemory,task);
        else __atomic_fetch_add(&native_gop_gpu_failures,1,__ATOMIC_RELAXED);
        return kIOReturnNoMemory;''')
    s=once(s,'IOReturn NativeGOPPresenter::completeSwap(uint64_t sequence,int status)',
           'IOReturn NativeGOPPresenter::completeSwap(uint64_t sequence,int status,task_t task)')
    s=once(s,'if(!swap.offered || !swap.delivered || swap.sequence!=sequence)',
           'if((task&&handoffOwner!=task) || !swap.offered || !swap.delivered || swap.sequence!=sequence)')
    s=once(s,'native_legacy_gpu_complete(done.display,status);return kIOReturnSuccess;',
           'noteFences(3,sequence);native_gop_gpu_fences[6]=uint64_t(uint32_t(status));native_legacy_gpu_complete(done.display,status);return kIOReturnSuccess;')
    begin=s.index('void NativeGOPPresenter::abandonSwap()');end=s.index('\nextern "C" void native_gop_presenter_set_source',begin)
    s=s[:begin]+'''void NativeGOPPresenter::abandonSwap(task_t task) {
    if(!gate)return;
    Kept all[kKept];IOLockLock(gate);
    if(task&&handoffOwner!=task){IOLockUnlock(gate);return;}
    task_t previous=handoffOwner;handoffOwner=TASK_NULL;++handoffEpoch;gpuClient=false;
    auto done=swap;swap={};
    for(unsigned i=0;i<kKept;++i){all[i]=kept[i];kept[i]={};}
    IOLockWakeup(gate,&swap,false);IOLockUnlock(gate);
    for(Kept &k:all){OSSafeReleaseNULL(k.map);OSSafeReleaseNULL(k.memory);if(k.task)task_deallocate(k.task);}
    if(previous)task_deallocate(previous);
    if(!done.offered)return;
    OSSafeReleaseNULL(done.map);if(done.memory)done.memory->release();
    __atomic_fetch_add(&native_gop_gpu_abandoned,1,__ATOMIC_RELAXED);
    native_legacy_gpu_complete(done.display,kIOReturnAborted);
}
void NativeGOPPresenter::handoffClose(task_t task){NativeGOPPresenter *p=gHandoff;if(p)p->abandonSwap(task);}
extern "C" void native_gop_handoff_close(task_t task){NativeGOPPresenter::handoffClose(task);}
'''+s[end:]
    s=once(s,'extern "C" void native_gop_presenter_set_source(IOService *presenter,IOService *nub) {',
           '''void NativeGOPPresenter::noteFences(unsigned slot,uint64_t sequence) {
    native_gop_gpu_fences[slot]=sequence;
    uint32_t submitted=0,retired=0,detail[3]={};
    if(&native_adreno_fences_detail && native_adreno_fences_detail(&submitted,&retired,detail)) {
        native_gop_gpu_fences[slot+1]=submitted;native_gop_gpu_fences[slot+2]=retired;
    } else native_gop_gpu_fences[7]=native_gop_gpu_fences[7]+1;
}
extern "C" void native_gop_presenter_set_source(IOService *presenter,IOService *nub) {''')
    s=once(s,'p->completeSwap(q.sequence,(int)q.status)','p->completeSwap(q.sequence,(int)q.status,task)')
    s=once(s,'client->controller->completeSwap(q.sequence,(int)q.status)','client->controller->completeSwap(q.sequence,(int)q.status,client->task)')
    s=once(s,'if(controller)controller->abandonSwap();','if(controller)controller->abandonSwap(task);')
    return s.encode()
def display(raw):
    # This exact source is the current source-build provider, not an old script copy.
    if hashlib.sha256(raw).hexdigest()!='7414c13c1132c63ebdc0f5bb7fdcb90e23e19cadad5a2a70d283bd31dd0c2fba':raise ValueError('Current display provider changed')
    s=raw.decode().replace('\r\n','\n')
    s='#include "FrameBridge.h"\n'+s
    s=once(s,'activeNub=nub;stage=5;OSSafeReleaseNULL(dtParent);',
           'activeNub=nub;stage=5;OSSafeReleaseNULL(dtParent);\n        elden_gpu_display_ready(nub);')
    # The pinned display driver calls native_legacy_gpu_offer with six arguments (the last is the surface's real format: NGP_BGRX8888 or a
    # half-float code for the render server's RGhA presents). The old stub took five; C linkage silently dropped the format and the bridge
    # hardcoded BGRX. Forward it, so the presenter reports the actual pixel format.
    s=once(s,'bool native_legacy_gpu_offer(void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t) {return false;}',
           'bool native_legacy_gpu_offer(void *d,IOMemoryDescriptor *m,uint64_t stride,uint32_t w,uint32_t h,uint32_t f) {return elden_gpu_frame_offer(d,m,stride,w,h,f);}')
    # Elden frame diagnostics (native_elden_frame_diag, owned by FrameBridge.cpp): the display's fallback reports and CPU copy timing
    # land in [6]/[7] and [8]/[9]; accepted swap validations in [15]. The remaining slots are the bridge's offer/fence counters.
    s=once(s,'void elden_keyevents_start(void);',
           'extern volatile uint64_t native_elden_frame_diag[16];\nvoid elden_keyevents_start(void);')
    s=once(s,'void native_legacy_gpu_fallback_observed(int status) {report("\\nELDEN_DISPLAY unexpected GPU fallback=%d\\n",status);}',
           'void native_legacy_gpu_fallback_observed(int status) {native_elden_frame_diag[6]=native_elden_frame_diag[6]+1;native_elden_frame_diag[7]=uint64_t(uint32_t(status));report("\\nELDEN_DISPLAY unexpected GPU fallback=%d\\n",status);}')
    s=once(s,'void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes) {',
           'void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes) {\n    native_elden_frame_diag[8]=native_elden_frame_diag[8]+1;native_elden_frame_diag[9]=native_elden_frame_diag[9]+bytes;')
    s=once(s,'    if(++validations<=32 || (!accepted && validations%128==0))',
           '    if(accepted)native_elden_frame_diag[15]=native_elden_frame_diag[15]+1;\n    if(++validations<=32 || (!accepted && validations%128==0))')
    s=once(s,'kIOMemoryTypePhysical64|kIODirectionInOut|',
           'IOOptionBits(kIOMemoryTypePhysical64)|IOOptionBits(kIODirectionInOut)|')
    return s.encode()
