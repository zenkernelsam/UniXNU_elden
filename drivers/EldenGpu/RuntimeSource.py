"""Derive the Elden runtime from a hash-pinned real A830 implementation."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def sha(b):return hashlib.sha256(b).hexdigest()
def once(s,a,b):
    if s.count(a)!=1:raise ValueError('Runtime source anchor not unique: '+a[:90])
    return s.replace(a,b)
def board():
    spec=json.loads((HERE/'upstream/source.json').read_text());raw=(HERE/'upstream/PaganiGpu.cpp').read_bytes().replace(b'\r\n',b'\n')
    if sha(raw)!=spec['sha256']:raise ValueError('Donor runtime source changed')
    s=raw.decode().replace('\r\n','\n')
    # Keep the real platform operations; discard phone-only status integration.
    s=s[:s.index('extern "C" int pagani_gpu_start()')]
    s=once(s,'#include "PaganiGpuVotes.h"','#include "EldenGpuVotes.h"\n#include "Runtime.h"\n#include "Sm8750Rpmh.h"')
    s=once(s,'#include "PeripheralStatus.h"','')
    s=once(s,'int startup_result=kIOReturnNotReady;','')
    for line in ['extern "C" void boot_progress_display_status(const char *,uint64_t);',
                 'extern "C" int boot_log_export_put(uint32_t,uint32_t,uint32_t,const void *,uint32_t) {return -1;}',
                 'extern "C" void pagani_gpu_trace_start();']:
        s=once(s,line,'')
    s=once(s,'namespace {','''extern "C" int elden_recovery_armed(void);
extern "C" int elden_recovery_rpc_ready(void);
extern "C" IOReturn native_adreno_quiesce_for_reboot(uint32_t);
extern "C" void native_adreno_close_gate(void);
extern "C" void elden_gpu_framebuffer_block_for_reboot(void);
namespace {
static volatile uint32_t runtimeState=EldenGpuIdle,runtimeStage,runtimeResult,stopRequested;
static uint64_t targetHz,startDeadline;
static uint64_t clockUs(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static bool ioAllowed(){unsigned state=__atomic_load_n(&runtimeState,__ATOMIC_ACQUIRE);
    return state==EldenGpuStarting?(!__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE)&&clockUs()<startDeadline):
        state==EldenGpuReady||state==EldenGpuClosing;}
''')
    s=once(s,'    bool open(uint64_t physical,size_t length,bool memory=false) {','    bool open(uint64_t physical,size_t length,bool memory=false) {\n        if(!ioAllowed())return false;')
    s=once(s,'return cpu && !(at&3)','return ioAllowed() && cpu && !(at&3)')
    s=once(s,'struct State {','''struct State {
    static void *operator new(size_t bytes) noexcept {return IOMallocZero(bytes);}
    static void operator delete(void *p,size_t bytes) noexcept {if(p)IOFree(p,bytes);}''')
    s=once(s,'} g;','};\nstatic State *context;\n#define g (*context)')
    s=once(s,'__atomic_store_n(&current_stage,name,__ATOMIC_RELEASE);boot_progress_display_status(name,value);',
              '__atomic_store_n(&current_stage,name,__ATOMIC_RELEASE);__atomic_fetch_add(&runtimeStage,1u,__ATOMIC_RELAXED);')
    s=s.replace('Pagani GPU:', 'Elden GPU:')
    begin=s.index('int vote(const rpmh_command *commands,unsigned count) {');end=s.index('\nuint32_t maximum',begin)
    s=s[:begin]+'''int vote(const rpmh_command *commands,unsigned count) {
    if(!count||count>16||!ioAllowed())return -22;
    Sm8750RpmhCmd request[16];for(unsigned i=0;i<count;++i)request[i]={commands[i].address,commands[i].value};
    const Sm8750RpmhIo io={nullptr,rsc_read,rsc_write,rsc_delay};Sm8750RpmhResult result{};
    int rc=Sm8750RpmhWriteActive(&io,request,count,7,&result);
    if(rc)stage("GPU shared RPMh failed",(uint64_t(result.tcs)<<32)|uint32_t(rc));
    return rc;
}'''+s[end:]
    s=once(s,'if(PaganiGpuBuildStartup(&g.votes,g.speed_bin,g.soc_code,&g.startup,&g.initial,&g.bandwidth))return -1;',
              'if(elden_gpu_build_startup(&g.votes,g.speed_bin,targetHz,&g.startup,&g.initial,&g.bandwidth))return -1;')
    s=once(s,'g.gpucc.open(0x3d90000,0xa000)','g.gpucc.open(0x3d90000,0x9800)')
    # TB322FC Linux arm-smmu-qcom: PREFETCH_DEEP|CPRE|CMTLB for both
    # qcom,adreno and qcom,adreno-gmu. Do not inherit the phone's extra bits.
    s=once(s,'g.smmu.write(g.smmu_offset,0x32b) || g.smmu.read(g.smmu_offset,&g.smmu_value) || g.smmu_value!=0x32b',
              'g.smmu.write(g.smmu_offset,0x303) || g.smmu.read(g.smmu_offset,&g.smmu_value) || g.smmu_value!=0x303')
    # Reuse maximum-vote retention while shared firmware consumers have no
    # precise live ledger. Do not lower a shared rail on shutdown.
    s=s.replace('// shortcut: retain maximum valid shared corners; replace when inherited display/CPU votes have a common owner.',
       '// Initial retention policy: keep highest valid shared corners; do not lower inherited consumers. Dynamic power management is separate.')
    s+=r'''
extern "C" int elden_gpu_start(uint64_t hz) {
    if(!hz||hz>1100000000ULL||!elden_recovery_armed()||!elden_recovery_rpc_ready())return kIOReturnNotReady;
    uint32_t idle=EldenGpuIdle;
    if(!__atomic_compare_exchange_n(&runtimeState,&idle,EldenGpuStarting,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return kIOReturnBusy;
    __atomic_store_n(&targetHz,hz,__ATOMIC_RELEASE);startDeadline=clockUs()+10000000ULL;
    __atomic_store_n(&context,new State,__ATOMIC_RELEASE);
    int rc=context?start():kIOReturnNoMemory;
    if(!rc&&__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE))rc=kIOReturnAborted;
    if(rc)native_adreno_close_gate();
    __atomic_store_n(&runtimeResult,uint32_t(rc),__ATOMIC_RELAXED);
    __atomic_store_n(&runtimeState,rc?EldenGpuHeld:EldenGpuReady,__ATOMIC_RELEASE);
    IOLog("Elden GPU startup rc=%x requested=%llu retained=1\n",unsigned(rc),hz);
    return rc;
}
extern "C" int elden_gpu_quiesce_for_reboot(uint32_t budget) {
    if(!budget||budget>2000000)return kIOReturnBadArgument;
    __atomic_store_n(&stopRequested,1u,__ATOMIC_RELEASE);
    unsigned state=__atomic_load_n(&runtimeState,__ATOMIC_ACQUIRE);
    if(state==EldenGpuIdle){unsigned old=EldenGpuIdle;__atomic_compare_exchange_n(&runtimeState,&old,EldenGpuDrained,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);state=__atomic_load_n(&runtimeState,__ATOMIC_ACQUIRE);}
    if(state==EldenGpuDrained)return 0;
    if(state!=EldenGpuReady)return kIOReturnBusy;
    unsigned ready=EldenGpuReady;
    if(!__atomic_compare_exchange_n(&runtimeState,&ready,EldenGpuClosing,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return kIOReturnBusy;
    int rc=native_adreno_quiesce_for_reboot(budget);
    if(!rc)elden_gpu_framebuffer_block_for_reboot();
    __atomic_store_n(&runtimeResult,uint32_t(rc),__ATOMIC_RELAXED);
    __atomic_store_n(&runtimeState,rc?EldenGpuHeld:EldenGpuDrained,__ATOMIC_RELEASE);
    return rc;
}
extern "C" void elden_gpu_runtime_status(EldenGpuRuntimeStatus *out) {
    if(!out)return;
    out->state=__atomic_load_n(&runtimeState,__ATOMIC_ACQUIRE);
    out->stage=__atomic_load_n(&runtimeStage,__ATOMIC_RELAXED);
    out->status=__atomic_load_n(&runtimeResult,__ATOMIC_RELAXED);
    out->retained=__atomic_load_n(&context,__ATOMIC_ACQUIRE)!=nullptr;out->requested_hz=__atomic_load_n(&targetHz,__ATOMIC_ACQUIRE);
}
'''
    return s.encode()

def publisher(raw):
    # Function names/init ABI stay intact; add a caller gate and bounded drain.
    spec=json.loads((HERE/'upstream/publisher-source.json').read_text())
    if sha(raw.replace(b'\r\n',b'\n'))!=spec['sha256']:raise ValueError('Publisher source changed; review the new upstream first')
    s=raw.decode().replace('\r\n','\n')
    s=once(s,'extern "C" int boot_log_export_put(uint32_t generation,uint32_t total,uint32_t offset,const void *data,uint32_t size) __attribute__((weak_import));',
        '''// Elden has no persistent GPU export provider. A static null provider
// preserves the original guarded refusal path without an unreachable BL to0.
using EldenGpuBootLogProvider=int (*)(uint32_t,uint32_t,uint32_t,const void *,uint32_t);
static constexpr EldenGpuBootLogProvider boot_log_export_put=nullptr;''')
    s=once(s,'struct Published {','#include "CallGate.h"\nstatic EldenGpuCallGate eldenGate;\nstruct Published {')
    s=once(s,'        IOSleep(500);\n        IOLockLock(g.lock);','''        IOSleep(500);
        if(eldenGpuClosed(&eldenGate)){thread_terminate(current_thread());return;}
        IOLockLock(g.lock);
        if(eldenGpuClosed(&eldenGate)){IOLockUnlock(g.lock);thread_terminate(current_thread());return;}''')
    s=once(s,'        IOSleep(1);\n        if(!gMailboxOwner',
        '        IOSleep(1);\n        if(eldenGpuClosed(&eldenGate)){thread_terminate(current_thread());return;}\n        if(!gMailboxOwner')
    # Gate complete calls, including sections where existing code drops g.lock.
    for signature,refusal in [('bool AGXDeviceUserClient::mount(', 'false'),
        ('static IOReturn runCore(AGXDeviceUserClient *connection,', 'kIOReturnNotReady'),
        ('IOReturn AGXDeviceUserClient::clientMemoryForType(', 'kIOReturnNotReady'),
        ('IOReturn AGXDeviceUserClient::externalMethod(', 'kIOReturnNotReady')]:
        if s.count(signature)!=1:raise ValueError('Publisher entry changed: '+signature)
        at=s.index('{',s.index(signature))+1
        prefix=''
        if 'clientMemoryForType' in signature:
            s=once(s,'    if(memory)*memory=nullptr;','')
            prefix='\n    if(memory)*memory=nullptr;'
        s=s[:at]+prefix+'\n    EldenGpuCallLease lease(&eldenGate);if(!lease)return '+refusal+';'+s[at:]
    s+='\n'+(HERE/'PublisherQuiesce.inc').read_text()
    return s.encode()

def memory(raw):
    spec=json.loads((HERE/'upstream/memory-source.json').read_text())
    if sha(raw.replace(b'\r\n',b'\n'))!=spec['sha256']:raise ValueError('Memory source changed; review required')
    s=raw.decode().replace('\r\n','\n')
    start=s.index('#if defined(NATIVE_ADRENO_HOST_TEST) && !defined(__APPLE__)\nextern "C" unsigned int native_pmap_user_mappings')
    end=s.index('#endif',start)+len('#endif')
    if 'weak_import' not in s[start:end]:raise ValueError('Mapping diagnostic declaration changed')
    s=s[:start]+'''// Optional observation only: no pmap mapping reporter exists in this
// kernel. Keep the original null guard and all allocation/free protections.
using EldenGpuMappingObserver=unsigned int (*)(ppnum_t,void **,uint64_t *,char *);
static constexpr EldenGpuMappingObserver native_pmap_user_mappings=nullptr;'''+s[end:]
    return s.encode()
