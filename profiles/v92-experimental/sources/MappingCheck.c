/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <machine/machine_routines.h>
#include "WcPolicy.h"
int elden_fb_check_mapping(uint64_t address,uint64_t length,uint64_t *bad_page,uint64_t *last_par,uint64_t *mair)
{
    if(!address||(address&4095)||length!=ELDEN_FB_BYTES)return 4;
    boolean_t interrupts=ml_set_interrupts_enabled(FALSE);
    __asm__ volatile("mrs %0, mair_el1":"=r"(*mair));
    unsigned result=((*mair>>8)&255)!=0x44?5:0;
    for(uint64_t offset=0;!result&&offset<ELDEN_FB_MAP_BYTES;offset+=4096){
        uint64_t saved,par;
        __asm__ volatile("mrs %0, par_el1\n\tat s1e1r, %2\n\tisb\n\tmrs %1, par_el1\n\tmsr par_el1, %0"
            :"=&r"(saved),"=&r"(par):"r"(address+offset):"memory");
        *bad_page=offset;*last_par=par;result=elden_fb_par_check(par,ELDEN_FB_PA+offset);
    }
    ml_set_interrupts_enabled(interrupts);return (int)result;
}
