/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Adc.h"
#include "Spmi.h"
#include "Policy.h"
#include "EldenAdcResources.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
extern "C" void elden_usb_log_putc(char);
extern "C" {__attribute__((weak)) volatile uint32_t sm8750_adc_owner;}
namespace {
static unsigned attempted;
static void report(const char *format,...){
    char b[336];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);
    for(const char *p=b;*p;++p)elden_usb_log_putc(*p);
}
struct Region {
    IOMemoryMap *mapping=nullptr;uintptr_t base=0;unsigned bytes=0;
    ~Region(){if(mapping)mapping->release();}
    bool map(uint64_t pa,unsigned n){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,n);if(!m)return false;
        mapping=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        base=mapping?(uintptr_t)mapping->getVirtualAddress():0;bytes=n;return base!=0;
    }
    uint32_t read(uint32_t r){
        if(!base||bytes<4||(r&3)||r>bytes-4)return 0xffffffffu;
        uint32_t v;__asm__ volatile("ldr %w0,[%1]":"=r"(v):"r"(base+r):"memory");return v;
    }
    void write(uint32_t r,uint32_t v){
        if(!base||bytes<4||(r&3)||r>bytes-4)return;
        __asm__ volatile("dsb sy\n\tstr %w0,[%1]\n\tdsb sy"::"r"(v),"r"(base+r):"memory");
    }
};
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Region regions[4];EldenAdcSpmi spmi={};EldenAdc adc={};unsigned observerOffset=0;
    bool headers(){return regions[EldenAdcSpmiCore].map(ELDEN_ADC_SPMI_CORE,ELDEN_ADC_SPMI_CORE_BYTES)&&
        regions[EldenAdcSpmiOwner].map(ELDEN_ADC_SPMI_OWNER,ELDEN_ADC_SPMI_OWNER_BYTES);}
    bool channel(bool writes){
        uint64_t obs=ELDEN_ADC_SPMI_OBSERVER+0x8000ull*ELDEN_ADC_SPMI_EE+0x20ull*spmi.apid;
        observerOffset=(unsigned)(obs&4095);
        if(!regions[EldenAdcSpmiObserver].map(obs&~4095ull,4096))return false;
        return !writes||regions[EldenAdcSpmiWriter].map(ELDEN_ADC_SPMI_WRITER+0x1000ull*spmi.apid,4096);
    }
};
static uint32_t busRead(void *p,EldenAdcSpmiSpace space,uint32_t r){
    Context *c=static_cast<Context *>(p);return c->regions[space].read(r+(space==EldenAdcSpmiObserver?c->observerOffset:0));
}
static void busWrite(void *p,EldenAdcSpmiSpace space,uint32_t r,uint32_t v){
    Context *c=static_cast<Context *>(p);c->regions[space].write(r+(space==EldenAdcSpmiObserver?c->observerOffset:0),v);
}
static void busDelay(void *,unsigned us){IODelay(us);}
static void adcDelay(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
static uint64_t nowUs(void *){uint64_t v;__asm__ volatile("mrs %0,cntpct_el0":"=r"(v));return v/96*5+v%96*5/96;}
static void finish(Context *c,int error){
    int released=c->spmi.uncertain?EldenAdcBusy:elden_adc_release(&c->adc);
    report("\nELDEN_ADC end error=%d stage=%u release=%d uncertain=%u quarantine=%u samples=%u commands=%u status=%x\n",
        error,c->adc.stage,released,c->spmi.uncertain,c->adc.quarantined,c->adc.samples,c->spmi.commands,c->spmi.last_status);
    if(!released&&!c->spmi.uncertain){delete c;__atomic_store_n(&sm8750_adc_owner,0u,__ATOMIC_RELEASE);}
    /* Unknown completion keeps the observer/writer maps and lease to reset. */
}
static void run(unsigned mode){
    uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    if(hz!=19200000){report("\nELDEN_ADC refused timer frequency\n");return;}
    uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&sm8750_adc_owner,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){report("\nELDEN_ADC refused lease busy\n");return;}
    Context *c=new Context;
    if(!c){__atomic_store_n(&sm8750_adc_owner,0u,__ATOMIC_RELEASE);return;}
    c->spmi.io={c,busRead,busWrite,busDelay};
    c->adc.io={&c->spmi,elden_adc_spmi_read,elden_adc_spmi_write,nowUs,adcDelay};
    if(!c->headers()){delete c;__atomic_store_n(&sm8750_adc_owner,0u,__ATOMIC_RELEASE);report("\nELDEN_ADC refused header mapping\n");return;}
    int rc=elden_adc_spmi_find(&c->spmi,mode>=2);
    report("\nELDEN_ADC SPMI mode=%u result=%d apid=%u owner=%u matches=%u owned=%u\n",mode,rc,c->spmi.apid,c->spmi.write_owner,c->spmi.matches,c->spmi.owned_matches);
    if(rc||!c->channel(mode>=2)){finish(c,rc?rc:EldenAdcIoError);return;}
    EldenAdcView view={};rc=elden_adc_inspect(&c->adc,&view);
    report("\nELDEN_ADC snapshot result=%d hs=%02x request=%02x status=%02x eoc=%02x previous=%04x config=%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
        rc,view.handshake,view.request,view.status,view.eoc,view.previous_code,
        view.config[0],view.config[1],view.config[2],view.config[3],view.config[4],view.config[5],view.config[6]);
    if(rc||mode==1){finish(c,rc);return;}
    rc=elden_adc_acquire(&c->adc);if(rc){finish(c,rc);return;}
    for(unsigned cycle=0;cycle<(mode==3?12u:1u)&&!rc;cycle++){
        for(unsigned port=1;port<=2;port++){
            uint16_t raw=0;int mc=0;rc=elden_adc_sample(&c->adc,port,&raw,&mc);
            report("\nELDEN_ADC sample cycle=%u port=%u result=%d raw=%04x temperature-mc=%d\n",cycle+1,port,rc,raw,mc);
            if(rc)break;
            /* The absolute-temperature branches need no battery input. The
             * delta branches wait for a separately qualified battery sample. */
            int decision=elden_usb_thermal_decision(port,true,mc,false,0);
            report("\nELDEN_USB_THERMAL shadow port=%u mc=%d battery-known=0 desired-level=%d GPIO-writes=0\n",port,mc,decision);
        }
        if(!rc&&mode==3&&cycle+1<12)IOSleep(5000);
    }
    finish(c,rc);
}
static void worker(void *p,wait_result_t){run((unsigned)(uintptr_t)p);thread_terminate(current_thread());}
}
extern "C" void elden_adc_runtime_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-adc",&mode,sizeof(mode));
    if(mode<1||mode>3||__atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
    thread_t t=nullptr;
    if(kernel_thread_start(worker,(void *)(uintptr_t)mode,&t)==KERN_SUCCESS)thread_deallocate(t);
    else report("\nELDEN_ADC refused worker allocation\n");
}
