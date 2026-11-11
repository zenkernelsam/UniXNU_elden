/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Spmi.h"
#include "EldenAdcResources.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
namespace elden_thermal_native {
struct Region {
    IOMemoryMap *mapping=nullptr;uintptr_t base=0;unsigned bytes=0;
    ~Region(){if(mapping)mapping->release();}
    bool map(uint64_t pa,unsigned n){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,n);if(!m)return false;
        mapping=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        base=mapping?(uintptr_t)mapping->getVirtualAddress():0;bytes=n;return base!=0;
    }
    uint32_t read(uint32_t off){
        if(!base||bytes<4||(off&3)||off>bytes-4)return 0xffffffffu;
        uint32_t v;__asm__ volatile("ldr %w0,[%1]":"=r"(v):"r"(base+off):"memory");return v;
    }
    void write(uint32_t off,uint32_t value){
        if(!base||bytes<4||(off&3)||off>bytes-4)return;
        __asm__ volatile("dsb sy\n\tstr %w0,[%1]\n\tdsb sy"::"r"(value),"r"(base+off):"memory");
    }
};
struct SpmiEndpoint {
    Region regions[4];EldenAdcSpmi bus={};unsigned observerOffset=0;
    static uint32_t read32(void *p,EldenAdcSpmiSpace space,uint32_t off){
        auto s=static_cast<SpmiEndpoint *>(p);return s->regions[space].read(off+(space==EldenAdcSpmiObserver?s->observerOffset:0));
    }
    static void write32(void *p,EldenAdcSpmiSpace space,uint32_t off,uint32_t value){
        auto s=static_cast<SpmiEndpoint *>(p);s->regions[space].write(off+(space==EldenAdcSpmiObserver?s->observerOffset:0),value);
    }
    static void delay(void *,unsigned us){IODelay(us);}
    int open(EldenThermalSpmiTarget target,bool writable){
        bus.io={this,read32,write32,delay};bus.target=target;
        if(!regions[EldenAdcSpmiCore].map(ELDEN_ADC_SPMI_CORE,ELDEN_ADC_SPMI_CORE_BYTES)||
           !regions[EldenAdcSpmiOwner].map(ELDEN_ADC_SPMI_OWNER,ELDEN_ADC_SPMI_OWNER_BYTES))return EldenAdcIoError;
        int rc=elden_adc_spmi_find(&bus,writable);if(rc)return rc;
        uint64_t obs=ELDEN_ADC_SPMI_OBSERVER+0x8000ull*ELDEN_ADC_SPMI_EE+0x20ull*bus.apid;
        observerOffset=(unsigned)(obs&4095);
        if(!regions[EldenAdcSpmiObserver].map(obs&~4095ull,4096))return EldenAdcIoError;
        if(writable&&!regions[EldenAdcSpmiWriter].map(ELDEN_ADC_SPMI_WRITER+0x1000ull*bus.apid,4096))return EldenAdcIoError;
        return 0;
    }
};
inline uint64_t nowUs(void *){uint64_t v;__asm__ volatile("mrs %0,cntpct_el0":"=r"(v));return v/96*5+v%96*5/96;}
inline void delayUs(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
}
