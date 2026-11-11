// SPDX-License-Identifier: BSD-2-Clause-Patent
// Opt-in SDAM reboot reason through the measured HLOS-owned SPMI v7 channel.
// No ownership changes, no disk writes, no PMIC operation in panic context.
#if defined(ELDEN_SDAM_RECOVERY) && ELDEN_SDAM_RECOVERY
#include "EldenSpmiRestart.h"
#include "EldenRebootOwnership.h"
#include <sys/reboot.h>
extern "C" int reboot_kernel(int,char *);
extern "C" int elden_pd_bootstrap_request_stop(void) __attribute__((weak_import));
extern "C" int elden_dc_noop_request_stop(void) __attribute__((weak_import));
extern "C" int elden_ulog_filter_request_stop(void) __attribute__((weak_import));
extern "C" int elden_backlight_request_stop(void) __attribute__((weak_import));
#if defined(ELDEN_TOUCH_RUNTIME) && ELDEN_TOUCH_RUNTIME
extern "C" int elden_touch_request_stop(void);
#endif
#if defined(ELDEN_THERMAL_RUNTIME) && ELDEN_THERMAL_RUNTIME
extern "C" int elden_thermal_request_stop(void);
#endif
extern "C" volatile uint32_t native_data_persist_state;
extern "C" uint64_t native_owner_data_seed_size;
static volatile unsigned gEldenRpcState;
static void eldenStartRecoveryDeadline(void);
static void eldenStartVdogTrial(void);
static void eldenStartSmpGuard(void);
static void eldenSmpPanicPet(void);
static struct {
  OSSharedPtr<IOMemoryMap> coreMap,ownerMap,observerMap,writerMap;
  uintptr_t core,owner,observer,writer;
  volatile unsigned armed,stage;
  uint32_t status;
  uint8_t before,desired,after;
  char session[24];
} gEldenSdam;
static void eldenSdamLog(const char *s){while(*s)elden_usb_log_putc(*s++);}
static void eldenSdamWrite(uintptr_t base,unsigned offset,uint32_t value){
  uintptr_t address=base+offset;
  __asm__ volatile("dsb sy\n\tstr %w0,[%1]\n\tdsb sy"::"r"(value),"r"(address):"memory");
}
static bool eldenSdamDone(uintptr_t base){
  for(unsigned i=0;i<10000;i++){
    uint32_t status=read32(base,8);gEldenSdam.status=status;
    if(status&1)return (status&0xe)==0;
    IODelay(1);
  }
  return false;
}
static bool eldenSdamRead(uint8_t *value){
  eldenSdamWrite(gEldenSdam.observer,0,elden_spmi::read_command);
  if(!eldenSdamDone(gEldenSdam.observer))return false;
  *value=(uint8_t)read32(gEldenSdam.observer,0x18);return true;
}
static bool eldenArmSdamRecovery(void){
  unsigned enabled=0,delay=0,usb=0;
  PE_parse_boot_argn("elden-return-fastboot",&enabled,sizeof(enabled));if(!enabled)return true;
  if(__atomic_load_n(&gEldenSdam.armed,__ATOMIC_ACQUIRE)==1)return true;
  PE_parse_boot_argn("elden-panic-delay",&delay,sizeof(delay));PE_parse_boot_argn("elden-usb-debug",&usb,sizeof(usb));
  if(enabled!=1||delay!=180||usb!=1)return false;
  PE_parse_boot_argn("elden-debug-session",gEldenSdam.session,sizeof(gEldenSdam.session));
  if(strlen(gEldenSdam.session)!=16)return false;
  for(unsigned i=0;i<16;i++)if(!((gEldenSdam.session[i]>='0'&&gEldenSdam.session[i]<='9')||(gEldenSdam.session[i]>='a'&&gEldenSdam.session[i]<='f')))return false;
  uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));if(hz!=19200000)return false;
  IOSleep(3000);
  gEldenSdam.stage=1;
  gEldenSdam.core=mapPhys(ELDEN_REBOOT_CORE_PHYS,ELDEN_REBOOT_CORE_BYTES,gEldenSdam.coreMap);
  gEldenSdam.owner=mapPhys(ELDEN_REBOOT_OWNER_PHYS,ELDEN_REBOOT_OWNER_BYTES,gEldenSdam.ownerMap);
  if(!gEldenSdam.core||!gEldenSdam.owner)return false;
  unsigned apid=0;
  if(!elden_spmi::select([](unsigned off){return read32(gEldenSdam.core,off);},
      [](unsigned off){return read32(gEldenSdam.owner,off);},&apid))return false;
  gEldenSdam.stage=2;
  uint64_t obs=ELDEN_REBOOT_OBSERVER_PHYS+0x8000ULL*ELDEN_REBOOT_EE+0x20ULL*apid;
  uintptr_t obsPage=mapPhys(obs&~4095ULL,4096,gEldenSdam.observerMap);
  gEldenSdam.writer=mapPhys(ELDEN_REBOOT_RW_PHYS+0x1000ULL*apid,4096,gEldenSdam.writerMap);
  if(!obsPage||!gEldenSdam.writer)return false;
  gEldenSdam.observer=obsPage+(obs&4095);
  gEldenSdam.stage=3;if(!eldenSdamRead(&gEldenSdam.before))return false;
  gEldenSdam.desired=elden_spmi::bootloader(gEldenSdam.before);
  gEldenSdam.stage=4;
  eldenSdamWrite(gEldenSdam.writer,0x10,gEldenSdam.desired);
  eldenSdamWrite(gEldenSdam.writer,0,elden_spmi::write_command);
  if(!eldenSdamDone(gEldenSdam.writer))return false;
  gEldenSdam.stage=5;if(!eldenSdamRead(&gEldenSdam.after)||gEldenSdam.after!=gEldenSdam.desired)return false;
  __atomic_store_n(&gEldenSdam.armed,1,__ATOMIC_RELEASE);
  char line[200];snprintf(line,sizeof(line),"\nELDEN_RECOVERY armed SDAM sid0:7148 apid=%u before=%02x after=%02x mask=fe session=%s; no disk writes\n",apid,gEldenSdam.before,gEldenSdam.after,gEldenSdam.session);eldenSdamLog(line);
  eldenStartRecoveryDeadline();
  eldenStartVdogTrial();
  return true;
}
static __attribute__((noreturn)) void eldenSdamResetNow(void){
  register uint64_t x0 __asm__("x0")=0x84000009,x1 __asm__("x1")=0,x2 __asm__("x2")=0,x3 __asm__("x3")=0;
  __asm__ volatile("dsb sy\n\tsmc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::"memory");
  for(;;)__asm__ volatile("wfe");
}
extern "C" int elden_recovery_armed(void){
  return __atomic_load_n(&gEldenSdam.armed,__ATOMIC_ACQUIRE)==1;
}
extern "C" int elden_recovery_rpc_ready(void){
  bool dataQualified=native_owner_data_seed_size==536870912ULL&&native_data_persist_state==0;
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  dataQualified=dataQualified||elden_data_rpc_ready();
#endif
  return __atomic_load_n(&gEldenSdam.armed,__ATOMIC_ACQUIRE)==1 &&
    __atomic_load_n(&gEldenRpcState,__ATOMIC_ACQUIRE)==0 && native_owner_cache_status==0 &&
    native_owner_data_status==0 && dataQualified;
}
static void eldenRecoveryShutdown(void){
  __atomic_store_n(&gEldenRpcHoldLogs,0,__ATOMIC_RELEASE);
  if(elden_ulog_filter_request_stop){
    int stopped=elden_ulog_filter_request_stop();
    eldenSdamLog(stopped?"\nELDEN_ULOG_FILTER stop=1; cold log defaults acknowledged\n":
        "\nELDEN_ULOG_FILTER stop=0; diagnostics restoration unconfirmed, continuing reset\n");
  }
  if(elden_dc_noop_request_stop){
    int stopped=elden_dc_noop_request_stop();
    eldenSdamLog(stopped?"\nELDEN_DC_NOOP stop=1; no write pending or same value verified\n":
        "\nELDEN_DC_NOOP stop=0; same-value write unconfirmed, continuing reset\n");
  }
  if(elden_backlight_request_stop){
    int stopped=elden_backlight_request_stop();
    eldenSdamLog(stopped?"\nELDEN_BACKLIGHT RPC stop=1; original brightness confirmed\n":
        "\nELDEN_BACKLIGHT RPC stop=0; restore/owner unconfirmed, continuing reset\n");
  }
  if(elden_pd_bootstrap_request_stop){
    int stopped=elden_pd_bootstrap_request_stop();
    eldenSdamLog(stopped?"\nELDEN_PD_BOOT stop=1; no pulse or paused input pending\n":
        "\nELDEN_PD_BOOT stop=0; restore unconfirmed, bounded wait expired\n");
  }
#if defined(ELDEN_TOUCH_RUNTIME) && ELDEN_TOUCH_RUNTIME
  int touchStopped=elden_touch_request_stop();
  eldenSdamLog(touchStopped?"\nELDEN_TOUCH RPC stop=1; SPI/route/memory released\n":
      "\nELDEN_TOUCH RPC stop=0; held or bounded wait expired, continuing reset\n");
#endif
#if defined(ELDEN_THERMAL_RUNTIME) && ELDEN_THERMAL_RUNTIME
  int thermalStopped=elden_thermal_request_stop();
  eldenSdamLog(thermalStopped?"\nELDEN_THERMAL RPC stop=1; GPIO levels retained\n":
      "\nELDEN_THERMAL RPC stop=0; bounded wait expired, continuing reset\n");
#endif
  IOSleep(500);
  // The only writable filesystem is a volatile, size-checked RAM Data volume.
  // RB_QUICK still syncs it and executes the normal kernel reboot sequence.
  int rebootFlags=RB_QUICK;
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  if(storeMode()==2)rebootFlags=0; // process shutdown and both VFS unmount stages
#endif
  int error=reboot_kernel(rebootFlags,nullptr);
  char line[100];snprintf(line,sizeof(line),"\nELDEN_RPC reboot_kernel returned %d; no second request\n",error);eldenSdamLog(line);
  thread_terminate(current_thread());
}
static void eldenRpcWorker(void *,wait_result_t){
  thread_bind(gEldenUsb.processor);
  if(current_processor()!=gEldenUsb.processor)thread_block(THREAD_CONTINUE_NULL);
  for(unsigned i=0;i<500&&!__atomic_load_n(&gEldenRpcAckComplete,__ATOMIC_ACQUIRE)&&
      __atomic_load_n(&gEldenRpcState,__ATOMIC_ACQUIRE)==1;i++)IOSleep(10);
  if(current_processor()!=gEldenUsb.processor || !__atomic_load_n(&gEldenRpcAckComplete,__ATOMIC_ACQUIRE)){
    __atomic_store_n(&gEldenRpcHoldLogs,0,__ATOMIC_RELEASE);
    if(elden_reboot_ack_cancel(&gEldenRpcState))eldenSdamLog("\nELDEN_RPC cancelled: ACK transfer not complete\n");
    thread_terminate(current_thread());return;
  }
  if(!elden_reboot_ack_commit(&gEldenRpcState)){thread_terminate(current_thread());return;}
  eldenSdamLog("\nELDEN_RPC ACK_COMMITTED; preparing filesystem shutdown\n");
  eldenRecoveryShutdown();
}
extern "C" int elden_recovery_rpc_schedule(void){
  if(!elden_recovery_rpc_ready()||!elden_reboot_request(&gEldenRpcState))return -1;
  thread_t thread=nullptr;
  if(kernel_thread_start(eldenRpcWorker,nullptr,&thread)!=KERN_SUCCESS){elden_reboot_ack_cancel(&gEldenRpcState);return -2;}
  thread_deallocate(thread);return 0;
}
static struct {
  processor_t processor;
  uint64_t expires;
  volatile unsigned state; // 0 absent,1 armed,2 fired,3 ineligible,4 other owner
} gEldenDeadline;
static uint64_t eldenDeadlineCounter(void){uint64_t n;__asm__ volatile("mrs %0,cntpct_el0":"=r"(n));return n;}
static bool eldenDeadlineDataReady(void){
  // Deliberately unavailable to the unqualified persistent-Data branch.
  return native_data_persist_state==0&&native_owner_data_seed_size==536870912ULL&&
    native_owner_cache_status==0&&native_owner_data_status==0&&elden_recovery_armed();
}
extern "C" unsigned elden_recovery_deadline_remaining(void){
  if(__atomic_load_n(&gEldenDeadline.state,__ATOMIC_ACQUIRE)!=1||!eldenDeadlineDataReady()||
     __atomic_load_n(&gEldenRpcState,__ATOMIC_ACQUIRE)!=0)return 0;
  uint64_t now=eldenDeadlineCounter();return now<gEldenDeadline.expires?(unsigned)((gEldenDeadline.expires-now)/19200000ULL):0;
}
static void eldenDeadlineWorker(void *,wait_result_t){
  thread_bind(gEldenDeadline.processor);
  if(current_processor()!=gEldenDeadline.processor)thread_block(THREAD_CONTINUE_NULL);
  while(eldenDeadlineCounter()<gEldenDeadline.expires)IOSleep(100);
  if(current_processor()!=gEldenDeadline.processor||!eldenDeadlineDataReady()){
    __atomic_store_n(&gEldenDeadline.state,3u,__ATOMIC_RELEASE);
    eldenSdamLog("\nELDEN_RECOVERY DEADLINE_HOLD: RAM Data or processor qualification lost\n");
    thread_terminate(current_thread());return;
  }
  if(!elden_reboot_deadline_commit(&gEldenRpcState)){
    __atomic_store_n(&gEldenDeadline.state,4u,__ATOMIC_RELEASE);
    thread_terminate(current_thread());return;
  }
  __atomic_store_n(&gEldenDeadline.state,2u,__ATOMIC_RELEASE);
  eldenSdamLog("\nELDEN_RECOVERY DEADLINE_FIRED; no USB ACK required; preparing filesystem shutdown\n");
  eldenRecoveryShutdown();
}
static void eldenStartRecoveryDeadline(void){
  unsigned seconds=0;PE_parse_boot_argn("elden-autoreturn",&seconds,sizeof(seconds));
  if(!seconds)return;
  if(seconds<90||seconds>900){eldenSdamLog("\nELDEN_RECOVERY DEADLINE_REFUSED: seconds outside90..900\n");return;}
  gEldenDeadline.processor=current_processor();gEldenDeadline.expires=eldenDeadlineCounter()+19200000ULL*seconds;
  __atomic_store_n(&gEldenDeadline.state,1u,__ATOMIC_RELEASE);thread_t t=nullptr;
  if(kernel_thread_start(eldenDeadlineWorker,nullptr,&t)!=KERN_SUCCESS){
    __atomic_store_n(&gEldenDeadline.state,0u,__ATOMIC_RELEASE);eldenSdamLog("\nELDEN_RECOVERY DEADLINE_REFUSED: worker allocation\n");return;
  }
  thread_deallocate(t);char line[140];snprintf(line,sizeof(line),"\nELDEN_RECOVERY DEADLINE_ARMED session=%s seconds=%u RAM-only; independent of USB\n",gEldenSdam.session,seconds);eldenSdamLog(line);
}
extern "C" void elden_rpc_halt(void){
  if(__atomic_load_n(&gEldenRpcState,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&gEldenSdam.armed,__ATOMIC_ACQUIRE)!=1)return;
  eldenSdamLog("\nELDEN_RPC RESET_AFTER_SYNC route=sdam-v7\n");
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  if(!elden_data_recovery_reset_ready()){
    eldenSdamLog("\nELDEN_DATA RESET_HOLD: storage fence not durable\n");
    for(;;){elden_usb_panic_drain();IODelay(1000);} // do not fall through to PE reset
  }
#endif
  for(unsigned i=0;i<100;i++){elden_usb_panic_drain();IODelay(1000);}
  eldenSdamResetNow();
}
extern "C" void elden_panic_return_to_fastboot(void){
  if(__atomic_load_n(&gEldenSdam.armed,__ATOMIC_ACQUIRE)!=1)return;
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  if(!elden_data_panic_reset_ready()){
    eldenSdamLog("\nELDEN_DATA PANIC_HOLD: outstanding or uncertain write; auto-reset not armed\n");return;
  }
#endif
  uint64_t hz,start,now;__asm__ volatile("mrs %0,cntfrq_el0\n\tmrs %1,cntpct_el0":"=r"(hz),"=r"(start));if(hz!=19200000)return;
  char line[160];snprintf(line,sizeof(line),"\nELDEN_RECOVERY PANIC_WAIT session=%s seconds=180 route=sdam-v7\n",gEldenSdam.session);eldenSdamLog(line);
  do{eldenSmpPanicPet();elden_usb_panic_drain();__asm__ volatile("mrs %0,cntpct_el0":"=r"(now));IODelay(1000);}while(now-start<hz*180ULL);
  eldenSdamLog("\nELDEN_RECOVERY PSCI_RESET route=sdam-v7\n");
  for(unsigned i=0;i<100;i++){elden_usb_panic_drain();IODelay(1000);}
  eldenSdamResetNow();
}
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
extern "C" int elden_data_reboot_fence(void){
  if(__atomic_load_n(&gEldenRpcState,__ATOMIC_ACQUIRE)!=2||storeMode()!=2)return 1;
  int ready=elden_data_recovery_close();
  eldenSdamLog(ready?"\nELDEN_DATA REBOOT_FENCE durable=1 closed=1\n":"\nELDEN_DATA REBOOT_FENCE refused\n");
  return ready;
}
#endif

/* ELDEN_VDOG_TRIAL_BEGIN */
/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ABI from the pinned Lenovo gh_virt_wdt and upstream gunyah_wdt drivers. */
enum { ELDEN_VDOG_CONTROL=0x86000005U, ELDEN_VDOG_STATUS=0x86000006U,
       ELDEN_VDOG_PET=0x86000007U, ELDEN_VDOG_TIME=0x86000008U,
       ELDEN_VDOG_BARK_MS=27000U, ELDEN_VDOG_TIMEOUT_MS=30000U };
typedef struct { uint64_t a0,a1,a2,a3; } EldenVdogReply;
typedef EldenVdogReply (*EldenVdogCall)(void *,uint32_t,uint32_t,uint32_t);
EldenVdogReply elden_vdog_smc(void *,uint32_t,uint32_t,uint32_t);
typedef enum { ELDEN_VDOG_IDLE, ELDEN_VDOG_ARMED, ELDEN_VDOG_STOPPED,
               ELDEN_VDOG_REFUSED, ELDEN_VDOG_UNCERTAIN } EldenVdogState;
typedef struct {
    uint32_t op,arg1,arg2;
    volatile uint64_t a0,a1,a2,a3;
} EldenVdogTrace;
typedef struct {
    EldenVdogState state;
    uint32_t attempted, enable_attempted, calls, last_op;
    int32_t last_error;
    uint32_t control, elapsed_ms, expired;
    uint32_t trace_count,trace_overflow;
    EldenVdogTrace trace[8];
} EldenVdog;
enum { ELDEN_VDOG_OK=0, ELDEN_VDOG_BAD_STATE=-100,
       ELDEN_VDOG_BUSY=-101, ELDEN_VDOG_BAD_STATUS=-102,
       ELDEN_VDOG_NOT_READY=-103 };
/* Caller serializes access. Software timestamps and hardware/route qualification
 * belong to the board controller; this protocol core cannot certify them. */
int elden_vdog_status(EldenVdog *,EldenVdogCall,void *);
int elden_vdog_arm(EldenVdog *,EldenVdogCall,void *,int recovery_ready);
int elden_vdog_pet(EldenVdog *,EldenVdogCall,void *);
int elden_vdog_stop(EldenVdog *,EldenVdogCall,void *);
#ifdef __cplusplus
}
#endif

