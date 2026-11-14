/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Called only by the existing exact-name/CDHash, bounded exit observer.
 * Reads the current thread's saved EL0 frame chain before teardown; no policy
 * or task state is changed and failed user reads simply stop the trace. */
#include <mach/arm/thread_status.h>
#include <machine/machine_routines.h>
static void elden_bt_exit_stack(proc_t p)
{
    if(p!=current_proc()||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return;
    /* kern_exec's historical forward declaration returns struct savearea*;
     * the native ARM implementation returns the same pointer to its saved state. */
    arm_saved_state_t *state=(arm_saved_state_t *)(void *)get_user_regs(current_thread());
    if(!state||!is_saved_state64(state)||(saved_state64(state)->cpsr&15)!=0)return;
    uint64_t pc=get_saved_state_pc(state),lr=get_saved_state_lr(state),fp=get_saved_state_fp(state);
    printf("ELDEN_BT_EXIT_STACK pid=%d pc=%llx lr=%llx fp=%llx syscall=%llx\n",
           proc_pid(p),(unsigned long long)pc,(unsigned long long)lr,
           (unsigned long long)fp,(unsigned long long)get_saved_state_reg(state,16));
    for(unsigned i=0;i<8&&fp;i++){
        if(fp<4096||fp>=(UINT64_C(1)<<39)-16||(fp&15))break;
        uint64_t pair[2];int error=copyin((user_addr_t)fp,pair,sizeof(pair));
        if(error){printf("ELDEN_BT_EXIT_FRAME pid=%d index=%u error=%d\n",proc_pid(p),i,error);break;}
        printf("ELDEN_BT_EXIT_FRAME pid=%d index=%u return=%llx\n",proc_pid(p),i,(unsigned long long)pair[1]);
        if(!pair[0]||pair[0]<=fp||pair[0]-fp>1048576)break;
        fp=pair[0];
    }
}
