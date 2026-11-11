// SPDX-License-Identifier: BSD-2-Clause-Patent
// vdog=2: bounded SMP admission under the source-bound v80 watchdog route.
#include <machine/machine_routines.h>
#include "HoldPolicy.h"
extern "C" int elden_armcpu_release_secondaries(void);
extern "C" processor_t elden_armcpu_guard_processor(unsigned);
extern "C" int elden_armcpu_guard_ipi(unsigned);
extern "C" void armcpu_gic_diag(uint64_t,uint32_t *);
extern "C" volatile uint32_t armcpu_gic_boot[8],armcpu_gic_diag_stage;
extern "C" volatile uint32_t armcpu_smp_step[8],armcpu_smp_mpidr[8],armcpu_smp_fail[8];
extern "C" volatile uint32_t armcpu_core_timer[8],armcpu_core_sgi[8];
extern "C" volatile int32_t armcpu_smp_psci_rc[8];
static volatile uint64_t gEldenSmpLease;
static volatile unsigned gEldenSmpOwned,gEldenSmpPanic,gEldenSmpWorkDone,gEldenSmpWorkFailed;
static unsigned gEldenSmpCount;
static unsigned gEldenSmpHoldSeconds;
static struct { uint32_t mpidr; int ipi; uint64_t iterations,checksum; } gEldenSmpWork[8];

static void eldenSmpReadGic(unsigned id)
{
  uint32_t words[8]={};const uint64_t affinity=id?armcpu_smp_mpidr[id]:0;
  armcpu_gic_diag(affinity,words);char line[300];
  snprintf(line,sizeof(line),"\nELDEN_GIC_READ cpu=%u aff=%llx found=%u stage=%x group=%x enable=%x pending=%x modifier=%x waker=%x ctl=%x priority0=%x boot-pmr=%x boot-igrpen=%x boot-sre=%x\n",
    id,(unsigned long long)affinity,words[7],armcpu_gic_diag_stage,words[0],words[1],words[2],words[3],words[4],words[5],words[6],armcpu_gic_boot[5],armcpu_gic_boot[6],armcpu_gic_boot[7]);
  eldenSdamLog(line);
}

