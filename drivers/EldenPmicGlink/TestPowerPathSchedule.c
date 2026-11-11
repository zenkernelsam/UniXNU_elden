/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PowerPathSchedule.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    EldenPowerPathSchedule s={0};unsigned checks=0;
    for(unsigned i=0;i<90000;i+=50){assert(!elden_power_reg_scheduled(&s,true,false,0,i));++checks;}
    assert(!elden_power_reg_scheduled(&s,true,true,0,90000));
    assert(!elden_power_reg_scheduled(&s,true,true,0,99999));
    assert(elden_power_reg_scheduled(&s,true,true,0,100000));
    assert(!elden_power_reg_scheduled(&s,true,true,1,100000));
    assert(!elden_power_reg_scheduled(&s,true,false,0,100001)&&s.ready_at_ms==0);
    assert(!elden_power_reg_scheduled(&s,true,true,0,110000));
    assert(!elden_power_reg_scheduled(&s,true,true,0,119999));
    assert(elden_power_reg_scheduled(&s,true,true,0,120000));
    assert(elden_power_reg_scheduled(&s,false,false,20,120000));
    printf("PowerPathSchedule: %u checks; no PD-transition reads,10s delay,one snapshot,baseline unaffected\n",checks+9);
}
