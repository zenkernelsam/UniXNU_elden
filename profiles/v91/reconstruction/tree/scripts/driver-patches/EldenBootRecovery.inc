// SPDX-License-Identifier: BSD-2-Clause-Patent
// Optional test-boot recovery: arm the known BCB route while the kernel is healthy.
// Panic handling only drains USB, waits on the physical counter and calls PSCI.
#if defined(ELDEN_BOOT_RECOVERY) && ELDEN_BOOT_RECOVERY
#include "EldenBcbRules.h"
static volatile unsigned gEldenRecoveryArmed, gEldenRecoveryStage;
static uint32_t gEldenRecoveryResult;
static uint8_t gEldenRecoveryMain[24576],gEldenRecoveryBackup[20480];
static uint8_t gEldenRecoveryBefore[4096],gEldenRecoveryAfter[4096],gEldenRecoveryReadback[4096];
static char gEldenRecoverySession[24];
extern "C" void elden_usb_panic_drain(void);
extern "C" void elden_usb_log_putc(char);
static void eldenRecoveryLog(const char *text){while(*text)elden_usb_log_putc(*text++);}
static bool eldenArmBootRecovery(void) {
  unsigned enabled=0,delay=0,usb=0;
  PE_parse_boot_argn("elden-return-fastboot",&enabled,sizeof(enabled));
  if(!enabled)return true;
  PE_parse_boot_argn("elden-panic-delay",&delay,sizeof(delay));
  PE_parse_boot_argn("elden-usb-debug",&usb,sizeof(usb));
  if(enabled!=1||delay!=180||usb!=1||!gHost.ready||gHost.poisoned||gRootLun!=0||
     gLuns[0].last!=elden_bcb::disk_last||gLuns[0].blockBytes!=4096)return false;
  PE_parse_boot_argn("elden-debug-session",gEldenRecoverySession,sizeof(gEldenRecoverySession));
  if(strlen(gEldenRecoverySession)!=16)return false;
  for(unsigned i=0;i<16;i++)if(!((gEldenRecoverySession[i]>='0'&&gEldenRecoverySession[i]<='9')||
      (gEldenRecoverySession[i]>='a'&&gEldenRecoverySession[i]<='f')))return false;
  uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
  if(hz!=19200000)return false;
  IOSleep(3000); // Let the prestarted USB worker complete host enumeration before a possible refusal.
  gEldenRecoveryStage=1;
  if(hostRead4k(0,0,6,gEldenRecoveryMain)!=kIOReturnSuccess||
     hostRead4k(0,elden_bcb::disk_last-4,5,gEldenRecoveryBackup)!=kIOReturnSuccess||
     !elden_bcb::layout(gEldenRecoveryMain,sizeof(gEldenRecoveryMain),gEldenRecoveryBackup,sizeof(gEldenRecoveryBackup)))return false;
  gEldenRecoveryStage=2;
  if(hostRead4k(0,elden_bcb::first,1,gEldenRecoveryBefore)!=kIOReturnSuccess||
     !elden_bcb::prepare(gEldenRecoveryBefore,gEldenRecoveryAfter))return false;
  gEldenRecoveryStage=30;
  // Serialize the bounded transaction before exposing root volumes; no panic-lock bypass.
  IOLockLock(gHost.lock);
  IOReturn result=kIOReturnNotReady;
  if(gHost.ready&&!gHost.poisoned&&read32(gHost.hc,0x58)==0){
    const uint64_t lba=elden_bcb::first;
    memcpy(gHost.virt+kDataOffset,gEldenRecoveryAfter,4096);
    uint8_t cdb[10]={0x2a,0x08,uint8_t(lba>>24),uint8_t(lba>>16),uint8_t(lba>>8),uint8_t(lba),0,0,1,0};
    uint32_t scsi=0;
    gEldenRecoveryStage=31;
    gEldenBcbWritePermit=true;
    result=hostScsiDir(0,cdb,10,1,4096,2000000,&scsi);
    if(result==kIOReturnSuccess){
      gEldenRecoveryStage=32;
      uint8_t sync[10]={0x35};
      result=hostScsiDir(0,sync,10,0,0,10000000,&scsi);
    }
    gEldenBcbWritePermit=false;
  }
  gEldenRecoveryResult=static_cast<uint32_t>(result);
  IOLockUnlock(gHost.lock);
  if(result!=kIOReturnSuccess)return false;
  gEldenRecoveryStage=4;
  if(hostRead4k(0,elden_bcb::first,1,gEldenRecoveryReadback)!=kIOReturnSuccess||
     !elden_bcb::result(gEldenRecoveryBefore,gEldenRecoveryReadback)||
     memcmp(gEldenRecoveryReadback,gEldenRecoveryAfter,4096))return false;
  __atomic_store_n(&gEldenRecoveryArmed,1,__ATOMIC_RELEASE);
  IOLog("ELDEN_RECOVERY armed: misc LUN0:%llu command=bootloader; other4064 bytes unchanged; panic delay180 session=%s\n",
        elden_bcb::first,gEldenRecoverySession);
  return true;
}
extern "C" void elden_panic_return_to_fastboot(void) {
  if(__atomic_load_n(&gEldenRecoveryArmed,__ATOMIC_ACQUIRE)!=1)return;
  uint64_t hz,start,now;__asm__ volatile("mrs %0,cntfrq_el0\n\tmrs %1,cntpct_el0":"=r"(hz),"=r"(start));
  if(hz!=19200000)return;
  char line[160];snprintf(line,sizeof(line),"\nELDEN_RECOVERY PANIC_WAIT session=%s seconds=180 route=armed-bcb\n",gEldenRecoverySession);
  eldenRecoveryLog(line);
  do {
    elden_usb_panic_drain();
    __asm__ volatile("mrs %0,cntpct_el0":"=r"(now));
    IODelay(1000);
  }while(now-start<hz*180ULL);
  eldenRecoveryLog("\nELDEN_RECOVERY PSCI_RESET route=armed-bcb\n");
  for(unsigned i=0;i<100;i++){elden_usb_panic_drain();IODelay(1000);}
  register uint64_t x0 __asm__("x0")=0x84000009;
  register uint64_t x1 __asm__("x1")=0,x2 __asm__("x2")=0,x3 __asm__("x3")=0;
  __asm__ volatile("dsb sy\n\tsmc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::"memory");
  // A returned PSCI call is a failed qualification, never an automatic second write.
  for(;;)__asm__ volatile("wfe");
}
#endif
