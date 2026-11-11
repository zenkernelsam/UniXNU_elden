// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off, register/firmware preflight only. No clock/power/UART writes.
#include "EldenBluetoothBoard.h"
extern "C" {
#include "BtGeni.h"
#include "Wcn7861Startup.h"
#if defined(ELDEN_BLUETOOTH_HCI_BUILD)
#include "Wcn7861Hci.h"
#endif
}
#include "EldenWcnFirmware.h" // generated from the exact board assets
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <stdarg.h>
extern "C" {
int elden_recovery_armed(void);
void elden_usb_log_putc(char);
extern volatile int native_owner_data_status,elden_init_exec_accepted;
}
namespace {
static unsigned attempted;
static void log(const char *format,...){
    char line[320];va_list a;va_start(a,format);vsnprintf(line,sizeof(line),format,a);va_end(a);
    for(const char *p=line;*p;p++)elden_usb_log_putc(*p);
}
struct Region{
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned bytes=0;
    ~Region(){if(keep)keep->release();}
    bool map(uint64_t pa,unsigned length){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,length);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();va=keep?(uintptr_t)keep->getVirtualAddress():0;bytes=length;return va!=0;
    }
    uint32_t read(unsigned at){
        if(!va||(at&3)||at>bytes-4)return 0xffffffffu;
        uint32_t value;uintptr_t p=va+at;__asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(p):"memory");return value;
    }
};
struct Probe{Region engine;unsigned writes=0;};
static uint32_t rd(void *ctx,uint32_t reg){return static_cast<Probe *>(ctx)->engine.read(reg);}
static void wr(void *ctx,uint32_t,uint32_t){static_cast<Probe *>(ctx)->writes++;} // never writes hardware
static uint64_t now(void *){uint64_t n;__asm__ volatile("mrs %0,cntpct_el0":"=r"(n));return n/19200;}
static void delay(void *,unsigned us){IODelay(us);}
static void sleep(void *,unsigned ms){IOSleep(ms);}
static void inspect(){
    EldenWcnPeripheralPlan plan;int fw=elden_wcn_peripheral_prepare(EldenWcnPatch,elden_wcn_patch,sizeof(elden_wcn_patch),&plan);
    if(!fw)fw=elden_wcn_peripheral_prepare(EldenWcnNvm,elden_wcn_nvm,sizeof(elden_wcn_nvm),&plan);
    if(!fw)fw=elden_wcn_peripheral_prepare(EldenWcnTmel,elden_wcn_tmel,sizeof(elden_wcn_tmel),&plan);
    log("\nELDEN_BT_INSPECT firmware-preflight=%d normal-rf-clk1=0 writes=0\n",fw);if(fw)return;
#if defined(ELDEN_BLUETOOTH_HCI_BUILD)
    EldenWcnHciFirmware hci={};
    fw=elden_wcn_hci_firmware_prepare(&hci,elden_wcn_bt_patch,sizeof(elden_wcn_bt_patch),
                                    elden_wcn_bt_nvm,sizeof(elden_wcn_bt_nvm));
    log("\nELDEN_BT_INSPECT hci-preflight=%d patch=%lu nvm=%lu rom=%04x patch-version=%04x writes=0\n",
        fw,(unsigned long)hci.patch_bytes,(unsigned long)hci.nvm_bytes,hci.rom,hci.patch_version);
    if(fw)return;
#endif
    Region gcc,pins,shared;
    if(!gcc.map(ELDEN_BT_GCC_BASE,ELDEN_BT_GCC_BYTES)||
       !pins.map(ELDEN_BT_TLMM_BASE+ELDEN_BT_PIN_CTS*ELDEN_BT_GPIO_STRIDE,0x4000)||
       !shared.map(ELDEN_BT_TLMM_BASE+ELDEN_BT_PIN_WLAN_EN*ELDEN_BT_GPIO_STRIDE,0x3000)){
        log("\nELDEN_BT_INSPECT refused mapping\n");return;
    }
    const unsigned halts[]={ELDEN_BT_HALT_M_AHB,ELDEN_BT_HALT_S_AHB,ELDEN_BT_HALT_CORE,ELDEN_BT_HALT_CORE_2X,ELDEN_BT_HALT_SE6};
    uint32_t off=0;
    for(unsigned i=0;i<5;i++){uint32_t value=gcc.read(halts[i]);off|=value;log("\nELDEN_BT_INSPECT halt%u=%08x\n",i,value);}
    log("\nELDEN_BT_INSPECT vote=%08x rcg=%08x cfg=%08x m=%08x n=%08x d=%08x dfs=%08x\n",
        gcc.read(ELDEN_BT_GCC_VOTE),gcc.read(ELDEN_BT_RCG),gcc.read(ELDEN_BT_RCG+4),gcc.read(ELDEN_BT_RCG+8),
        gcc.read(ELDEN_BT_RCG+12),gcc.read(ELDEN_BT_RCG+16),gcc.read(ELDEN_BT_RCG+20));
    for(unsigned i=0;i<4;i++)log("\nELDEN_BT_INSPECT gpio%u ctl=%08x io=%08x\n",24+i,pins.read(i*0x1000),pins.read(i*0x1000+4));
    log("\nELDEN_BT_INSPECT wlan-en=%08x sw-ctrl=%08x\n",shared.read(4),shared.read(0x2004));
    if(off&0x80000000u){log("\nELDEN_BT_INSPECT engine skipped clocks-off; no votes made\n");return;}
    Probe p;Region wrapper;
    if(!p.engine.map(ELDEN_BT_SE_BASE,ELDEN_BT_SE_BYTES)||!wrapper.map(ELDEN_BT_QUP_BASE,ELDEN_BT_QUP_BYTES))return;
    PbGeni g={};PbGeniInfo info={};PbGeniIo io={&p,rd,wr,delay,sleep,now};int result=pb_geni_probe(&g,&io,&info);
    log("\nELDEN_BT_INSPECT probe=%d revision=%08x if-disable=%08x param0=%08x param1=%08x ios=%08x wrapper=%08x writes=%u\n",
        result,p.engine.read(PB_SE_FW_REVISION),p.engine.read(PB_SE_IF_DISABLE),p.engine.read(PB_SE_HW_PARAM_0),
        p.engine.read(PB_SE_HW_PARAM_1),p.engine.read(PB_SE_IOS),wrapper.read(4),p.writes);
}
static void worker(void *,wait_result_t){
    for(unsigned i=0;i<12000&&(!elden_recovery_armed()||native_owner_data_status!=0||!elden_init_exec_accepted);i++)IOSleep(10);
    if(elden_recovery_armed()&&native_owner_data_status==0&&elden_init_exec_accepted)inspect();
    thread_terminate(current_thread());
}
}
extern "C" void elden_bluetooth_inspect_start(void){
    unsigned enable=0;PE_parse_boot_argn("elden-bluetooth-inspect",&enable,sizeof(enable));
    if(enable!=1||__atomic_exchange_n(&attempted,1,__ATOMIC_ACQ_REL))return;
    thread_t t=nullptr;if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
}