static EldenVdogReply eldenSmpSmc(void *arg,uint32_t fn,uint32_t x1,uint32_t x2)
{
  // The qualified core reaches CONTROL3 only after verifying no active owner.
  // Publish the attempted ownership before the call so panic can keep it alive.
  if(fn==ELDEN_VDOG_CONTROL&&x1==3)__atomic_store_n(&gEldenSmpOwned,1u,__ATOMIC_RELEASE);
  EldenVdogReply reply=elden_vdog_smc(arg,fn,x1,x2);
  if(fn==ELDEN_VDOG_CONTROL&&x1==2&&(uint32_t)reply.a0==0)
    __atomic_store_n(&gEldenSmpOwned,0u,__ATOMIC_RELEASE);
  return reply;
}
extern "C" bool elden_smp_recovery_ready(void)
{
  const uint64_t until=__atomic_load_n(&gEldenSmpLease,__ATOMIC_ACQUIRE);
  return until&&eldenDeadlineCounter()<until&&
    __atomic_load_n(&gEldenSmpOwned,__ATOMIC_ACQUIRE)&&
    !__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE)&&
    current_processor()==gEldenDeadline.processor&&elden_recovery_rpc_ready()&&eldenDeadlineDataReady();
}
static void eldenSmpPanicPet(void)
{
  if(!__atomic_load_n(&gEldenSmpOwned,__ATOMIC_ACQUIRE))return;
  __atomic_store_n(&gEldenSmpPanic,1u,__ATOMIC_RELEASE);
  __atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);
  static volatile uint64_t last;
  uint64_t now=eldenDeadlineCounter(),previous=__atomic_load_n(&last,__ATOMIC_RELAXED);
  if(previous&&now-previous<19200000ULL*5)return;
  // Panic uses only idempotent PET/STATUS, no shared mutable protocol context.
  EldenVdogReply pet=elden_vdog_smc(nullptr,ELDEN_VDOG_PET,0,0);
  if((uint32_t)pet.a0){__atomic_store_n(&gEldenSmpOwned,0u,__ATOMIC_RELEASE);return;}
  EldenVdogReply status=elden_vdog_smc(nullptr,ELDEN_VDOG_STATUS,0,0);
  if((uint32_t)status.a0||!(status.a1&1)||(status.a1&(1ULL<<31))||status.a2>=5000){
    __atomic_store_n(&gEldenSmpOwned,0u,__ATOMIC_RELEASE);return;
  }
  __atomic_store_n(&last,now,__ATOMIC_RELAXED);
}
static void eldenSmpWorkThread(void *arg,wait_result_t)
{
  const unsigned id=(unsigned)(uintptr_t)arg;
  if(id==0||id>=gEldenSmpCount){thread_terminate(current_thread());return;}
  processor_t processor=elden_armcpu_guard_processor(id);
  if(!processor){__atomic_fetch_or(&gEldenSmpWorkFailed,1u<<id,__ATOMIC_RELEASE);thread_terminate(current_thread());return;}
  thread_bind(processor);
  if(current_processor()!=processor)thread_block(THREAD_CONTINUE_NULL);
  uint64_t mpidr;__asm__ volatile("mrs %0,mpidr_el1":"=r"(mpidr));
  gEldenSmpWork[id].mpidr=(uint32_t)mpidr;
  if(current_processor()!=processor||(mpidr&0xff00ffffffULL)!=armcpu_smp_mpidr[id]){
    __atomic_fetch_or(&gEldenSmpWorkFailed,1u<<id,__ATOMIC_RELEASE);thread_terminate(current_thread());return;
  }
  gEldenSmpWork[id].ipi=elden_armcpu_guard_ipi(0);
  uint64_t start=eldenDeadlineCounter(),value=id,iterations=0;
  while(eldenDeadlineCounter()-start<19200000ULL/20){
    value=value*6364136223846793005ULL+1;iterations++;
  }
  gEldenSmpWork[id].iterations=iterations;gEldenSmpWork[id].checksum=value;
  if(current_processor()!=processor||gEldenSmpWork[id].ipi||!iterations)
    __atomic_fetch_or(&gEldenSmpWorkFailed,1u<<id,__ATOMIC_RELEASE);
  else __atomic_fetch_or(&gEldenSmpWorkDone,1u<<id,__ATOMIC_RELEASE);
  thread_terminate(current_thread());
}
static void eldenSmpGuardReport(unsigned action,const EldenSmpPolicy *policy,
                                uint32_t online,uint32_t timers,uint32_t ipis,int error)
{
  char line[240];snprintf(line,sizeof(line),"\nELDEN_SMP action=%u phase=%u reason=%u expected=%x online=%x work=%x timers=%x ipis=%x error=%d vdog=%u\n",
    action,policy->phase,policy->reason,policy->mask,online,
    __atomic_load_n(&gEldenSmpWorkDone,__ATOMIC_ACQUIRE),timers,ipis,error,(unsigned)gEldenVdog.state);
  eldenSdamLog(line);
  for(unsigned i=1;i<gEldenSmpCount;i++){
    const unsigned finished=__atomic_load_n(&gEldenSmpWorkDone,__ATOMIC_ACQUIRE)|
      __atomic_load_n(&gEldenSmpWorkFailed,__ATOMIC_ACQUIRE);
    const uint32_t workMpidr=(finished&(1u<<i))?gEldenSmpWork[i].mpidr:0;
    snprintf(line,sizeof(line),"\nELDEN_SMP CPU id=%u step=%u fail=%u psci=%d physical=%x timer=%u sgi=%u work-mpidr=%x\n",i,
      armcpu_smp_step[i],armcpu_smp_fail[i],armcpu_smp_psci_rc[i],armcpu_smp_mpidr[i],armcpu_core_timer[i],armcpu_core_sgi[i],workMpidr);
    eldenSdamLog(line);
  }
}
static __attribute__((noinline)) void eldenSmpHoldWindow(uint32_t mask)
{
  EldenSmpHold hold={};
  if(!elden_smp_hold_init(&hold,gEldenSmpHoldSeconds,eldenDeadlineCounter()/19200,mask))return;
  char line[180];unsigned rounds=0;
  snprintf(line,sizeof(line),"\nELDEN_SMP HOLD_BEGIN seconds=%u mask=%x; independent watchdog retained\n",gEldenSmpHoldSeconds,mask);eldenSdamLog(line);
  for(;;){
    IOSleep(1000);
    __atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);
    if(__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE))return;
    uint32_t online=1;int error=__atomic_load_n(&gEldenSmpWorkFailed,__ATOMIC_ACQUIRE)!=0;
    for(unsigned i=1;i<gEldenSmpCount;i++){
      if(armcpu_smp_step[i]==7&&!armcpu_smp_fail[i])online|=1u<<i;
      if(armcpu_smp_fail[i]||armcpu_smp_psci_rc[i])error=1;
    }
    const unsigned reason=elden_smp_hold_step(&hold,eldenDeadlineCounter()/19200,eldenVdogTrialReady(),online,error);
    if(reason==5)continue;
    if(reason){
      snprintf(line,sizeof(line),"\nELDEN_SMP HOLD_END reason=%u elapsed-ms=%llu online=%x; stop-pet\n",reason,(unsigned long long)(hold.last-hold.start),online);eldenSdamLog(line);return;
    }
    int rc=elden_vdog_pet(&gEldenVdog,eldenSmpSmc,nullptr);
    if(rc){eldenVdogTrialReport("HOLD_PET_FAILED",rc);return;}
    if(__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&gEldenSmpLease,eldenDeadlineCounter()+19200000ULL,__ATOMIC_RELEASE);
    if(++rounds%10==0){
      snprintf(line,sizeof(line),"\nELDEN_SMP HOLD elapsed-ms=%llu online=%x\n",(unsigned long long)(hold.last-hold.start),online);eldenSdamLog(line);
    }
  }
}
static void eldenSmpGuardWorker(void *,wait_result_t)
{
  thread_bind(gEldenDeadline.processor);
  if(current_processor()!=gEldenDeadline.processor)thread_block(THREAD_CONTINUE_NULL);
  uint64_t deadline=eldenDeadlineCounter()+19200000ULL*120;
  while(!eldenVdogTrialReady()&&eldenDeadlineCounter()<deadline)IOSleep(250);
  EldenSmpPolicy policy={};
  if(!eldenVdogTrialReady()||!elden_smp_policy_init(&policy,gEldenSmpCount)){
    eldenSdamLog("\nELDEN_SMP ARM_REFUSED prerequisites\n");thread_terminate(current_thread());return;
  }
  int rc=elden_vdog_arm(&gEldenVdog,eldenSmpSmc,nullptr,1);eldenVdogTrialReport("SMP_ARM",rc);
  if(rc){thread_terminate(current_thread());return;}
  const uint64_t start=eldenDeadlineCounter();
  uint32_t initialTimer[8]={},initialIpi[8]={};
  for(unsigned i=0;i<gEldenSmpCount;i++){initialTimer[i]=armcpu_core_timer[i];initialIpi[i]=armcpu_core_sgi[i];}
  __atomic_store_n(&gEldenSmpWorkDone,1u,__ATOMIC_RELEASE);
  __atomic_store_n(&gEldenSmpLease,start+19200000ULL,__ATOMIC_RELEASE);
  rc=elden_armcpu_release_secondaries();
  if(rc){__atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);eldenSdamLog("\nELDEN_SMP RELEASE_FAILED; stop-pet\n");thread_terminate(current_thread());return;}
  uint32_t timers=0,ipis=0;
  for(;;){
    IOSleep(1000);
    __atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);
    if(__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE)){thread_terminate(current_thread());return;}
    uint64_t now=eldenDeadlineCounter();bool ready=eldenVdogTrialReady();uint32_t online=1;
    int error=__atomic_load_n(&gEldenSmpWorkFailed,__ATOMIC_ACQUIRE)!=0;
    for(unsigned i=0;i<gEldenSmpCount;i++){
      if(i&&armcpu_smp_step[i]==7&&!armcpu_smp_fail[i])online|=1u<<i;
      if(i&&(armcpu_smp_fail[i]||armcpu_smp_psci_rc[i]))error=1;
      if(armcpu_core_timer[i]!=initialTimer[i])timers|=1u<<i;
      if(armcpu_core_sgi[i]!=initialIpi[i])ipis|=1u<<i;
    }
    const EldenSmpAction action=elden_smp_policy_step(&policy,(now-start)/19200,ready,online,
      __atomic_load_n(&gEldenSmpWorkDone,__ATOMIC_ACQUIRE),timers,ipis,error);
    eldenSmpGuardReport((unsigned)action,&policy,online,timers,ipis,error);
    if(action==EldenSmpQualified&&gEldenSmpHoldSeconds){
      eldenSmpHoldWindow(policy.mask);
      __atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);
      thread_terminate(current_thread());return;
    }
    if(action==EldenSmpFailed||action==EldenSmpQualified){
      eldenSdamLog(action==EldenSmpQualified?"\nELDEN_SMP QUALIFIED_RETURN stop-pet; await fastboot\n":"\nELDEN_SMP FAILED_RETURN stop-pet; await fastboot\n");
      thread_terminate(current_thread());return;
    }
    // No pet after a reboot request or lost readiness. The independent timer
    // remains armed through any stalled kernel shutdown.
    if(ready){
      rc=elden_vdog_pet(&gEldenVdog,eldenSmpSmc,nullptr);
      if(rc){eldenVdogTrialReport("SMP_PET_FAILED",rc);thread_terminate(current_thread());return;}
      if(__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE)){thread_terminate(current_thread());return;}
      __atomic_store_n(&gEldenSmpLease,eldenDeadlineCounter()+19200000ULL,__ATOMIC_RELEASE);
      if(policy.phase==0&&(rc=elden_armcpu_release_secondaries())!=0){
        __atomic_store_n(&gEldenSmpLease,0ULL,__ATOMIC_RELEASE);thread_terminate(current_thread());return;
      }
    }
    if(action==EldenSmpLaunchWork){
      eldenSmpReadGic(0);
      timers=0;ipis=0;
      for(unsigned i=0;i<gEldenSmpCount;i++){initialTimer[i]=armcpu_core_timer[i];initialIpi[i]=armcpu_core_sgi[i];}
      for(unsigned i=1;i<gEldenSmpCount;i++){
        if(elden_armcpu_guard_ipi(i)){
          __atomic_fetch_or(&gEldenSmpWorkFailed,1u<<i,__ATOMIC_RELEASE);continue;
        }
        eldenSmpReadGic(i);
        thread_t thread=nullptr;
        if(kernel_thread_start(eldenSmpWorkThread,(void *)(uintptr_t)i,&thread)!=KERN_SUCCESS)
          __atomic_fetch_or(&gEldenSmpWorkFailed,1u<<i,__ATOMIC_RELEASE);
        else thread_deallocate(thread);
      }
    }
  }
}
static void eldenStartSmpGuard(void)
{
  unsigned smp=0,self=0,cpus=0;
  PE_parse_boot_argn("sheng-smp",&smp,sizeof(smp));PE_parse_boot_argn("armcpu-self-ipi",&self,sizeof(self));
  PE_parse_boot_argn("cpus",&cpus,sizeof(cpus));
  unsigned hold=0;PE_parse_boot_argn("smp-hold",&hold,sizeof(hold));
  if(hold&&(hold<60||hold>600||cpus!=8)){eldenSdamLog("\nELDEN_SMP HOLD_REFUSED config\n");return;}
  if(smp!=1||self!=1||cpus<2||cpus>8||gEldenDeadline.state!=1||!elden_recovery_armed())return;
  if((unsigned)ml_get_max_cpu_number()+1!=cpus||ml_get_boot_cpu_number()!=0)return;
  gEldenSmpCount=cpus;
  gEldenSmpHoldSeconds=hold;
  if(!__sync_bool_compare_and_swap(&gEldenVdogStarted,0,1))return;
  thread_t thread=nullptr;
  if(kernel_thread_start(eldenSmpGuardWorker,nullptr,&thread)!=KERN_SUCCESS){eldenSdamLog("\nELDEN_SMP THREAD_REFUSED\n");return;}
  thread_deallocate(thread);
}
