/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativePipeline.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/thread.h>
extern "C" {
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*);
IOReturn armcpu_spi_mask(uint32_t,bool);
extern volatile uint32_t armcpu_gic_ready;
boolean_t ml_at_interrupt_context(void);
}
namespace {
struct PipelineWindow {
    IOMemoryMap *keep=nullptr;uintptr_t address=0;uint32_t bytes=0;
    bool map(uint64_t pa,uint32_t size){if(keep)return false;IODeviceMemory *m=IODeviceMemory::withRange(pa,size);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();address=keep?keep->getVirtualAddress():0;bytes=size;return address!=0;}
    int read(uint32_t reg,uint32_t *out){if(!out||!address||(reg&3)||bytes<4||reg>bytes-4)return -14;
        __asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(address+reg):"memory");return 0;}
    int write(uint32_t reg,uint32_t v){if(!address||(reg&3)||bytes<4||reg>bytes-4)return -14;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(v),"r"(address+reg):"memory");return 0;}
};
static uint64_t pipeline_now_us(void *){uint64_t t;__asm__ volatile("mrs %0,cntpct_el0":"=r"(t));return t/96*5+(t%96)*5/96;}
}
#include "NativePipelineMethods.inc"
