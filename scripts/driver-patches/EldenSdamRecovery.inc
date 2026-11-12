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
extern "C" int elden_dc_native_request_stop(void) __attribute__((weak_import));
extern "C" int elden_ulog_filter_request_stop(void) __attribute__((weak_import));
extern "C" int elden_backlight_request_stop(void) __attribute__((weak_import));
extern "C" int elden_wcn_request_stop(void) __attribute__((weak_import));
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
  if(elden_wcn_request_stop){
    int stopped=elden_wcn_request_stop();
    eldenSdamLog(stopped?"\nELDEN_WCN RPC stop=1; radio/UART stopped, boot-shared votes retained\n":
        "\nELDEN_WCN RPC stop=0; held or bounded wait expired, continuing reset\n");
  }
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
  if(elden_dc_native_request_stop){
    int stopped=elden_dc_native_request_stop();
    eldenSdamLog(stopped?"\nELDEN_DC_NATIVE stop=1; restored or no write issued\n":
        "\nELDEN_DC_NATIVE stop=0; restoration unconfirmed, further writes fenced\n");
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
  do{elden_usb_panic_drain();__asm__ volatile("mrs %0,cntpct_el0":"=r"(now));IODelay(1000);}while(now-start<hz*180ULL);
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
#endif
