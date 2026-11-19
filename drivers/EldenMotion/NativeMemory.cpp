/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativeMemory.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/task.h>
extern "C" boolean_t ml_at_interrupt_context(void);
/* The captured getBytesNoCopy slot differs from original OS17. All these
 * accesses call the actual original exports directly, never a guessed slot. */
extern "C" {
void *elden_sensor_memory_bytes(IOBufferMemoryDescriptor *) __asm__("__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv");
IOReturn elden_sensor_memory_prepare(IOGeneralMemoryDescriptor *,IOOptionBits) __asm__("__ZN25IOGeneralMemoryDescriptor7prepareEj");
uint64_t elden_sensor_memory_segment(IOGeneralMemoryDescriptor *,IOByteCount,IOByteCount *,IOOptionBits) __asm__("__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj");
uintptr_t elden_sensor_memory_virtual(IOMemoryMap *) __asm__("__ZN11IOMemoryMap17getVirtualAddressEv");
}
namespace {
static uint64_t sensor_memory_translate(uintptr_t address,bool write){
    uint64_t daif,saved,result;
    if(write)__asm__ volatile("mrs %0,daif\n\tmsr daifset,#2\n\tmrs %1,par_el1\n\tat s1e1w,%3\n\tisb\n\tmrs %2,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%0"
        :"=&r"(daif),"=&r"(saved),"=&r"(result):"r"(address):"memory");
    else __asm__ volatile("mrs %0,daif\n\tmsr daifset,#2\n\tmrs %1,par_el1\n\tat s1e1r,%3\n\tisb\n\tmrs %2,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%0"
        :"=&r"(daif),"=&r"(saved),"=&r"(result):"r"(address):"memory");
    return result;
}
static uint32_t sensor_memory_cache_line(){uint64_t ctr;__asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));return 4u<<((ctr>>16)&15);}
static void sensor_memory_clean(void *p,uint64_t bytes,uint32_t line){
    uintptr_t begin=(uintptr_t)p;for(uint64_t offset=0;offset<bytes;offset+=line)__asm__ volatile("dc civac,%0"::"r"(begin+offset):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static void sensor_memory_barrier(){__asm__ volatile("dsb sy":::"memory");}
}
#include "NativeMemoryMethods.inc"
