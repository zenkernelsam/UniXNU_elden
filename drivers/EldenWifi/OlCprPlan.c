/* SPDX-License-Identifier: GPL-2.0-only */
/* Based on pwrseq-qcom-wcn.c in h0cheung/tb322fc-linux-kernel,
 * dc7d14dc64c8d4f86220b30cd126b17d805c8474, and the TB322FC DTS
 * at6f97250ead155fc3b980856323e99a4623dae73d. Keep computation separate
 * from the future shared-owner executor; a valid plan is not an applied vote.
 */
#include "OlCprPlan.h"
static const struct {const char *pin;unsigned rail;} aliases[]={
    {"VDDD_AON_0P9",EldenOlCprS5f},{"VDDA_RFA_1P9",EldenOlCprS3g},
    {"VDDA_RFA_1P3",EldenOlCprS7i},{"VDDA_RFA_0P9",EldenOlCprS5f},
    {"VDDD_WLMX_0P9",EldenOlCprS4d},{"VDDD_WLCX_0P9",EldenOlCprS5f},
    {"VDDD_BTCX_0P9",EldenOlCprS5f},{"VDDD_BTCMX_0P9",EldenOlCprS5f},
    {"VDDA_PCIE_1P2",EldenOlCprS7i},{"VDDA_PCIE_0P9",EldenOlCprS7i},
};
static const char *const rails[]={"s5f","s4d","s3g","s7i"};
static const char *const pdcs[]={"rf","bb","rf","rf"};
const char *elden_olcpr_rail_name(unsigned i){return i<ELDEN_OLCPR_RAILS?rails[i]:0;}
const char *elden_olcpr_pdc_name(unsigned i){return i<ELDEN_OLCPR_RAILS?pdcs[i]:0;}
static int matches(const char *pin,const char *name){
    for(unsigned i=0;i<ELDEN_OLCPR_NAME_BYTES;++i){
        if(pin[i]!=name[i])return 0;
        if(!name[i])return 1;
    }
    return 0;
}
int elden_olcpr_adjust(uint32_t voltage,uint32_t drop,uint32_t *result){
    if(!result)return EldenOlCprInvalid;
    if(voltage>UINT32_MAX-3u)return EldenOlCprRange;
    uint32_t rounded=(voltage+3u)&~3u;
    if(rounded<16u||rounded-16u>UINT32_MAX-drop)return EldenOlCprRange;
    *result=rounded-16u+drop;return EldenOlCprOk;
}
int elden_olcpr_prepare(const EldenOlCprParam *params,size_t count,EldenOlCprPlan *out){
    if(!params||!out||!count||count>ELDEN_OLCPR_MAX_PARAMS)return EldenOlCprInvalid;
    EldenOlCprPlan plan={0};
    for(size_t i=0;i<count;++i){
        const EldenOlCprParam *p=&params[i];
        if(!p->pin[0])return EldenOlCprInvalid;
        if(!p->wake_valid&&!p->sleep_valid)continue;
        ++plan.requested;
        unsigned rail=ELDEN_OLCPR_RAILS;
        for(unsigned a=0;a<sizeof(aliases)/sizeof(aliases[0]);++a)
            if(matches(p->pin,aliases[a].pin)){rail=aliases[a].rail;break;}
        if(rail==ELDEN_OLCPR_RAILS){++plan.unknown;continue;}
        EldenOlCprVote *v=&plan.rail[rail];uint32_t value;
        if(p->wake_valid){
            int rc=elden_olcpr_adjust(p->wake,30u,&value);if(rc)return rc;
            if(value>v->wake)v->wake=value;
            v->wake_valid=1;
        }
        if(p->sleep_valid){
            int rc=elden_olcpr_adjust(p->sleep,10u,&value);if(rc)return rc;
            if(value>v->sleep)v->sleep=value;
            v->sleep_valid=1;
        }
        ++plan.mapped;
    }
    if(plan.requested&&!plan.mapped)return EldenOlCprNoData;
    for(unsigned i=0;i<ELDEN_OLCPR_RAILS;++i)
        plan.commands+=plan.rail[i].wake_valid+plan.rail[i].sleep_valid;
    *out=plan;return EldenOlCprOk;
}
