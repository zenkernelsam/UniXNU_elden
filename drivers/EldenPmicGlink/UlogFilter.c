/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UlogFilter.h"
#include <string.h>
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
void elden_log_filter_init(EldenLogFilter *s){memset(s,0,sizeof(*s));}
void elden_log_filter_fail(EldenLogFilter *s,unsigned e){s->phase=EldenLogFilterFailed;s->error=e;s->pending=false;}
void elden_log_filter_link(EldenLogFilter *s,bool up,uint32_t epoch){
    if(s->up==up&&s->epoch==epoch)return;
    if(s->phase!=EldenLogFilterOff&&s->phase!=EldenLogFilterDone)elden_log_filter_fail(s,1);
    s->up=up;s->epoch=epoch;
}
int elden_log_filter_next(EldenLogFilter *s,bool start,bool stop,uint8_t out[32]){
    if(!s->up||!s->epoch||s->pending||!out)return 0;
    if(s->phase==EldenLogFilterOff){
        if(stop){s->phase=EldenLogFilterDone;s->restored=true;return 0;}
        if(!start)return 0;
        s->phase=EldenLogFilterSet;
    }
    if(s->phase==EldenLogFilterActive&&stop)s->phase=EldenLogFilterRestore;
    if(s->phase==EldenLogFilterSet&&!s->touched&&(stop||!start)){
        if(stop){s->phase=EldenLogFilterDone;s->restored=true;}
        return 0;
    }
    if(s->phase!=EldenLogFilterSet&&s->phase!=EldenLogFilterRestore)return 0;
    /* Exact ARM64 stock ABI: 4-byte padding after header, u64 at16, level24.
     * Only diagnostic categories USB charger(5), DPM(25), ICM(34).
     * Cold firmware baseline: override mask0, level4; no caller-supplied value. */
    memset(out,0,32);put(out,32778);put(out+4,1);put(out+8,0x19);
    if(s->phase==EldenLogFilterSet){put(out+16,0x02000020);put(out+20,4);put(out+24,3);}
    else put(out+24,4);
    s->pending=true;s->touched=true;++s->requests;return 32;
}
void elden_log_filter_unsent(EldenLogFilter *s){if(s->pending){s->pending=false;--s->requests;s->touched=s->requests!=0;}}
int elden_log_filter_receive(EldenLogFilter *s,uint32_t epoch,const uint8_t *p,size_t n){
    if(!s->up||!s->pending||s->epoch!=epoch||!p)return 0;
    if(n>=4&&get(p)!=32778)return 0;
    if(n>=12&&get(p+8)!=0x19)return 0;
    if(n!=16||get(p+4)!=1||get(p+12)){elden_log_filter_fail(s,2);return -1;}
    s->pending=false;++s->acks;
    if(s->phase==EldenLogFilterSet)s->phase=EldenLogFilterActive;
    else{s->phase=EldenLogFilterDone;s->restored=true;}
    return 1;
}