/* SPDX-License-Identifier: BSD-2-Clause-Patent */

static int invoke(EldenVdog *s,EldenVdogCall call,void *arg,
                  uint32_t op,uint32_t x1,uint32_t x2,EldenVdogReply *reply)
{
    *reply=call(arg,op,x1,x2);
    if(s->trace_count<8){
        EldenVdogTrace *t=&s->trace[s->trace_count++];
        t->op=op;t->arg1=x1;t->arg2=x2;
        t->a0=reply->a0;t->a1=reply->a1;t->a2=reply->a2;t->a3=reply->a3;
    }else s->trace_overflow++;
    s->last_op=op;s->calls++;
    s->last_error=(int32_t)(uint32_t)reply->a0;
    return s->last_error;
}

int elden_vdog_status(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call)return ELDEN_VDOG_BAD_STATE;
    EldenVdogReply r;
    int rc=invoke(s,call,arg,ELDEN_VDOG_STATUS,0,0,&r);
    if(rc)return rc;
    /* Bit31 means the bite action is queued, not necessarily that its deadline
     * has passed. Equal bark/bite queues bite immediately in Gunyah; use the
     * OEM's positive bark-to-bite interval. Never copy status into CONTROL. */
    if(r.a1>UINT32_MAX||r.a2>UINT32_MAX){s->last_error=ELDEN_VDOG_BAD_STATUS;return ELDEN_VDOG_BAD_STATUS;}
    s->control=(uint32_t)r.a1;s->expired=(s->control>>31)&1;
    s->elapsed_ms=(uint32_t)r.a2;
    return ELDEN_VDOG_OK;
}

