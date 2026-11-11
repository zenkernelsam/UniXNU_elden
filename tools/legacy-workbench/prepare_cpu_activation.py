"""Prepare and test the real ArmCpu queue/release methods in isolation."""
from pathlib import Path
import hashlib,importlib.util,json
r=Path(__file__).resolve().parents[1];p=r/'research/cpu-activation-20261111';p.mkdir(exist_ok=True)
script=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation/Deferred.py')
spec=importlib.util.spec_from_file_location('deferred',script);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
base=(r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes();new=mod.apply(base)
(p/'ArmCpu.deferred.cpp').write_bytes(new)
text=new.decode();drain=text[text.index('void ArmCpu::drainPending (void)'):text.index('bool ArmCpu::finishSecondary (void)')]
prefix='''#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <cstdint>
#define ARMCPU_SMP_SLOTS 8
class ArmCpu {
public:
 unsigned id; explicit ArmCpu(unsigned i):id(i){}
 bool finishSecondary();
 static void drainPending();
 static int releaseSecondaries();
};
static ArmCpu *volatile gPending[8];
static volatile uint32_t gBootRegistered;
static bool gSmpWanted;
static std::atomic<unsigned> calls[8];
static std::atomic<bool> ready{false};
static std::atomic<unsigned> hooks{0};
static unsigned deny_after=0;
bool ArmCpu::finishSecondary(){ calls[id].fetch_add(1); return true; }
'''
decl=mod.DECLARATIONS.replace('__attribute__((weak_import))','__attribute__((weak))')
hook='''
#ifndef MISSING_HOOK
extern "C" bool elden_smp_recovery_ready(void) {
 unsigned n=hooks.fetch_add(1)+1;
 return ready.load() && (!deny_after || n<=deny_after);
}
#endif
'''
tests='''
static void reset(){
 for(unsigned i=0;i<8;i++){gPending[i]=nullptr;calls[i]=0;}
 gBootRegistered=0;gSmpWanted=false;ready=false;hooks=0;deny_after=0;
#if ELDEN_ARMCPU_DEFERRED
 gEldenSmpReleased=0;
#endif
}
int main(){
 ArmCpu cpu[7]={ArmCpu(1),ArmCpu(2),ArmCpu(3),ArmCpu(4),ArmCpu(5),ArmCpu(6),ArmCpu(7)};
 reset();
#if !ELDEN_ARMCPU_DEFERRED
 assert(ArmCpu::releaseSecondaries()==-3);
 gPending[1]=&cpu[0];ArmCpu::drainPending();assert(calls[1]==1);
#elif defined(MISSING_HOOK)
 gBootRegistered=1;gSmpWanted=true;gPending[1]=&cpu[0];
 assert(ArmCpu::releaseSecondaries()==-2);ArmCpu::drainPending();assert(calls[1]==0&&gPending[1]);
#else
 gPending[1]=&cpu[0];ArmCpu::drainPending();assert(calls[1]==0);
 assert(ArmCpu::releaseSecondaries()==-1);
 gBootRegistered=1;assert(ArmCpu::releaseSecondaries()==-1);
 gSmpWanted=true;assert(ArmCpu::releaseSecondaries()==-2);assert(gPending[1]);
 ready=true;ArmCpu::drainPending();assert(calls[1]==0); // readiness alone must not release
 assert(ArmCpu::releaseSecondaries()==0);assert(calls[1]==1&&!gPending[1]);
 assert(ArmCpu::releaseSecondaries()==0);assert(calls[1]==1); // idempotent
 ready=false;gPending[2]=&cpu[1];ArmCpu::drainPending();assert(!calls[2]&&gPending[2]);
 ready=true;assert(ArmCpu::releaseSecondaries()==0);assert(calls[2]==1);
 // Losing readiness between registrations leaves remaining CPUs queued.
 reset();gBootRegistered=1;gSmpWanted=true;ready=true;deny_after=3;
 gPending[1]=&cpu[0];gPending[2]=&cpu[1];assert(ArmCpu::releaseSecondaries()==0);
 assert(calls[1]==1&&calls[2]==0&&gPending[2]);
 // Model arrivals racing the explicit release: the exact production exchange consumes each once.
 for(unsigned round=0;round<80;round++){
  reset();gBootRegistered=1;gSmpWanted=true;ready=true;
  std::vector<std::thread> threads;
  for(unsigned id=1;id<8;id++)threads.emplace_back([&,id]{
   __atomic_store_n(&gPending[id],&cpu[id-1],__ATOMIC_RELEASE);
   ArmCpu::drainPending();
  });
  threads.emplace_back([]{ArmCpu::releaseSecondaries();});
  for(auto &thread:threads)thread.join();
  ArmCpu::releaseSecondaries();
  for(unsigned id=1;id<8;id++)assert(calls[id]==1&&!gPending[id]);
 }
#endif
 puts("PASS: actual queue/release methods");
}
'''
(p/'test-deferred.cpp').write_text(prefix+decl+hook+drain+tests)
report={'base_sha256':hashlib.sha256(base).hexdigest(),'deferred_sha256':hashlib.sha256(new).hexdigest(),'adapter_sha256':hashlib.sha256(script.read_bytes()).hexdigest(),
        'test_scope':'exact production drain/release methods; mocked recovery/IOCPU, host concurrency only','recovery_hook_implemented':False,'boot_image_created':False,'hardware_tested':False}
(p/'source.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
