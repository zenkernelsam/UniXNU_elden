"""Bind the existing Elden CPU framebuffer to an owned GPU handoff presenter."""
from pathlib import Path
import hashlib,json
from RuntimeSource import once
def presenter(raw):
    spec=json.loads((Path(__file__).resolve().parent/'upstream/presenter-source.json').read_text())
    if hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()!=spec['sha256']:raise ValueError('Presenter source changed; review required')
    s=raw.decode().replace('\r\n','\n')
    s=once(s,'    bool gpuMode=false,gpuClient=false;',
           '    bool gpuMode=false,gpuClient=false;\n    task_t handoffOwner=TASK_NULL;uint64_t handoffEpoch=0;')
    s=once(s,'    void abandonSwap();','    void abandonSwap(task_t task=TASK_NULL);')
    s=once(s,'    IOReturn completeSwap(uint64_t sequence,int status);','    IOReturn completeSwap(uint64_t sequence,int status,task_t task=TASK_NULL);')
    s=once(s,'    static IOMemoryDescriptor *handoffMemory();','    static IOMemoryDescriptor *handoffMemory();\n    static void handoffClose(task_t);')
    s=once(s,'memorySource:provider)->mapDeviceMemoryWithIndex(0,kIOMapInhibitCache)',
           'memorySource:provider)->mapDeviceMemoryWithIndex(0,kIOMapWriteCombineCache)')
    s=once(s,'PE_parse_boot_argn("fb-gpu",&v,sizeof v)','PE_parse_boot_argn("elden-gpu",&v,sizeof v)')
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
    s=once(s,'bool native_legacy_gpu_offer(void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t) {return false;}',
           'bool native_legacy_gpu_offer(void *d,IOMemoryDescriptor *m,uint64_t stride,uint32_t w,uint32_t h) {return elden_gpu_frame_offer(d,m,stride,w,h);}')
    s=once(s,'kIOMemoryTypePhysical64|kIODirectionInOut|',
           'IOOptionBits(kIOMemoryTypePhysical64)|IOOptionBits(kIODirectionInOut)|')
    return s.encode()
