/* SPDX-License-Identifier: GPL-2.0-only */
#include "WcnPower.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int legacy_elden_wcn_cpr_plan(const EldenWcnPmuParam *,size_t,EldenWcnPdcVote [4]);
int legacy_elden_wcn_power_cpr(EldenWcnPower *,uint64_t,const EldenWcnPmuParam *,size_t);
typedef struct {unsigned calls,fail_at;char messages[8][64];} Recorder;
static int record(void *context,const char *message){
    Recorder *r=context;assert(r->calls<8&&strlen(message)<64);
    strcpy(r->messages[r->calls++],message);return r->calls==r->fail_at?-110:0;
}
static uint32_t seed=0x9122ac07;
static uint32_t random32(void){seed=seed*1664525u+1013904223u;return seed;}
static uint32_t voltage(void){uint32_t r=random32();return (r&31)==0?UINT32_MAX-(r%32):r%2400;}
int main(void){
    const char *names[]={"VDDD_AON_0P9","VDDA_RFA_1P9","VDDA_RFA_1P3","VDDA_RFA_0P9","VDDD_WLMX_0P9","VDDD_WLCX_0P9","VDDD_BTCX_0P9","VDDD_BTCMX_0P9","VDDA_PCIE_1P2","VDDA_PCIE_0P9","unknown",""};
    for(unsigned test=0;test<4096;++test){
        EldenWcnPmuParam params[16]={0};size_t count=1+random32()%16;
        for(size_t i=0;i<count;++i){
            strcpy(params[i].pin,names[random32()%12]);params[i].wake=voltage();params[i].sleep=voltage();
            params[i].wake_valid=(random32()>>12)&1;params[i].sleep_valid=(random32()>>7)&1;
        }
        if(test%31==0)memset(params[0].pin,'X',sizeof(params[0].pin));
        EldenWcnPdcVote a[4],b[4];memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
        int old=legacy_elden_wcn_cpr_plan(params,count,a),now=elden_wcn_cpr_plan(params,count,b);
        assert(old==now);
        if(old)assert(!memcmp(a,b,sizeof(a)));
        else for(unsigned i=0;i<4;++i)assert(a[i].wake==b[i].wake&&a[i].sleep==b[i].sleep&&a[i].wake_valid==b[i].wake_valid&&a[i].sleep_valid==b[i].sleep_valid);
        Recorder ra={0},rb={0};ra.fail_at=rb.fail_at=test%9;
        EldenWcnPower pa={0},pb={0};pa.epoch=pb.epoch=7;pa.state=pb.state=EldenWcnPowerReady;
        pa.io.context=&ra;pb.io.context=&rb;pa.io.qmp=pb.io.qmp=record;
        uint64_t epoch=test%13==0?6:7;
        old=legacy_elden_wcn_power_cpr(&pa,epoch,params,count);now=elden_wcn_power_cpr(&pb,epoch,params,count);
        assert(old==now&&ra.calls==rb.calls&&!memcmp(ra.messages,rb.messages,sizeof(ra.messages)));
        assert(pa.state==pb.state&&pa.error==pb.error&&pa.quarantined==pb.quarantined&&pa.qmp_messages==pb.qmp_messages);
    }
    EldenWcnPmuParam all[4]={{"VDDD_AON_0P9",876,800,true,true},{"VDDD_WLMX_0P9",900,800,true,true},
                            {"VDDA_RFA_1P9",1800,1500,true,true},{"VDDA_PCIE_1P2",1312,1100,true,true}};
    for(unsigned failure=0;failure<=8;++failure){
        Recorder a={0},b={0};a.fail_at=b.fail_at=failure;
        EldenWcnPower pa={0},pb={0};pa.epoch=pb.epoch=7;pa.state=pb.state=EldenWcnPowerReady;
        pa.io.context=&a;pb.io.context=&b;pa.io.qmp=pb.io.qmp=record;
        int old=legacy_elden_wcn_power_cpr(&pa,7,all,4),now=elden_wcn_power_cpr(&pb,7,all,4);
        assert(old==now&&now==(failure?-110:0)&&a.calls==(failure?failure:8)&&a.calls==b.calls);
        assert(!memcmp(a.messages,b.messages,sizeof(a.messages))&&pa.state==pb.state&&pa.quarantined==pb.quarantined);
        assert(pb.qmp_messages==(failure?failure-1:8)&&pb.quarantined==(failure!=0));
    }
    EldenWcnPmuParam p={"VDDD_AON_0P9",876,876,true,true};EldenWcnPdcVote votes[4];EldenWcnCprCounts counts={99,99,99};
    assert(!elden_wcn_cpr_plan_details(&p,1,votes,&counts)&&counts.requested==1&&counts.mapped==1&&counts.unknown==0);
    p.wake=UINT32_MAX;assert(elden_wcn_cpr_plan_details(&p,1,votes,&counts)<0&&counts.requested==1);
    puts("4096 legacy/new comparisons plus all8 command-failure positions passed; command bytes, errors, epochs and quarantine preserved");
}