int elden_vdog_stop(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call||!s->enable_attempted)return ELDEN_VDOG_BAD_STATE;
    if(s->state==ELDEN_VDOG_STOPPED)return ELDEN_VDOG_OK;
    EldenVdogReply r;
    int rc=elden_vdog_status(s,call,arg);
    if(!rc&&!(s->control&1)){s->state=ELDEN_VDOG_STOPPED;return ELDEN_VDOG_OK;}
    /* We may disable only after this context attempted enable. There is no
     * stop/reset of a pre-existing active watchdog and no retry loop. */
    rc=invoke(s,call,arg,ELDEN_VDOG_CONTROL,2,0,&r);
    if(rc){s->state=ELDEN_VDOG_UNCERTAIN;return rc;}
    rc=elden_vdog_status(s,call,arg);
    if(rc||(s->control&1)){s->state=ELDEN_VDOG_UNCERTAIN;return rc?rc:ELDEN_VDOG_BAD_STATUS;}
    s->state=ELDEN_VDOG_STOPPED;
    return ELDEN_VDOG_OK;
}

int elden_vdog_arm(EldenVdog *s,EldenVdogCall call,void *arg,int recovery_ready)
{
    if(!s||!call||s->attempted||s->state!=ELDEN_VDOG_IDLE)return ELDEN_VDOG_BAD_STATE;
    if(recovery_ready!=1)return ELDEN_VDOG_NOT_READY;
    s->attempted=1;
    int rc=elden_vdog_status(s,call,arg);
    if(rc){s->state=ELDEN_VDOG_REFUSED;return rc;}
    if((s->control&1)||s->expired){s->state=ELDEN_VDOG_REFUSED;return ELDEN_VDOG_BUSY;}
    EldenVdogReply r;
    rc=invoke(s,call,arg,ELDEN_VDOG_TIME,ELDEN_VDOG_BARK_MS,ELDEN_VDOG_TIMEOUT_MS,&r);
    if(rc){s->state=ELDEN_VDOG_REFUSED;return rc;}
    s->enable_attempted=1;
    rc=invoke(s,call,arg,ELDEN_VDOG_CONTROL,3,0,&r);
    /* Lenovo qcom_wdt_init enables and immediately resets the counter. A
     * one-shot initial PET establishes the epoch; the trial never pets again. */
    if(!rc)rc=invoke(s,call,arg,ELDEN_VDOG_PET,0,0,&r);
    if(!rc)rc=elden_vdog_status(s,call,arg);
    if(!rc&&(!(s->control&1)||s->expired||s->elapsed_ms>=ELDEN_VDOG_TIMEOUT_MS))rc=ELDEN_VDOG_BAD_STATUS;
    if(rc){
        /* Preserve the original failure as return value; stop result/state
         * records whether returning to disabled was actually confirmed. */
        s->state=ELDEN_VDOG_UNCERTAIN;
        (void)elden_vdog_stop(s,call,arg);
        return rc;
    }
    s->state=ELDEN_VDOG_ARMED;
    return ELDEN_VDOG_OK;
}

