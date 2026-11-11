/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
typedef struct { uint64_t start,last,last_ready; uint32_t duration_ms,mask; } EldenSmpHold;
static inline int elden_smp_hold_init(EldenSmpHold *p,unsigned seconds,uint64_t now,uint32_t mask)
{
    if(!p||seconds<60||seconds>600||mask!=255)return 0;
    p->start=now;p->last=now;p->last_ready=now;p->duration_ms=seconds*1000;p->mask=mask;return 1;
}
/* 0 permits one pet; 5 pauses without pet; other outcomes end this hold. */
static inline unsigned elden_smp_hold_step(EldenSmpHold *p,uint64_t now,int ready,uint32_t online,int error)
{
    if(now<p->last)return 1;
    p->last=now;
    if(now-p->start>=p->duration_ms)return 2;
    if(online!=p->mask||error)return 4;
    if(!ready)return now-p->last_ready>=10000?3:5;
    p->last_ready=now;
    return 0;
}
