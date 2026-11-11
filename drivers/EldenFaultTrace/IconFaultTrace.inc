// SPDX-License-Identifier: BSD-2-Clause-Patent
// Called synchronously from the existing unresolved EL0 fault observer.
#include <machine/machine_routines.h>
#include "FaultFrames.h"
extern "C" int elden_icon_fault_capture(uint64_t,uint64_t,uint64_t *,EldenFaultFrames *);
static __attribute__((noinline)) void eldenIconFaultTrace(int pid,const char *name,uint64_t pc,uint64_t lr)
{
    if(!name||strcmp(name,"iconservicesagent")||pc!=0||!lr||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return;
    static unsigned attempts;
    if(__atomic_fetch_add(&attempts,1u,__ATOMIC_RELAXED)>=4)return;
    uint64_t fp=0;EldenFaultFrames chain;
    if(!elden_icon_fault_capture(pc,lr,&fp,&chain))return;
    char line[600];int n=snprintf(line,sizeof(line),"\nELDEN_ICON_CHAIN pid=%d fp=%llx count=%u reason=%u read-error=%d frames=",pid,(unsigned long long)fp,chain.count,chain.reason,chain.read_error);
    if(n<0||(unsigned)n>=sizeof(line))return;
    for(unsigned i=0;i<chain.count;i++){
        int added=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%llx",i?",":"",(unsigned long long)chain.addresses[i]);
        if(added<0||(unsigned)added>=sizeof(line)-(unsigned)n)return;n+=added;
    }
    if((unsigned)n+1>=sizeof(line))return;line[n++]='\n';
    // Atomic enqueue or explicit failure: no sleeping, spinning or allocating
    // while handling the fault; no raw stack data or user strings are emitted.
    if(!elden_usb_preview_line(line,(unsigned)n)){
        const char *failed="\nELDEN_ICON_CHAIN queue-full; no retry in exception\n";
        while(*failed)elden_usb_log_putc(*failed++);
    }
}
