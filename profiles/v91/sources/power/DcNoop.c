/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcNoop.h"
#include <string.h>
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static bool admitted(const EldenPowerPathRead *s){
    const uint8_t *v=s->values;
    if(!s->cycles||s->failed||v[0]!=0xdb||v[18]!=0xdb||v[1]!=1||v[2]!=0||v[3]!=10)return false;
    for(unsigned i=4;i<=11;i+=7)
        if(v[i]!=0x10||v[i+1]!=0x12||v[i+2]!=1||v[i+3]!=1||v[i+4]!=0||v[i+5]!=0x80||v[i+6]!=0x80)return false;
    return true;
}
void elden_dc_noop_init(EldenDcNoop *s){memset(s,0,sizeof(*s));}
void elden_dc_noop_fail(EldenDcNoop *s,unsigned reason){s->phase=EldenDcNoopFailed;s->error=reason;s->pending=false;}
void elden_dc_noop_cancel(EldenDcNoop *s){
    /* Drain an already queued write and its readback; never start a write
     * after cancellation. The owner calls again after an in-flight read. */
    if(!s->pending&&s->phase!=EldenDcNoopVerify&&s->phase!=EldenDcNoopDone&&s->phase!=EldenDcNoopFailed)
        elden_dc_noop_fail(s,EldenDcNoopStopped);
}
void elden_dc_noop_link(EldenDcNoop *s,bool up,uint32_t epoch){
    if(s->up==up&&s->epoch==epoch)return;
    s->up=up;s->epoch=epoch;
    if(s->phase!=EldenDcNoopOff)elden_dc_noop_fail(s,EldenDcNoopStopped);
}
int elden_dc_noop_begin(EldenDcNoop *s){
    if(!s->up||s->phase!=EldenDcNoopOff||s->write_attempted)return 0;
    elden_power_reg_init(&s->snapshot);elden_power_reg_extend(&s->snapshot);
    elden_power_reg_link(&s->snapshot,true,s->epoch);elden_power_reg_begin(&s->snapshot);
    s->phase=EldenDcNoopProbe;return 1;
}
int elden_dc_noop_next(EldenDcNoop *s,bool qualified,uint8_t out[28]){
    if(!s->up||s->pending||!out)return 0;
    if(s->phase==EldenDcNoopProbe||s->phase==EldenDcNoopVerify){
        int n=elden_power_reg_next(&s->snapshot,out);if(n)s->pending=true;return n;
    }
    if(s->phase!=EldenDcNoopWrite)return 0;
    if(!qualified||!admitted(&s->snapshot)||s->write_attempted){elden_dc_noop_fail(s,EldenDcNoopUnsafe);return 0;}
    /* Exact stock WRITE ABI: 28B, bus0/SID7/address/data. There is no caller
     * supplied address/value, no GPIO target and no current-increase value. */
    put(out,32783);put(out+4,1);put(out+8,0x38);put(out+12,0);put(out+16,7);put(out+20,0x2a52);put(out+24,10);
    s->pending=true;s->write_attempted=true;return 28;
}
void elden_dc_noop_unsent(EldenDcNoop *s){
    if(!s->pending)return;
    s->pending=false;
    if(s->phase==EldenDcNoopWrite)s->write_attempted=false;
    else elden_power_reg_unsent(&s->snapshot);
}
int elden_dc_noop_receive(EldenDcNoop *s,const uint8_t *p,size_t bytes){
    if(!s->up||!s->pending||!p)return 0;
    if(bytes>=4&&get(p)!=32783)return 0;
    if(s->phase==EldenDcNoopWrite){
        if(bytes>=12&&get(p+8)!=0x38)return 0;
        if(bytes!=16||get(p+4)!=1||get(p+12)){elden_dc_noop_fail(s,EldenDcNoopProtocol);return -1;}
        s->pending=false;++s->write_acks;s->phase=EldenDcNoopVerify;
        elden_power_reg_begin(&s->snapshot);return 1;
    }
    if(bytes>=12&&get(p+8)!=0x37)return 0;
    int result=elden_power_reg_receive(&s->snapshot,p,bytes);
    if(result<0){elden_dc_noop_fail(s,EldenDcNoopProtocol);return -1;}
    if(result)s->pending=false;
    if(result==2){
        if(!admitted(&s->snapshot)){elden_dc_noop_fail(s,EldenDcNoopReadback);return -1;}
        if(s->phase==EldenDcNoopProbe){s->phase=EldenDcNoopWrite;return 2;}
        s->phase=EldenDcNoopDone;s->verified=true;return 3;
    }
    return result;
}
