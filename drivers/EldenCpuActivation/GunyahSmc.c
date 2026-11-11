/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "GunyahWatchdog.h"
/* SMCCC1.1 SMC32 vendor-hypervisor ABI. No MMIO or SPMI access. */
EldenVdogReply elden_vdog_smc(void *unused,uint32_t fn,uint32_t arg1,uint32_t arg2)
{
    (void)unused;
    if(!((fn==ELDEN_VDOG_STATUS||fn==ELDEN_VDOG_PET)?(arg1==0&&arg2==0):
         fn==ELDEN_VDOG_TIME?(arg1==ELDEN_VDOG_BARK_MS&&arg2==ELDEN_VDOG_TIMEOUT_MS):
         fn==ELDEN_VDOG_CONTROL?((arg1==2||arg1==3)&&arg2==0):0))
        return (EldenVdogReply){(uint64_t)(int64_t)ELDEN_VDOG_BAD_STATE,0,0,0};
    register uint64_t x0 __asm__("x0")=fn,x1 __asm__("x1")=arg1;
    register uint64_t x2 __asm__("x2")=arg2,x3 __asm__("x3")=0;
    __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::
                     "x4","x5","x6","x7","x8","x9","x10","x11",
                     "x12","x13","x14","x15","x16","x17","cc","memory");
    return (EldenVdogReply){x0,x1,x2,x3};
}
