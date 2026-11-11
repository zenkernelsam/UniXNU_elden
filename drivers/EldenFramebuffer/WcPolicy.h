/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#define ELDEN_FB_PA UINT64_C(0xfc800000)
#define ELDEN_FB_BYTES UINT64_C(23152640)
#define ELDEN_FB_MAP_BYTES ((ELDEN_FB_BYTES+4095)&~UINT64_C(4095))
static inline int elden_fb_wc_layout(uint64_t pa,uint64_t bytes,uint32_t width,uint32_t height,uint32_t stride,uint32_t depth,uint32_t rotate,uint64_t physBase,uint64_t physSize,uint64_t consoleState)
{
    if(pa!=ELDEN_FB_PA||bytes!=ELDEN_FB_BYTES||width!=1904||height!=3040||stride!=7616||depth!=32||rotate)return 0;
    if(consoleState!=1||!physBase||!physSize||physSize>UINT64_MAX-physBase)return 0;
    return physBase>=ELDEN_FB_PA+ELDEN_FB_MAP_BYTES||physBase+physSize<=ELDEN_FB_PA;
}
static inline unsigned elden_fb_par_check(uint64_t par,uint64_t expected)
{
    if(par&1)return 1;
    if((par&UINT64_C(0x0000fffffffff000))!=expected)return 2;
    if((par>>56)!=0x44)return 3;
    return 0;
}
