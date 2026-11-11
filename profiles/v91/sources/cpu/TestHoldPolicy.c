#include "HoldPolicy.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    EldenSmpHold h;
    assert(!elden_smp_hold_init(&h,0,0,255));
    assert(!elden_smp_hold_init(&h,59,0,255));
    assert(!elden_smp_hold_init(&h,601,0,255));
    assert(!elden_smp_hold_init(&h,240,0,3));
    assert(!elden_smp_hold_init(&h,~0U,0,255));
    assert(elden_smp_hold_init(&h,240,10000,255));
    for(unsigned t=11000;t<250000;t+=1000)assert(!elden_smp_hold_step(&h,t,1,255,0));
    assert(elden_smp_hold_step(&h,250000,1,255,0)==2);
    assert(elden_smp_hold_init(&h,60,10000,255));
    assert(elden_smp_hold_step(&h,9999,1,255,0)==1);
    assert(elden_smp_hold_init(&h,60,10000,255));
    assert(elden_smp_hold_step(&h,11000,0,255,0)==5);
    assert(elden_smp_hold_step(&h,12000,1,255,0)==0);
    assert(elden_smp_hold_step(&h,21999,0,255,0)==5);
    assert(elden_smp_hold_step(&h,22000,0,255,0)==3);
    assert(elden_smp_hold_init(&h,60,10000,255));
    assert(elden_smp_hold_step(&h,11000,1,127,0)==4);
    assert(elden_smp_hold_init(&h,60,10000,255));
    assert(elden_smp_hold_step(&h,11000,1,255,1)==4);
    puts("PASS bounded hold, exact expiry, backward clock, missing readiness/core, work failure, invalid config");
}
