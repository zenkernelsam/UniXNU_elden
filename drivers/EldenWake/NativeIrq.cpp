/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Irq.h"
#include "NativeIrq.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
extern "C" {
IOReturn armcpu_spi_attach(uint32_t,bool,void (*)(void *),void *,uint32_t *,uint64_t *);
IOReturn armcpu_spi_mask(uint32_t,bool);
extern volatile uint32_t armcpu_gic_ready;
boolean_t ml_at_interrupt_context(void);
}
namespace {
enum {PdcBase=0x0b220000,TlmmGpio=0x0f100000+162*0x1000,GicBase=0x16000000};
struct Window {
    IOMemoryMap *keep=nullptr;uintptr_t address=0;unsigned bytes=0;
    bool map(uint64_t pa,unsigned n){
        if(keep)return false;IODeviceMemory *m=IODeviceMemory::withRange(pa,n);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        address=keep?keep->getVirtualAddress():0;bytes=n;return address!=0;
    }
    int read(unsigned reg,uint32_t *out){
        if(!out||!address||bytes<4||(reg&3)||reg>bytes-4)return -14;uintptr_t p=address+reg;
        __asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(p):"memory");return 0;
    }
    int write(unsigned reg,uint32_t value){
        if(!address||bytes<4||(reg&3)||reg>bytes-4)return -14;uintptr_t p=address+reg;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");return 0;
    }
};
}
#include "NativeIrqMethods.inc"
