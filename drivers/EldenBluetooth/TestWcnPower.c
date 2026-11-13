/* SPDX-License-Identifier: GPL-2.0-only */
#include "WcnPower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"%u %s\n",__LINE__,#c);exit(1);}}while(0)
typedef struct {
    EldenWcnRailState rails[EldenWcnRailCount];
    uint64_t time;
    unsigned fault,phase,qmp_count,votes,gpio_calls;
    bool high,wlan,clock_stuck;
    char messages[16][64];
} Model;
static int preflight(void *ctx){return ((Model *)ctx)->fault==1?-19:0;}
static int read_rail(void *ctx,EldenWcnRail i,EldenWcnRailState *out){
    Model *m=ctx;
    if(m->fault==2&&i==EldenWcnS7i&&!m->votes)return -5;
    *out=m->rails[i];if(m->fault==6&&m->votes&&i==EldenWcnS7i)out->mv=1224;
    return 0;
}
static int vote(void *ctx,EldenWcnRail i,uint16_t mv,uint8_t mode,bool enable){
    Model *m=ctx;CHECK(m->qmp_count==4&&!m->high);CHECK(enable&&mode==7);
    CHECK(mv>=m->rails[i].mv&&mv>=elden_wcn_rail_specs[i].minimum&&mv<=elden_wcn_rail_specs[i].maximum);
    m->votes++;if(m->fault==5&&i==EldenWcnS7i)return -110;
    m->rails[i]=(EldenWcnRailState){mv,0x80,0x80,7};return 0;
}
static int qmp(void *ctx,const char *text){
    Model *m=ctx;CHECK(strlen(text)<64);CHECK(m->qmp_count<16);
    if(m->qmp_count<4)CHECK(!m->votes&&!m->gpio_calls);
    strcpy(m->messages[m->qmp_count++],text);
    return m->fault==4&&m->qmp_count==2?-110:0;
}
static int set_bt(void *ctx,bool high){
    Model *m=ctx;m->gpio_calls++;CHECK(m->qmp_count>=4);
    if(high){CHECK(m->votes==6);CHECK(m->time>=50000+(m->wlan?0:100000));}
    if(m->fault==7)return -5;m->high=high;return 0;
}
static int get_bt(void *ctx,bool *high){Model *m=ctx;*high=m->fault==8&&m->high?false:m->high;return 0;}
static int get_wlan(void *ctx,bool *high){*high=((Model *)ctx)->wlan;return 0;}
static int parents(void *ctx,uint32_t *main,uint32_t *even){
    if(((Model *)ctx)->fault==3)return -19;*main=600000000;*even=300000000;return 0;
}
static uint64_t now(void *ctx){return ((Model *)ctx)->time;}
static void delay(void *ctx,unsigned us){Model *m=ctx;if(!m->clock_stuck)m->time+=us;}
static void setup(Model *m,EldenWcnPower *p){
    for(unsigned i=0;i<EldenWcnRailCount;i++)
        m->rails[i]=(EldenWcnRailState){(uint16_t)(i==EldenWcnS7i?1304:elden_wcn_rail_specs[i].minimum),0x80,0x80,3};
    p->io=(EldenWcnPowerIo){m,preflight,read_rail,vote,qmp,set_bt,get_bt,get_wlan,parents,now,delay};
}
static void cpr(void){
    EldenWcnPmuParam params[3]={{"VDDD_AON_0P9",860,800,true,true},
                              {"VDDD_BTCX_0P9",900,820,true,true},
                              {"VDDA_RFA_1P3",1200,1100,true,true}};
    EldenWcnPdcVote votes[4];CHECK(!elden_wcn_cpr_plan(params,3,votes));
    CHECK(votes[0].wake==914&&votes[0].sleep==814&&votes[3].wake==1214&&votes[3].sleep==1094);
    CHECK(!votes[1].wake_valid&&!votes[2].wake_valid);
    EldenWcnPdcVote saved[4];memcpy(saved,votes,sizeof(saved));
    params[2].wake=UINT32_MAX;CHECK(elden_wcn_cpr_plan(params,3,votes)<0&&
        !memcmp(saved,votes,sizeof(saved)));
    Model m={0};EldenWcnPower p={0};setup(&m,&p);
    CHECK(!elden_wcn_power_acquire(&p,10));unsigned before=m.qmp_count;
    CHECK(elden_wcn_power_cpr(&p,10,params,3)<0&&m.qmp_count==before);
    params[2].wake=1200;CHECK(!elden_wcn_power_cpr(&p,10,params,3));
    CHECK(m.qmp_count==8&&strstr(m.messages[4],"s5f.v, upval: 914")&&strstr(m.messages[7],"s7i.v, dwnval: 1094"));
}
int main(void){
    unsigned scenarios=0;
    for(unsigned test=0;test<11;test++){
        Model m={0};EldenWcnPower p={0};m.fault=test<=8?test:0;m.wlan=test==9;m.clock_stuck=test==10;setup(&m,&p);
        int rc=elden_wcn_power_acquire(&p,1);scenarios++;
        bool success=test==0||test==9;
        if(success){
            CHECK(!rc&&p.state==EldenWcnPowerReady&&m.high);
            CHECK(m.qmp_count==4&&m.votes==6&&m.gpio_calls==3);
            CHECK(m.time==(m.wlan?100000:200000));
            CHECK(!elden_wcn_power_check(&p,1,1)&&elden_wcn_power_check(&p,2,1)<0);
            CHECK(elden_wcn_power_check(&p,1,3)<0);
            CHECK(!elden_wcn_power_stop(&p,1)&&!m.high&&p.state==EldenWcnPowerStopped);
            CHECK(!elden_wcn_power_check(&p,1,3)&&elden_wcn_power_check(&p,1,2)<0);
            CHECK(!elden_wcn_power_finish(&p,1)&&p.baseline_votes_retained);
            CHECK(elden_wcn_power_check(&p,1,3)<0);
        }else{
            CHECK(rc);
            if(test<=3)CHECK(!m.qmp_count&&!m.votes&&!m.gpio_calls);
            else{
                CHECK(p.quarantined);unsigned controls=m.qmp_count+m.votes+m.gpio_calls;
                CHECK(elden_wcn_power_acquire(&p,2)<0&&elden_wcn_power_stop(&p,1)<0);
                CHECK(controls==m.qmp_count+m.votes+m.gpio_calls);
            }
        }
    }
    {
        Model m={0};EldenWcnPower p={0};setup(&m,&p);m.rails[EldenWcnS7i].mv=1344;
        CHECK(elden_wcn_power_acquire(&p,2)<0&&!m.qmp_count&&!m.gpio_calls);scenarios++;
    }
    uint32_t main=0,even=0;
    CHECK(!elden_wcn_parent_decode(0x80000001,1,31,0x4000,7|(1u<<10),&main,&even)&&main==600000000&&even==300000000);
    CHECK(elden_wcn_parent_decode(1,1,31,0x4000,7|(1u<<10),&main,&even)<0);
    CHECK(elden_wcn_parent_decode(0x80000001,1,32,0x4000,7|(1u<<10),&main,&even)<0);
    CHECK(elden_wcn_parent_decode(0x80000001,1,31,0x4000,7,&main,&even)<0);
    /* Exact first v99 hardware observation: a running vote-mode GPLL0 was
     * incorrectly rejected because its manual OPMODE and unused output differ. */
    CHECK(!elden_wcn_parent_decode(0xe0000005,0,31,0x4000,0x02000403,&main,&even)&&
        main==600000000&&even==300000000);
    CHECK(!elden_wcn_parent_decode(0xe0000005,1,31,0x4000,0x02000403,&main,&even));
    CHECK(!elden_wcn_parent_decode(0x80000001,1,31,0x4000,0x403,&main,&even));
    const uint32_t invalid[][5]={
        {0xe0000005,0,31,0x4000,0x403}, /* manual standby */
        {0x60000005,0,31,0x4000,0x02000403}, /* unlocked */
        {0xe0000004,0,31,0x4000,0x02000403}, /* output disabled */
        {0xe0000005,0,31,0x4000,0x02000402}, /* main disabled */
        {0xe0000005,0,31,0x4000,0x02000401}, /* even disabled */
        {0xe0000005,2,31,0x4000,0x02000403}, /* unsupported OPMODE */
        {0xe0000005,0,32,0x4000,0x02000403}, /* wrong main rate */
        {0xe0000005,0,31,0x4001,0x02000403}, /* wrong fractional rate */
        {0xe0000005,0,31,0x4000,0x02000803}, /* unsupported divider */
        {0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        main=123;even=456;
        CHECK(elden_wcn_parent_decode(invalid[i][0],invalid[i][1],invalid[i][2],
            invalid[i][3],invalid[i][4],&main,&even)==EldenWcnPowerIdentity);
        CHECK(main==123&&even==456);
    }
    cpr();
    printf("PASS %u power scenarios, %u sequence/lease/rail/PLL/OL-CPR assertions\n",scenarios,checks);
    return 0;
}
