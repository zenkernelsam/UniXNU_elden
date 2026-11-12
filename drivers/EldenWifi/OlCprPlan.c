/* SPDX-License-Identifier: GPL-2.0-only */
/* Wire-format adapter to the existing board WCN power planner. The shared
 * WcnPower provider owns aliases, arithmetic and aggregation for both clients.
 * No QMP/RPMh call or hardware operation occurs in this adapter.
 */
#include "OlCprPlan.h"
#include "WcnPower.h"
#include <string.h>
const char *elden_olcpr_rail_name(unsigned i){return elden_wcn_cpr_rail_name(i);}
const char *elden_olcpr_pdc_name(unsigned i){return elden_wcn_cpr_pdc_name(i);}
int elden_olcpr_adjust(uint32_t voltage,uint32_t drop,uint32_t *result){
    if(!result)return EldenOlCprInvalid;
    return elden_wcn_cpr_adjust(voltage,drop,result)?EldenOlCprRange:EldenOlCprOk;
}
int elden_olcpr_prepare(const EldenOlCprParam *params,size_t count,EldenOlCprPlan *out){
    if(!params||!out||!count||count>ELDEN_OLCPR_MAX_PARAMS)return EldenOlCprInvalid;
    EldenWcnPmuParam converted[ELDEN_OLCPR_MAX_PARAMS]={0};
    for(size_t i=0;i<count;++i){
        if(!params[i].pin[0])return EldenOlCprInvalid;
        memcpy(converted[i].pin,params[i].pin,ELDEN_OLCPR_NAME_BYTES);
        converted[i].pin[ELDEN_OLCPR_NAME_BYTES]=0;
        converted[i].wake=params[i].wake;converted[i].sleep=params[i].sleep;
        converted[i].wake_valid=params[i].wake_valid!=0;
        converted[i].sleep_valid=params[i].sleep_valid!=0;
    }
    EldenWcnPdcVote votes[ELDEN_OLCPR_RAILS];EldenWcnCprCounts counts;
    int rc=elden_wcn_cpr_plan_details(converted,count,votes,&counts);
    if(rc)return rc==EldenWcnPowerIdentity?EldenOlCprNoData:EldenOlCprRange;
    EldenOlCprPlan plan={0};
    for(unsigned i=0;i<ELDEN_OLCPR_RAILS;++i){
        plan.rail[i]=(EldenOlCprVote){votes[i].wake_valid,votes[i].wake,votes[i].sleep_valid,votes[i].sleep};
        plan.commands+=votes[i].wake_valid+votes[i].sleep_valid;
    }
    plan.requested=counts.requested;plan.mapped=counts.mapped;plan.unknown=counts.unknown;
    *out=plan;return EldenOlCprOk;
}
