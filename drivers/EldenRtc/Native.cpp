/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Core.h"
#include "Spmi.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
extern "C" {
void armdt_platform_clock_register(int (*read)(uint64_t *),void (*set)(uint64_t));
boolean_t ml_at_interrupt_context(void);
}
namespace {
struct Window {
    IOMemoryMap *keep=nullptr;uintptr_t va=0;uint32_t size=0;
    bool map(uint64_t physical,uint32_t bytes){
        if(keep)return false;IODeviceMemory *memory=IODeviceMemory::withRange(physical,bytes);if(!memory)return false;
        keep=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
        va=keep?(uintptr_t)keep->getVirtualAddress():0;size=bytes;return va!=0;
    }
    int read(uint32_t reg,uint32_t *out){
        if(!va||!out||(reg&3)||reg>size-4)return -14;uintptr_t p=va+reg;
        __asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(p):"memory");return 0;
    }
    int write(uint32_t reg,uint32_t value){
        if(!va||(reg&3)||reg>size-4)return -14;uintptr_t p=va+reg;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");return 0;
    }
};
static Window maps[3];static EldenRtcSpmi spmi;static EldenRtc rtc;
static uint32_t owned,initialized,attempted,stopping;
static int lastResult;
static int result(int rc){__atomic_store_n(&lastResult,rc,__ATOMIC_RELEASE);return rc;}
static bool enter(){
    if(ml_at_interrupt_context()||__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return false;
    for(unsigned i=0;i<1000;++i){uint32_t empty=0;if(__atomic_compare_exchange_n(&owned,&empty,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return true;IODelay(1);}return false;
}
static void leave(){__atomic_store_n(&owned,0u,__ATOMIC_RELEASE);}
static int read32(void *,unsigned w,uint32_t reg,uint32_t *out){return w<3?maps[w].read(reg,out):-22;}
static int write32(void *,unsigned w,uint32_t reg,uint32_t value){
    // Only the read transaction command; no PMIC counter/alarm/SDAM writer map.
    return w==EldenRtcSpmiObserver&&reg==0?maps[w].write(reg,value):-22;
}
static void delay(void *,unsigned us){IODelay(us);}
static int prepare(){
    if(attempted)return initialized?0:__atomic_load_n(&lastResult,__ATOMIC_ACQUIRE);attempted=1;
    unsigned mode=0;PE_parse_boot_argn("elden-rtc",&mode,sizeof(mode));
    if(mode!=1)return result(-19);
    if(!maps[0].map(0x0c400000,0x3000)||!maps[1].map(0x0c42d000,0x4000))return result(-12);
    spmi.io={nullptr,read32,write32,delay};int rc=elden_rtc_spmi_select(&spmi);if(rc)return result(rc);
    if(!maps[2].map(0x0c440000+UINT64_C(0x20)*spmi.apid,0x20))return result(-12);
    EldenRtcIo io={&spmi,elden_rtc_spmi_read};rc=elden_rtc_core_init(&rtc,&io);if(rc)return result(rc);
    initialized=1;return 0;
}
static int clockRead(uint64_t *seconds){
    if(!seconds||!enter())return -16;int rc=prepare();if(!rc)rc=elden_rtc_core_read(&rtc,seconds);
    __atomic_store_n(&lastResult,rc,__ATOMIC_RELEASE);leave();return rc;
}
static void clockSet(uint64_t seconds){
    if(!enter())return;int rc=prepare();if(!rc)rc=elden_rtc_core_learn(&rtc,seconds);
    __atomic_store_n(&lastResult,rc,__ATOMIC_RELEASE);leave();
}
/* Registration only: no physical access during constructor/default-off. The
 * platform clock invokes prepare only for explicit elden-rtc=1. */
struct Registration {Registration(){armdt_platform_clock_register(clockRead,clockSet);}} registration;
}
extern "C" int elden_rtc_native_result(void){return __atomic_load_n(&lastResult,__ATOMIC_ACQUIRE);}
extern "C" int elden_rtc_stop_for_reboot(void){
    __atomic_store_n(&stopping,1u,__ATOMIC_RELEASE);
    for(unsigned i=0;i<1000;++i){uint32_t empty=0;
        if(__atomic_compare_exchange_n(&owned,&empty,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
            elden_rtc_core_stop(&rtc);leave();return 0;
        }IODelay(1);
    }
    return -16;
}
