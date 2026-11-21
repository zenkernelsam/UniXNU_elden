/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Catalog.h"
static int digest(const char *p){
    if(!p)return 0;for(unsigned i=0;i<64;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
    return !p[64];
}
static int name(const char *p){
    if(!p)return 0;for(unsigned i=0;i<128;i++){if(!p[i])return i>0;if((uint8_t)p[i]<32||p[i]=='/'||p[i]=='\\')return 0;}return 0;
}
int elden_haptics_catalog_check(const EldenHapticsEffect *e,size_t count,const EldenRtpLimits *l){
    if(!count)return 0;
    if(!e||!l||count>512||!l->mask||l->mask>3||!l->max_duration_us||l->max_duration_us>60000000||
       !l->window_frames||l->window_frames>240000)return -22;
    uint64_t bytes=0;
    for(size_t i=0;i<count;i++){
        if(!e[i].id||e[i].id&0x80000000u||(i&&e[i-1].id>=e[i].id)||!name(e[i].name)||!digest(e[i].source_sha256)||!digest(e[i].bank_sha256)||
           !e[i].frames||e[i].frames>1440000||!e[i].mask||e[i].mask&~l->mask||
           ((uint64_t)e[i].frames*1000000+23999)/24000>l->max_duration_us||
           (unsigned)e[i].gain[0]+e[i].gain[1]>l->max_sum_gain)return -22;
        for(unsigned m=0;m<2;m++){
            if(e[i].mask&(1u<<m)){
                if(!e[i].pcm[m]||!e[i].gain[m]||e[i].gain[m]>l->max_gain[m])return -22;
                bytes+=e[i].frames;
            }else if(e[i].pcm[m]||e[i].gain[m])return -22;
        }
        if(bytes>32u*1024*1024)return -22; // retained boot catalog/storage bound
    }
    return 0;
}
const EldenHapticsEffect *elden_haptics_catalog_find(const EldenHapticsEffect *e,size_t count,uint32_t id){
    if(!e||count>512||!id)return 0;
    size_t lo=0,hi=count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(e[mid].id<id)lo=mid+1;else hi=mid;}
    return lo<count&&e[lo].id==id?e+lo:0;
}