int elden_vdog_pet(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call||s->state!=ELDEN_VDOG_ARMED)return ELDEN_VDOG_BAD_STATE;
    EldenVdogReply r;
    int rc=invoke(s,call,arg,ELDEN_VDOG_PET,0,0,&r);
    if(!rc)rc=elden_vdog_status(s,call,arg);
    if(!rc&&(!(s->control&1)||s->expired||s->elapsed_ms>=ELDEN_VDOG_TIMEOUT_MS))rc=ELDEN_VDOG_BAD_STATUS;
    if(rc)s->state=ELDEN_VDOG_UNCERTAIN;
    return rc;
}

/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SMCCC1.1 SMC32 vendor-hypervisor ABI. No MMIO or SPMI access. */
EldenVdogReply elden_vdog_smc(void *unused,uint32_t fn,uint32_t arg1,uint32_t arg2)
{
    (void)unused;
    if(!((fn==ELDEN_VDOG_STATUS||fn==ELDEN_VDOG_PET)?(arg1==0&&arg2==0):
         fn==ELDEN_VDOG_TIME?(arg1==ELDEN_VDOG_BARK_MS&&arg2==ELDEN_VDOG_TIMEOUT_MS):
         fn==ELDEN_VDOG_CONTROL?((arg1==2||arg1==3)&&arg2==0):0))
        return (EldenVdogReply){(uint64_t)(int64_t)ELDEN_VDOG_BAD_STATE,0,0,0};
    register uint64_t x0 __asm__("x0")=fn,x1 __asm__("x1")=arg1;
    register uint64_t x2 __asm__("x2")=arg2,x3 __asm__("x3")=0;
    __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::
                     "x4","x5","x6","x7","x8","x9","x10","x11",
                     "x12","x13","x14","x15","x16","x17","cc","memory");
    return (EldenVdogReply){x0,x1,x2,x3};
}

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
  if(mode==2){eldenStartSmpGuard();return;}
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

