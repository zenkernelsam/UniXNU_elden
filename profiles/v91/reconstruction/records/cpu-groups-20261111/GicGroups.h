/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
namespace elden_gic_groups {
// Linux's NS distributor view: ARE_NS|ENABLE_G1A|ENABLE_G1 =0x13.
// This bounded late fix adds only bit0 and preserves the observed other bits.
template<class Read,class Write,class Wait,class Ready>
static int enable(Read read,Write write,Wait wait,Ready ready,
                  volatile uint32_t *before,volatile uint32_t *after)
{
    *before=0;*after=0;
    if(!ready())return 1;
    if(!wait())return 2;
    const uint32_t original=read();*before=original;
    if((original&0x12U)!=0x12U)return 3;
    if(!ready())return 1;
    const uint32_t wanted=original|1U;
    if(wanted!=original)write(wanted);
    if(!wait())return 2;
    const uint32_t actual=read();*after=actual;
    return ((actual^wanted)&0x7fffffffU)==0?0:4;
}
}
