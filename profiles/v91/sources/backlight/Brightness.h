/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
/* CoreBrightness's FadePeriod is unsigned 16.16 seconds, not milliseconds.
 * Pack one bounded request atomically; a newer value supersedes an old fade. */
static inline uint64_t elden_bl_request(int64_t level,uint64_t fixed_seconds){
    if(level<0)level=0;
    if(level>4095)level=4095;
    if(fixed_seconds>10u*65536u)fixed_seconds=10u*65536u;
    uint64_t ms=(fixed_seconds*1000u)>>16;
    return (1ull<<63)|(ms<<16)|level;
}
typedef struct {uint64_t start;unsigned from,to,ms;} EldenBlFade;
static inline void elden_bl_fade_begin(EldenBlFade *f,uint64_t request,unsigned actual,uint64_t now){
    f->start=now;f->from=actual;f->to=(unsigned)(request&0xffff);f->ms=(unsigned)((request>>16)&0xffff);
}
static inline unsigned elden_bl_fade_level(const EldenBlFade *f,uint64_t now){
    uint64_t elapsed=now>=f->start?now-f->start:0;
    if(!f->ms||elapsed>=f->ms)return f->to;
    int64_t distance=(int64_t)f->to-f->from;
    return (unsigned)((int64_t)f->from+distance*(int64_t)elapsed/f->ms);
}
