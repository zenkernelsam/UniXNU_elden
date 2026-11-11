/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <kern/thread.h>
#include <kern/misc_protos.h>
#include <arm/thread.h>
#include <machine/machine_routines.h>
#include "FaultFrames.h"
static int read_pair(void *unused,uint64_t address,uint64_t *pair)
{
    (void)unused;return copyin((user_addr_t)address,pair,16);
}
int elden_icon_fault_capture(uint64_t pc,uint64_t lr,uint64_t *fp,EldenFaultFrames *chain)
{
    if(ml_at_interrupt_context()||!ml_get_interrupts_enabled())return 0;
    arm_saved_state_t *state=current_thread()->machine.upcb;
    if(!state||!is_saved_state64(state)||(saved_state64(state)->cpsr&15)!=0||get_saved_state_pc(state)!=pc||get_saved_state_lr(state)!=lr)return 0;
    *fp=get_saved_state_fp(state);elden_fault_frames(chain,*fp,read_pair,0);return 1;
}