/* ELDEN_VDOG_TRIAL_END */

/* ELDEN_SMP_GUARD_BEGIN */
/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <stdint.h>
typedef enum { EldenSmpWait, EldenSmpLaunchWork, EldenSmpQualified, EldenSmpFailed } EldenSmpAction;
typedef struct { uint32_t mask,phase,reason; uint64_t since,last_now,last_ready; } EldenSmpPolicy;
static inline int elden_smp_policy_init(EldenSmpPolicy *p,unsigned cpus)
{
    if(!p||cpus<2||cpus>8)return 0;
    p->mask=(1U<<cpus)-1U;p->phase=0;p->reason=0;p->since=0;p->last_now=0;p->last_ready=0;
    return 1;
}
static inline EldenSmpAction elden_smp_policy_step(EldenSmpPolicy *p,uint64_t now,
    int ready,uint32_t online,uint32_t work,uint32_t timers,uint32_t ipis,int error)
{
    if(p->phase==3)return EldenSmpQualified;
    if(p->phase==4)return EldenSmpFailed;
    if(now<p->last_now){p->reason=1;goto failed;}
    p->last_now=now;
    if(error){p->reason=2;goto failed;}
    if(ready)p->last_ready=now;
    else if(now-p->last_ready>=10000){p->reason=3;goto failed;}
    if(p->phase==0){
        if((online&p->mask)==p->mask&&ready){p->phase=1;p->since=now;return EldenSmpLaunchWork;}
        if(now>=15000){p->reason=4;goto failed;}
    }else if(p->phase==1){
        if(((work&timers&ipis)&p->mask)==p->mask&&ready){p->phase=2;p->since=now;}
        else if(now-p->since>=15000){p->reason=5;goto failed;}
    }else if(p->phase==2){
        if((online&p->mask)!=p->mask){p->reason=6;goto failed;}
        if(ready&&now-p->since>=5000){p->phase=3;return EldenSmpQualified;}
    }
    return EldenSmpWait;
failed:
    p->phase=4;return EldenSmpFailed;
}

