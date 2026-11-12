/* SPDX-License-Identifier: GPL-2.0-only */
#include "OlCprPlan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Extracted unchanged arithmetic body from the pinned Linux patch. */
#include "LinuxOlCprReference.h"
static void param(EldenOlCprParam *p,const char *pin,uint32_t wake,uint32_t sleep){
    memset(p,0,sizeof(*p));assert(strlen(pin)<sizeof(p->pin));strcpy(p->pin,pin);
    p->wake_valid=p->sleep_valid=1;p->wake=wake;p->sleep=sleep;
}
int main(void){
    unsigned cases=0;uint32_t value=0xfeedbeef;
    for(uint32_t raw=0;raw<10000;++raw)for(uint32_t drop=10;drop<=30;drop+=20){
        uint32_t a=0xfeedbeef,b=0xfeedbeef;int x=elden_olcpr_adjust(raw,drop,&a),y=pwrseq_qcom_wcn_adjust_voltage(raw,drop,&b);
        assert(x==y);if(!x)assert(a==b);else assert(a==0xfeedbeef);++cases;
    }
    for(uint32_t delta=0;delta<64;++delta)for(uint32_t drop=10;drop<=30;drop+=20){
        uint32_t a=0xfeedbeef,b=0xfeedbeef;int x=elden_olcpr_adjust(UINT32_MAX-delta,drop,&a),y=pwrseq_qcom_wcn_adjust_voltage(UINT32_MAX-delta,drop,&b);
        assert(x==y);if(!x)assert(a==b);else assert(a==0xfeedbeef);++cases;
    }
    assert(elden_olcpr_adjust(100,30,0)==EldenOlCprInvalid);
    assert(elden_olcpr_adjust(876,30,&value)==0&&value==890);
    assert(elden_olcpr_adjust(876,10,&value)==0&&value==870);
    EldenOlCprParam p[16];EldenOlCprPlan out,unchanged;
    memset(&out,0xa5,sizeof(out));unchanged=out;
    assert(elden_olcpr_prepare(p,0,&out)==EldenOlCprInvalid&&!memcmp(&out,&unchanged,sizeof(out)));
    assert(elden_olcpr_prepare(p,17,&out)==EldenOlCprInvalid);
    param(&p[0],"VDDD_AON_0P9",800,700);param(&p[1],"VDDD_WLCX_0P9",876,680);
    param(&p[2],"VDDA_RFA_1P9",1800,1600);param(&p[3],"VDDA_PCIE_1P2",1200,1000);
    param(&p[4],"VDDA_RFA_1P3",1300,1100);param(&p[5],"VDDD_WLMX_0P9",850,800);
    assert(elden_olcpr_prepare(p,6,&out)==0&&out.requested==6&&out.mapped==6&&out.commands==8);
    assert(out.rail[EldenOlCprS5f].wake==890&&out.rail[EldenOlCprS5f].sleep==694);
    assert(out.rail[EldenOlCprS7i].wake==1314&&out.rail[EldenOlCprS7i].sleep==1094);
    assert(out.rail[EldenOlCprS4d].wake==866&&out.rail[EldenOlCprS3g].wake==1814);
    EldenOlCprPlan base=out;
    for(unsigned i=0;i<3;++i){EldenOlCprParam swap=p[i];p[i]=p[5-i];p[5-i]=swap;}
    assert(elden_olcpr_prepare(p,6,&out)==0&&!memcmp(&out,&base,sizeof(out)));
    param(&p[6],"unknown",UINT32_MAX,0);
    assert(elden_olcpr_prepare(p,7,&out)==0&&out.unknown==1&&out.mapped==6);
    unchanged=out;assert(elden_olcpr_prepare(&p[6],1,&out)==EldenOlCprNoData&&!memcmp(&out,&unchanged,sizeof(out)));
    memset(p[6].pin,'X',32);assert(elden_olcpr_prepare(&p[6],1,&out)==EldenOlCprNoData);
    p[6].pin[0]=0;assert(elden_olcpr_prepare(p,7,&out)==EldenOlCprInvalid&&!memcmp(&out,&unchanged,sizeof(out)));
    param(&p[6],"VDDD_BTCMX_0P9",UINT32_MAX,700);
    assert(elden_olcpr_prepare(p,7,&out)==EldenOlCprRange&&!memcmp(&out,&unchanged,sizeof(out)));
    p[6].wake_valid=p[6].sleep_valid=0;assert(elden_olcpr_prepare(&p[6],1,&out)==0&&out.commands==0);
    const char *pins[]={"VDDD_AON_0P9","VDDA_RFA_1P9","VDDA_RFA_1P3","VDDA_RFA_0P9","VDDD_WLMX_0P9","VDDD_WLCX_0P9","VDDD_BTCX_0P9","VDDD_BTCMX_0P9","VDDA_PCIE_1P2","VDDA_PCIE_0P9"};
    unsigned rails[]={0,2,3,0,1,0,0,0,3,3};
    for(unsigned i=0;i<10;++i){param(p,pins[i],100,100);assert(elden_olcpr_prepare(p,1,&out)==0&&out.rail[rails[i]].wake==114);}
    assert(!strcmp(elden_olcpr_rail_name(0),"s5f")&&!strcmp(elden_olcpr_pdc_name(1),"bb"));
    assert(!elden_olcpr_rail_name(4)&&!elden_olcpr_pdc_name(UINT32_MAX));
    printf("OL-CPR: %u Linux arithmetic comparisons; aliases, aggregation, overflow, unknown/empty pins and transactional failures passed\n",cases);
}
