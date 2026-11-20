/* SPDX-License-Identifier: GPL-2.0-only */
#include "NativeClocks.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
extern "C" boolean_t ml_at_interrupt_context(void);
namespace {
struct CameraClockWindow {
    IOMemoryMap *keep=nullptr;uintptr_t address=0;uint32_t bytes=0;
    bool map(uint64_t pa,uint32_t length){if(keep)return false;IODeviceMemory *m=IODeviceMemory::withRange(pa,length);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();address=keep?keep->getVirtualAddress():0;bytes=length;return address!=0;}
    int read(uint32_t offset,uint32_t *out){if(!out||!address||(offset&3)||bytes<4||offset>bytes-4)return -14;
        __asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(address+offset):"memory");return 0;}
    int write(uint32_t offset,uint32_t value){if(!address||(offset&3)||bytes<4||offset>bytes-4)return -14;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(address+offset):"memory");return 0;}
};
static uint64_t camera_clock_now(void *){uint64_t ticks;__asm__ volatile("mrs %0,cntpct_el0":"=r"(ticks));return ticks/96*5+(ticks%96)*5/96;}
}
#include "NativeClockMethods.inc"
