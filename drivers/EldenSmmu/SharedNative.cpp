/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "SharedNative.h"
#include "Irqs.h"
#include <IOKit/IOLib.h>
extern "C" {
boolean_t ml_at_interrupt_context(void);
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*);
IOReturn armcpu_spi_mask(uint32_t,bool);
extern volatile uint32_t armcpu_gic_ready;
}
namespace {
static int shared_native_read32(uintptr_t base,size_t off,uint32_t *out){if(!base||!out||(off&3))return -14;
    uintptr_t p=base+off;__asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(p):"memory");return 0;}
static int shared_native_read64(uintptr_t base,size_t off,uint64_t *out){if(!base||!out||(off&7))return -14;
    uintptr_t p=base+off;__asm__ volatile("ldr %0,[%1]":"=r"(*out):"r"(p):"memory");return 0;}
static int shared_native_write32(uintptr_t base,size_t off,uint32_t value){if(!base||(off&3))return -14;
    uintptr_t p=base+off;__asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");return 0;}
static int shared_native_write64(uintptr_t base,size_t off,uint64_t value){if(!base||(off&7))return -14;
    uintptr_t p=base+off;__asm__ volatile("str %0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");return 0;}
static void shared_native_barrier(){__asm__ volatile("dsb sy\n\tisb":::"memory");}
static uint64_t shared_native_now(){uint64_t t;__asm__ volatile("mrs %0,cntpct_el0":"=r"(t));return t/96*5+(t%96)*5/96;}
static uint64_t shared_native_affinity(){uint64_t t;__asm__ volatile("mrs %0,mpidr_el1":"=r"(t));return t&UINT64_C(0xff00ffffff);}
}
#include "SharedNativeMethods.inc"
