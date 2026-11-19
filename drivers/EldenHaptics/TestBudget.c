/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Budget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenSharedBudgetLimits limits(void){
    EldenSharedBudgetLimits l={100000,{30000,30000},30000,{UINT64_C(1)<<38,UINT64_C(1)<<38},3};return l;
}
int main(void){
    EldenSharedBudget b={0};EldenSharedBudgetLimits l=limits();CHECK(!elden_shared_budget_init(&b,&l));CHECK(elden_shared_budget_init(&b,&l)==-22);
    uint8_t gain[2]={16,16};CHECK(!elden_shared_budget_ram(&b,1,1,1,0,20000,gain));CHECK(elden_shared_budget_active(&b));
    uint64_t squares[2]={100,100};uint32_t on[2]={20000,20000};
    CHECK(elden_shared_budget_reserve(&b,1,1,2,1,20000,squares,on,20000)==-13); // RAM and RTP consume same budget
    CHECK(!elden_shared_budget_finish(&b,1,1,1,20000,1));CHECK(!elden_shared_budget_active(&b));
    CHECK(elden_shared_budget_reserve(&b,2,1,1,100001,20000,squares,on,20000)==-13); // new client doesn't reset it
    CHECK(!elden_shared_budget_reserve(&b,2,1,1,370001,20000,squares,on,20000));
    CHECK(elden_shared_budget_reserve(&b,2,1,1,370002,20000,squares,on,20000)==-116);
    CHECK(elden_shared_budget_finish(&b,2,1,1,380000,0)==-125);CHECK(elden_shared_budget_active(&b));
    CHECK(elden_shared_budget_reserve(&b,3,1,1,10000000,20000,squares,on,20000)==-13); // failed stop never expires
    CHECK(elden_shared_budget_finish(&b,2,1,1,10000000,1)==-125); // can't reset held by a success flag
    memset(&b,0,sizeof(b));CHECK(!elden_shared_budget_init(&b,&l));
    CHECK(!elden_shared_budget_reserve(&b,1,1,1,100,20000,squares,on,20000));
    CHECK(elden_shared_budget_finish(&b,1,1,1,99,1)==-110);CHECK(b.clock_failed&&elden_shared_budget_active(&b));
    CHECK(elden_shared_budget_reserve(&b,1,1,2,101,20000,squares,on,20000)==-110);
    memset(&b,0,sizeof(b));l.max_on_us[0]=l.max_on_us[1]=100000;l.max_dual_on_us=100000;CHECK(!elden_shared_budget_init(&b,&l));
    uint64_t zero_square[2]={0};uint32_t zero_on[2]={0};
    for(unsigned i=0;i<64;i++)CHECK(!elden_shared_budget_reserve(&b,1,1,i+1,i,20000,zero_square,zero_on,0));
    CHECK(elden_shared_budget_reserve(&b,1,1,65,10000000,20000,zero_square,zero_on,0)==-16);
    for(unsigned i=0;i<64;i++)CHECK(!elden_shared_budget_finish(&b,1,1,i+1,10000000,1));
    CHECK(!elden_shared_budget_reserve(&b,2,1,1,10100001,20000,zero_square,zero_on,0));
    CHECK(elden_shared_budget_reserve(&b,2,1,2,UINT64_MAX-1,20000,zero_square,zero_on,0)==-22);
    memset(&b,0,sizeof(b));l=limits();CHECK(!elden_shared_budget_init(&b,&l));
    CHECK(!elden_shared_budget_reserve(&b,1,1,1,0,60000000,squares,on,20000));
    CHECK(!elden_shared_budget_finish_actual(&b,1,1,1,20000,1));
    CHECK(b.slots[0].expires_us==120000); // physical early STOP, not planned60s
    CHECK(!elden_shared_budget_reserve(&b,2,1,1,120001,20000,squares,on,20000));
    memset(&b,0,sizeof(b));l=limits();CHECK(!elden_shared_budget_init(&b,&l));
    uint64_t reserved_square[2]={1000,1000},actual_square[2]={10,20};uint32_t reserved_on[2]={30000,30000},actual_on[2]={1000,2000};
    CHECK(!elden_shared_budget_reserve(&b,1,1,1,0,60000000,reserved_square,reserved_on,30000));
    CHECK(!elden_shared_budget_settle_actual(&b,1,1,1,3000,1,actual_square,actual_on));
    CHECK(!elden_shared_budget_active(&b)&&b.slots[0].squares[0]==10&&b.slots[0].on_us[1]==2000&&b.slots[0].dual_us==1000&&b.slots[0].expires_us==103000);
    CHECK(!elden_shared_budget_reserve(&b,2,1,1,3001,20000,squares,on,20000)); // actual cost survives reconnect
    CHECK(elden_shared_budget_settle_actual(&b,1,1,1,3002,1,actual_square,actual_on)==-125); // already finished
    actual_on[0]=30001;
    CHECK(elden_shared_budget_settle_actual(&b,2,1,1,3003,1,actual_square,actual_on)==-13);
    CHECK(b.slots[1].held&&elden_shared_budget_active(&b));
    CHECK(elden_shared_budget_finish_actual(&b,2,1,1,10000000,1)==-125); // a later flag cannot erase the overrun
    puts("PASS: shared RAM/RTP budgets, reconnects, active/failed-stop retention, slot limits and clock refusal");
    printf("%u actual budget checks\n",checks);return 0;
}
