/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargePolicy.h"
#include <string.h>
#define OWNER 32778u
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
bool elden_charge_window_valid(uint32_t v){
    unsigned enabled=v/100000,low=v/1000%100,high=v%1000;
    return enabled<=1&&high<=100&&(enabled?low<high:low<=high);
}
void elden_charge_policy_init(EldenChargePolicy *s){memset(s,0,sizeof(*s));}
void elden_charge_policy_link(EldenChargePolicy *s,bool up){
    if(s->up==up)return;
    s->up=up;s->pending=s->firmware_held=false;s->observed_cycle=s->verify_after=0;
    s->phase=up?EldenPolicyReadWindow:EldenPolicyOff;
    if(up){++s->epoch;if(s->requested_mode)++s->request_sequence;}
}
int elden_charge_policy_select(EldenChargePolicy *s,unsigned mode){
    if(mode<EldenChargeStock||mode>EldenChargeWindow4060)return -1;
    if(s->phase==EldenPolicyFailed)return -2;
    s->requested_mode=mode;++s->request_sequence;return 0;
}
static int fail(EldenChargePolicy *s,uint32_t error){
    s->firmware_error=error;++s->errors;s->pending=false;s->phase=EldenPolicyFailed;return -1;
}
static void done(EldenChargePolicy *s){
    s->applied_mode=s->active_mode;s->applied_sequence=s->active_sequence;s->phase=EldenPolicyReady;
}
static uint32_t permitted_enable(EldenChargePolicy *s){
    if(s->active_mode==EldenChargeBypass)return 0;
    unsigned low=100,high=100;bool limited=false;
    uint32_t windows[2]={s->target_window,s->eu_window};
    for(unsigned i=0;i<2;++i)if(windows[i]/100000==1){
        unsigned l=windows[i]/1000%100,h=windows[i]%1000;
        if(l<low)low=l;
        if(h<high)high=h;
        limited=true;
    }
    if(!limited)return 1;
    if(s->observed_soc>=high*100)return 0;
    if(s->observed_soc<=low*100)return 1;
    return s->observed_enable; /* retain firmware hysteresis inside the band */
}
void elden_charge_policy_observe(EldenChargePolicy *s,uint32_t cycle,uint32_t soc,uint32_t enabled){
    if(!s->up||soc>10000||enabled>1||s->phase==EldenPolicyFailed)return;
    s->observed_cycle=cycle;s->observed_soc=soc;s->observed_enable=enabled;
    if(cycle<s->verify_after)return;
    if(s->phase==EldenPolicyWaitWindow){
        s->target_enable=permitted_enable(s);
        if(s->target_enable==enabled)done(s);else s->phase=EldenPolicySetEnable;
    }else if(s->phase==EldenPolicyWaitEnable){
        if(enabled!=s->target_enable){
            if(!s->target_enable){fail(s,UINT32_MAX-1);return;}
            s->firmware_held=true; /* other firmware protection may inhibit charge */
        }
        done(s);
    }
}
int elden_charge_policy_next(EldenChargePolicy *s,bool allow_change,uint8_t out[24]){
    if(!s->up||s->pending||!out||s->phase==EldenPolicyFailed)return 0;
    if(s->phase==EldenPolicyReady){
        if(!allow_change||!s->observed_cycle||s->request_sequence==s->applied_sequence)return 0;
        s->active_mode=s->requested_mode;s->active_sequence=s->request_sequence;s->firmware_held=false;
        if(s->active_mode==EldenChargeBypass){s->target_enable=0;s->phase=EldenPolicySetEnable;}
        else {
            s->target_window=s->active_mode==EldenChargeLimit80?176080u:
                s->active_mode==EldenChargeWindow4060?140060u:s->original_window;
            s->phase=EldenPolicySetWindow;
        }
    }
    uint32_t op=0x60,prop=29,value=0;
    switch(s->phase){
    case EldenPolicyReadWindow:break;
    case EldenPolicyReadEu:prop=30;break;
    case EldenPolicySetWindow:if(!allow_change)return 0;op=0x61;value=s->target_window;break;
    case EldenPolicyVerifyWindow:break;
    case EldenPolicySetEnable:if(!allow_change)return 0;op=0x61;prop=28;value=s->target_enable;break;
    default:return 0;
    }
    if(op==0x61&&((prop==28&&value>1)||(prop==29&&!elden_charge_window_valid(value))))return fail(s,UINT32_MAX-2);
    memset(out,0,24);put(out,OWNER);put(out+4,1);put(out+8,op);put(out+16,prop);put(out+20,value);
    s->last_opcode=op;s->last_property=prop;s->last_value=value;s->pending=true;return 24;
}
void elden_charge_policy_unsent(EldenChargePolicy *s){s->pending=false;}
void elden_charge_policy_timeout(EldenChargePolicy *s){if(s->pending){++s->timeouts;fail(s,UINT32_MAX);}}
int elden_charge_policy_receive(EldenChargePolicy *s,const uint8_t *p,size_t n){
    if(!s->up||s->phase==EldenPolicyFailed||!s->pending||!p||n<16||get(p)!=OWNER||get(p+4)!=1||
        get(p+8)!=s->last_opcode)return 0;
    /* The OEM SET handler validates length/status without requiring a echoed
     * property. This is the only SET0x61 executor; real readback verifies the
     * change. GETs still require the exact property to coexist with BMS. */
    if(s->last_opcode==0x60&&get(p+12)!=s->last_property)return 0;
    if(n!=24)return fail(s,UINT32_MAX-3);
    if(get(p+20))return fail(s,get(p+20));
    s->pending=false;
    switch(s->phase){
    case EldenPolicyReadWindow:
        s->window=get(p+16);if(!elden_charge_window_valid(s->window))return fail(s,UINT32_MAX-4);
        if(!s->original_saved)s->original_window=s->window;
        s->phase=EldenPolicyReadEu;break;
    case EldenPolicyReadEu:
        s->eu_window=get(p+16);if(!elden_charge_window_valid(s->eu_window))return fail(s,UINT32_MAX-4);
        s->original_saved=true;s->phase=EldenPolicyReady;break;
    case EldenPolicySetWindow:s->phase=EldenPolicyVerifyWindow;break;
    case EldenPolicyVerifyWindow:
        if(get(p+16)!=s->target_window)return fail(s,UINT32_MAX-5);
        s->window=get(p+16);s->verify_after=s->observed_cycle+2;s->phase=EldenPolicyWaitWindow;break;
    case EldenPolicySetEnable:
        s->verify_after=s->observed_cycle+2;s->phase=EldenPolicyWaitEnable;break;
    default:return fail(s,UINT32_MAX-6);
    }
    return 1;
}