// SPDX-License-Identifier: BSD-2-Clause-Patent
// vdog=2: bounded SMP admission under the source-bound v80 watchdog route.
#include <machine/machine_routines.h>
/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
typedef struct { uint64_t start,last,last_ready; uint32_t duration_ms,mask; } EldenSmpHold;
static inline int elden_smp_hold_init(EldenSmpHold *p,unsigned seconds,uint64_t now,uint32_t mask)
{
    if(!p||seconds<60||seconds>600||mask!=255)return 0;
    p->start=now;p->last=now;p->last_ready=now;p->duration_ms=seconds*1000;p->mask=mask;return 1;
}
/* 0 permits one pet; 5 pauses without pet; other outcomes end this hold. */
static inline unsigned elden_smp_hold_step(EldenSmpHold *p,uint64_t now,int ready,uint32_t online,int error)
{
    if(now<p->last)return 1;
    p->last=now;
    if(now-p->start>=p->duration_ms)return 2;
    if(online!=p->mask||error)return 4;
    if(!ready)return now-p->last_ready>=10000?3:5;
    p->last_ready=now;
    return 0;
}

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
static volatile unsigned gEldenFramebufferReady;
extern "C" bool elden_smp_framebuffer_ready(void)
{
  return __atomic_load_n(&gEldenFramebufferReady,__ATOMIC_ACQUIRE) &&
    __atomic_load_n(&gEldenSmpOwned,__ATOMIC_ACQUIRE) &&
    !__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE);
}
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
  __atomic_store_n(&gEldenFramebufferReady,1u,__ATOMIC_RELEASE);
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

/* ELDEN_SMP_GUARD_END */
#endif
