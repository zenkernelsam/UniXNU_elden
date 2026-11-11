// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off, read-only Elden SPI4 clock/pin/engine survey. No register writes,
// clock votes, SPI transactions, resets, DMA or touch firmware operations.
#include "EldenTouchBoard.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <stdarg.h>

extern "C" {
extern volatile int native_owner_data_status,elden_init_exec_accepted;
int elden_recovery_armed(void);
void elden_usb_log_putc(char);
}

namespace {
static unsigned attempted;
static void report(const char *format,...) {
    char line[320];va_list args;va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);va_end(args);
    for(const char *p=line;*p;++p)elden_usb_log_putc(*p);
}
// Keep each MMIO access as one naturally aligned ordinary 32-bit LDR. Do not
// use paired, unprivileged or post-index loads on this device mapping.
__attribute__((noinline)) static uint32_t read32(uintptr_t address) {
    uint32_t value;__asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(address):"memory");
    return value;
}
struct Region {
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned length=0;
    ~Region(){if(keep)keep->release();}
    bool map(uint64_t pa,unsigned bytes) {
        if(keep || !bytes || (pa&0xfff) || (bytes&0xfff))return false;
        IODeviceMemory *memory=IODeviceMemory::withRange(pa,bytes);
        if(!memory)return false;
        keep=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
        va=keep?static_cast<uintptr_t>(keep->getVirtualAddress()):0;
        length=bytes;return va!=0;
    }
    uint32_t read(unsigned offset) const {
        // All callers use fixed source-audited offsets, inside mapped pages.
        if(!va || (offset&3) || length<4 || offset>length-4)return 0xffffffffu;
        return read32(va+offset);
    }
};
static void inspect() {
    Region vote,core,source,pins,irq;
    report("\nELDEN_TOUCH_INSPECT begin v=1 writes=0 spi-transactions=0 dma=0\n");
    if(!vote.map(ELDEN_TOUCH_GCC_BASE+0x52000,0x1000) ||
       !core.map(ELDEN_TOUCH_GCC_BASE+0x23000,0x1000) ||
       !source.map(ELDEN_TOUCH_GCC_BASE+0x18000,0x1000) ||
       !pins.map(ELDEN_TOUCH_TLMM_BASE+ELDEN_TOUCH_GPIO_MISO*ELDEN_TOUCH_GPIO_STRIDE,0x4000) ||
       !irq.map(ELDEN_TOUCH_TLMM_BASE+ELDEN_TOUCH_GPIO_IRQ*ELDEN_TOUCH_GPIO_STRIDE,0x1000)) {
        report("\nELDEN_TOUCH_INSPECT refused mapping\n");return;
    }
    const uint32_t voted=vote.read(ELDEN_TOUCH_GCC_VOTE&0xfff);
    const uint32_t c2=core.read(ELDEN_TOUCH_GCC_HALT_CORE_2X&0xfff);
    const uint32_t c=core.read(ELDEN_TOUCH_GCC_HALT_CORE&0xfff);
    const uint32_t m=core.read(ELDEN_TOUCH_GCC_HALT_M_AHB&0xfff);
    const uint32_t s=core.read(ELDEN_TOUCH_GCC_HALT_S_AHB&0xfff);
    const uint32_t se=source.read(ELDEN_TOUCH_GCC_HALT_S4&0xfff);
    report("\nELDEN_TOUCH_INSPECT gcc vote=%08x core2x=%08x core=%08x m-ahb=%08x s-ahb=%08x s4=%08x\n",voted,c2,c,m,s,se);
    report("\nELDEN_TOUCH_INSPECT rcg cmd=%08x cfg=%08x m=%08x n=%08x d=%08x\n",
        source.read((ELDEN_TOUCH_GCC_S4_RCG&0xfff)+0),source.read((ELDEN_TOUCH_GCC_S4_RCG&0xfff)+4),
        source.read((ELDEN_TOUCH_GCC_S4_RCG&0xfff)+8),source.read((ELDEN_TOUCH_GCC_S4_RCG&0xfff)+12),
        source.read((ELDEN_TOUCH_GCC_S4_RCG&0xfff)+16));
    for(unsigned n=0;n<4;++n) {
        unsigned off=n*ELDEN_TOUCH_GPIO_STRIDE;uint32_t ctl=pins.read(off),io=pins.read(off+4);
        report("\nELDEN_TOUCH_INSPECT gpio%u ctl=%08x io=%08x mux=%u pull=%u drive=%u oe=%u\n",
            ELDEN_TOUCH_GPIO_MISO+n,ctl,io,(ctl>>2)&15,ctl&3,(ctl>>6)&7,(ctl>>9)&1);
    }
    report("\nELDEN_TOUCH_INSPECT irq gpio%u ctl=%08x io=%08x intr-cfg=%08x intr-status=%08x\n",
        ELDEN_TOUCH_GPIO_IRQ,irq.read(0),irq.read(4),irq.read(8),irq.read(12));
    // GENI reads need its clocks; a successful page mapping alone is not proof
    // that the serial engine is accessible. Skip when any required branch is off.
    if((c2|c|m|s|se)&ELDEN_TOUCH_GCC_CLK_OFF) {
        report("\nELDEN_TOUCH_INSPECT engine skipped clocks-off; no votes made\n");return;
    }
    Region wrapper,engine;
    if(!wrapper.map(ELDEN_TOUCH_QUP_BASE,ELDEN_TOUCH_QUP_SIZE) ||
       !engine.map(ELDEN_TOUCH_SE_BASE,ELDEN_TOUCH_SE_SIZE)) {
        report("\nELDEN_TOUCH_INSPECT engine mapping failed\n");return;
    }
    const uint32_t revision=engine.read(0x68),param=engine.read(0xe24);
    report("\nELDEN_TOUCH_INSPECT engine wrapper=%08x revision=%08x protocol=%u if-disable=%08x hw-param=%08x status=%08x dma-mode=%08x\n",
        wrapper.read(4),revision,(revision>>8)&255,engine.read(0x64),param,engine.read(0x40),engine.read(0x258));
    report("\nELDEN_TOUCH_INSPECT engine clk-select=%08x serial-clk=%08x m-cmd=%08x m-irq=%08x\n",
        engine.read(0x7c),engine.read(0x48),engine.read(0x600),engine.read(0x610));
    report("\nELDEN_TOUCH_INSPECT end writes=0 no-controller-claim\n");
}
static void worker(void *,wait_result_t) {
    for(unsigned n=0;n<12000 && (!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted);++n)IOSleep(10);
    if(elden_recovery_armed() && native_owner_data_status==0 && elden_init_exec_accepted)inspect();
    else report("\nELDEN_TOUCH_INSPECT refused baseline not ready\n");
    thread_terminate(current_thread());
}
}
extern "C" void elden_touch_inspect_start(void) {
    unsigned enabled=0;PE_parse_boot_argn("elden-touch-inspect",&enabled,sizeof(enabled));
    if(enabled!=1 || __atomic_exchange_n(&attempted,1,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else report("\nELDEN_TOUCH_INSPECT refused worker allocation\n");
}
