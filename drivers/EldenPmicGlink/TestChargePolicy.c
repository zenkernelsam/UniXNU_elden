/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargePolicy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static int reply(EldenChargePolicy *s,uint32_t value,uint32_t error){
    uint8_t p[24]={0};put(p,32778);put(p+4,1);put(p+8,s->last_opcode);put(p+12,s->last_property);put(p+16,value);put(p+20,error);
    return elden_charge_policy_receive(s,p,sizeof(p));
}
static void request(EldenChargePolicy *s,unsigned op,unsigned prop,unsigned value){
    uint8_t p[24];CHECK(elden_charge_policy_next(s,true,p)==24);
    CHECK(get(p)==32778&&get(p+4)==1&&get(p+8)==op&&get(p+12)==0&&get(p+16)==prop&&get(p+20)==value);
    CHECK(!elden_charge_policy_next(s,true,p));CHECK(!(op==0x60&&prop==28));
}
static void init(EldenChargePolicy *s){
    elden_charge_policy_init(s);elden_charge_policy_link(s,true);
    request(s,0x60,29,0);CHECK(reply(s,95100,0)==1);
    request(s,0x60,30,0);CHECK(reply(s,176080,0)==1);
    CHECK(s->phase==EldenPolicyReady&&s->original_window==95100);
    elden_charge_policy_observe(s,10,5000,1);
}
int main(void){
    CHECK(elden_charge_window_valid(95100));CHECK(elden_charge_window_valid(0));
    CHECK(elden_charge_window_valid(176080));CHECK(elden_charge_window_valid(140060));
    CHECK(!elden_charge_window_valid(276080));CHECK(!elden_charge_window_valid(180076));CHECK(!elden_charge_window_valid(199999));
    EldenChargePolicy s;uint8_t p[24];init(&s);
    CHECK(elden_charge_policy_select(&s,0)==-1);CHECK(elden_charge_policy_select(&s,5)==-1);
    CHECK(elden_charge_policy_select(&s,EldenChargeBypass)==0);
    CHECK(!elden_charge_policy_next(&s,false,p));request(&s,0x61,28,0);CHECK(reply(&s,0,0)==1);
    elden_charge_policy_observe(&s,11,5000,0);CHECK(s.phase==EldenPolicyWaitEnable);
    CHECK(!elden_charge_policy_next(&s,true,p));elden_charge_policy_observe(&s,12,5000,0);
    CHECK(s.applied_mode==EldenChargeBypass&&s.phase==EldenPolicyReady);
    CHECK(elden_charge_policy_select(&s,EldenChargeLimit80)==0);request(&s,0x61,29,176080);CHECK(reply(&s,0,0)==1);
    request(&s,0x60,29,0);CHECK(reply(&s,176080,0)==1);
    elden_charge_policy_observe(&s,13,5000,0);CHECK(s.phase==EldenPolicyWaitWindow);
    elden_charge_policy_observe(&s,14,5000,0);CHECK(s.phase==EldenPolicySetEnable&&s.target_enable==1);
    request(&s,0x61,28,1);CHECK(reply(&s,0,0)==1);elden_charge_policy_observe(&s,16,5000,1);
    CHECK(s.applied_mode==EldenChargeLimit80&&!s.firmware_held);
    CHECK(elden_charge_policy_select(&s,EldenChargeWindow4060)==0);request(&s,0x61,29,140060);CHECK(reply(&s,0,0)==1);
    request(&s,0x60,29,0);CHECK(reply(&s,140060,0)==1);elden_charge_policy_observe(&s,18,6500,0);
    CHECK(s.phase==EldenPolicyReady&&s.applied_mode==EldenChargeWindow4060);CHECK(!elden_charge_policy_next(&s,true,p));
    CHECK(elden_charge_policy_select(&s,EldenChargeStock)==0);request(&s,0x61,29,95100);CHECK(reply(&s,0,0)==1);
    request(&s,0x60,29,0);CHECK(reply(&s,95100,0)==1);elden_charge_policy_observe(&s,20,8100,0);
    CHECK(s.phase==EldenPolicyReady&&s.observed_enable==0); /* original EU80 still protects */
    CHECK(elden_charge_policy_select(&s,EldenChargeBypass)==0);request(&s,0x61,28,0);elden_charge_policy_timeout(&s);
    CHECK(s.phase==EldenPolicyFailed&&s.timeouts==1);CHECK(reply(&s,0,0)==0);
    elden_charge_policy_link(&s,true);CHECK(s.phase==EldenPolicyFailed);
    elden_charge_policy_link(&s,false);elden_charge_policy_link(&s,true);request(&s,0x60,29,0);CHECK(reply(&s,140060,0)==1);
    CHECK(s.original_window==95100);request(&s,0x60,30,0);CHECK(reply(&s,176080,0)==1);
    init(&s);CHECK(elden_charge_policy_select(&s,EldenChargeLimit80)==0);request(&s,0x61,29,176080);
    CHECK(reply(&s,0,4)==-1&&s.phase==EldenPolicyFailed);
    init(&s);CHECK(elden_charge_policy_select(&s,EldenChargeBypass)==0);request(&s,0x61,28,0);CHECK(reply(&s,0,0)==1);
    elden_charge_policy_observe(&s,12,5000,1);CHECK(s.phase==EldenPolicyFailed); /* never claim failed inhibit */
    init(&s);CHECK(elden_charge_policy_select(&s,EldenChargeBypass)==0);request(&s,0x61,28,0);
    uint8_t ack[24]={0};put(ack,32778);put(ack+4,1);put(ack+8,0x61);
    CHECK(elden_charge_policy_receive(&s,ack,sizeof(ack))==1&&s.phase==EldenPolicyWaitEnable);
    elden_charge_policy_observe(&s,12,5000,0);CHECK(s.applied_mode==EldenChargeBypass);
    printf("ChargePolicy: %u checks passed\n",checks);return 0;
}
