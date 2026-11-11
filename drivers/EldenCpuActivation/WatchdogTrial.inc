// SPDX-License-Identifier: BSD-2-Clause-Patent
// Included after the frozen SDAM implementation in the UFS translation unit.
// vdog=1 is an expiration qualification only; it does not release SMP CPUs.
static volatile unsigned gEldenVdogStarted;
static EldenVdog gEldenVdog;
static bool eldenVdogTrialReady(void)
{
  if(!elden_recovery_rpc_ready()||elden_recovery_deadline_remaining()<60||
     !eldenDeadlineDataReady()||gEldenUsb.failed||!gEldenUsb.active||
     current_processor()!=gEldenUsb.processor)return false;
  if(!__sync_bool_compare_and_swap(&gEldenUsb.busy,0,1))return false;
  struct dwc3_snapshot snap;dwc3_snapshot(eldenDwc(),&snap);
  bool ready=snap.configured&&snap.dtr&&dwc3_ready(eldenDwc());
  __sync_lock_release(&gEldenUsb.busy);
  return ready;
}
static void eldenVdogTrialReport(const char *stage,int rc)
{
  char text[256];
  snprintf(text,sizeof(text),"\nELDEN_VDOG %s session=%s rc=%d state=%u ctl=%x elapsed-ms=%u expired=%u calls=%u op=%x raw=%d\n",
    stage,gEldenSdam.session,rc,(unsigned)gEldenVdog.state,gEldenVdog.control,
    gEldenVdog.elapsed_ms,gEldenVdog.expired,gEldenVdog.calls,gEldenVdog.last_op,gEldenVdog.last_error);
  eldenSdamLog(text);
}
static void eldenVdogTrialWorker(void *,wait_result_t)
{
  thread_bind(gEldenDeadline.processor);
  if(current_processor()!=gEldenDeadline.processor)thread_block(THREAD_CONTINUE_NULL);
  const uint64_t expires=eldenDeadlineCounter()+19200000ULL*120;
  while(!eldenVdogTrialReady()&&eldenDeadlineCounter()<expires)IOSleep(250);
  if(!eldenVdogTrialReady()){
    eldenVdogTrialReport("PREREQUISITE_REFUSED",ELDEN_VDOG_NOT_READY);
    thread_terminate(current_thread());return;
  }
  eldenVdogTrialReport("BEGIN_30S_RAM_ONLY",0);
  int rc=elden_vdog_arm(&gEldenVdog,elden_vdog_smc,nullptr,1);
  eldenVdogTrialReport("ARM_RESULT",rc);
  for(unsigned i=0;i<gEldenVdog.trace_count;i++){
    const EldenVdogTrace *t=&gEldenVdog.trace[i];char line[240];
    snprintf(line,sizeof(line),"\nELDEN_VDOG REPLY n=%u op=%x args=%u,%u raw=%llx,%llx,%llx,%llx\n",i,t->op,t->arg1,t->arg2,
      (unsigned long long)t->a0,(unsigned long long)t->a1,(unsigned long long)t->a2,(unsigned long long)t->a3);
    eldenSdamLog(line);
  }
  if(rc){thread_terminate(current_thread());return;}
  const uint64_t armed=eldenDeadlineCounter();
  eldenSdamLog("\nELDEN_VDOG EXPECT_EXPIRATION seconds=30 bark-seconds=27 initial-pet=1 periodic-pet=0; PC must verify fastboot\n");
  while(eldenDeadlineCounter()-armed<19200000ULL*36){
    IOSleep(2000);
    if(!eldenVdogTrialReady()){
      rc=elden_vdog_stop(&gEldenVdog,elden_vdog_smc,nullptr);
      eldenVdogTrialReport("ABORT_STOP",rc);thread_terminate(current_thread());return;
    }
    rc=elden_vdog_status(&gEldenVdog,elden_vdog_smc,nullptr);
    eldenVdogTrialReport("STATUS",rc);
    // A queued bite after the27s bark is expected; do not cancel it before30s.
    if(rc||!(gEldenVdog.control&1))break;
  }
  // A functioning independent watchdog must have reset before this point.
  rc=elden_vdog_stop(&gEldenVdog,elden_vdog_smc,nullptr);
  eldenVdogTrialReport("NO_RESET_STOP",rc);
  thread_terminate(current_thread());
}
static void eldenStartVdogTrial(void)
{
  unsigned mode=0,smp=0;PE_parse_boot_argn("vdog",&mode,sizeof(mode));
  if(mode!=1)return;
  PE_parse_boot_argn("sheng-smp",&smp,sizeof(smp));
  if(smp||gEldenDeadline.state!=1||!elden_recovery_armed())return;
  if(!__sync_bool_compare_and_swap(&gEldenVdogStarted,0,1))return;
  thread_t thread=nullptr;
  if(kernel_thread_start(eldenVdogTrialWorker,nullptr,&thread)!=KERN_SUCCESS){
    eldenVdogTrialReport("THREAD_REFUSED",-1);return;
  }
  thread_deallocate(thread);
}
