/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Control.h"
#include <string.h>
void elden_thermal_control_step(EldenThermalControl *s,const EldenThermalInput *in,EldenThermalAction *out){
    if(!out)return;
    memset(out,0,sizeof(*out));out->level[0]=out->level[1]=EldenThermalKeep;
    if(!s||!in)return;
    out->armed=s->armed;out->fault_latched=s->fault_latched[0]||s->fault_latched[1];
    bool fresh=in->sequence&&in->sequence>s->previous_sequence;
    bool consecutive=!s->previous_sequence||in->sequence==s->previous_sequence+1;
    if(fresh)s->previous_sequence=in->sequence;
    bool valid=fresh&&in->connector_valid[0]&&in->connector_valid[1]&&in->battery_valid&&
        in->connector_mc[0]>=-40960&&in->connector_mc[0]<=130048&&
        in->connector_mc[1]>=-40960&&in->connector_mc[1]<=130048&&
        in->battery_centi_c>=-4000&&in->battery_centi_c<=9000;
    if(!s->armed){
        s->readiness=valid?(consecutive?s->readiness+1:1):0;
        if(s->readiness<2){
            /* An independently measured absolute overheat can close its
             * input immediately; startup never reopens an input. */
            for(unsigned i=0;i<2;i++)if(fresh&&in->connector_valid[i]&&
                elden_usb_thermal_decision(i+1,true,in->connector_mc[i],false,0)==EldenThermalHigh)
                out->level[i]=EldenThermalHigh;
            return;
        }
        s->armed=true;out->armed=true;
    }
    if(!valid){
        s->fault_latched[0]=s->fault_latched[1]=true;s->cool[0]=s->cool[1]=0;
        out->level[0]=out->level[1]=EldenThermalHigh;out->fault_latched=true;return;
    }
    for(unsigned i=0;i<2;i++){
        int desired=elden_usb_thermal_decision(i+1,true,in->connector_mc[i],true,in->battery_centi_c);
        if(s->fault_latched[i]){
            if(desired==EldenThermalLow)s->cool[i]=consecutive?s->cool[i]+1:1;else s->cool[i]=0;
            if(s->cool[i]>=2)s->fault_latched[i]=false;
            else desired=EldenThermalHigh;
        }
        out->level[i]=desired;
    }
    out->fault_latched=s->fault_latched[0]||s->fault_latched[1];
}
