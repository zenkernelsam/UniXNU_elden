/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
typedef enum { EldenSmpWait, EldenSmpLaunchWork, EldenSmpQualified, EldenSmpFailed } EldenSmpAction;
typedef struct { uint32_t mask,phase,reason; uint64_t since,last_now,last_ready; } EldenSmpPolicy;
static inline int elden_smp_policy_init(EldenSmpPolicy *p,unsigned cpus)
{
    if(!p||cpus<2||cpus>8)return 0;
    p->mask=(1U<<cpus)-1U;p->phase=0;p->reason=0;p->since=0;p->last_now=0;p->last_ready=0;
    return 1;
}
static inline EldenSmpAction elden_smp_policy_step(EldenSmpPolicy *p,uint64_t now,
    int ready,uint32_t online,uint32_t work,uint32_t timers,uint32_t ipis,int error)
{
    if(p->phase==3)return EldenSmpQualified;
    if(p->phase==4)return EldenSmpFailed;
    if(now<p->last_now){p->reason=1;goto failed;}
    p->last_now=now;
    if(error){p->reason=2;goto failed;}
    if(ready)p->last_ready=now;
    else if(now-p->last_ready>=10000){p->reason=3;goto failed;}
    if(p->phase==0){
        if((online&p->mask)==p->mask&&ready){p->phase=1;p->since=now;return EldenSmpLaunchWork;}
        if(now>=15000){p->reason=4;goto failed;}
    }else if(p->phase==1){
        if(((work&timers&ipis)&p->mask)==p->mask&&ready){p->phase=2;p->since=now;}
        else if(now-p->since>=15000){p->reason=5;goto failed;}
    }else if(p->phase==2){
        if((online&p->mask)!=p->mask){p->reason=6;goto failed;}
        if(ready&&now-p->since>=5000){p->phase=3;return EldenSmpQualified;}
    }
    return EldenSmpWait;
failed:
    p->phase=4;return EldenSmpFailed;
}
