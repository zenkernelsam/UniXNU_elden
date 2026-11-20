/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BootRoute.h"
#include <stddef.h>
static unsigned state;
static const void *owner;
static EldenSmmuBootRoute intended,published;
static EldenSmmuBootRoute planned_set[EldenSmmuBootMaxRoutes],published_set[EldenSmmuBootMaxRoutes];
static uint64_t set_epoch;static unsigned set_count,set_mode,reservation_ready;

static int same(const EldenSmmuBootRoute *a,const EldenSmmuBootRoute *b){
    return a->slot==b->slot&&a->bank==b->bank&&a->smr==b->smr&&a->s2cr==b->s2cr&&a->cbar==b->cbar&&a->cba2r==b->cba2r;
}
static int valid(const EldenSmmuBootRoute *r){
    /* TB322FC has132 slots/83 banks. Preserve boot slots0..6, touch slot7 and
     * boot banks0..6 (especially inherited bypass CB0). Exact Wi-Fi SID only;
     * no masks, EXIDS, privilege override or other consumer is accepted. */
    return r&&r->slot>=8&&r->slot<132&&r->bank>=7&&r->bank<83&&
        r->smr==0x80001401u&&r->s2cr==r->bank&&r->cbar==0x0001f300u&&r->cba2r==1;
}
static int group(const EldenSmmuBootRoute *r){
    if(!r||r->slot<8||r->slot>=132||r->bank<7||r->bank>=83||r->s2cr!=r->bank||r->cbar!=0x1f300||r->cba2r!=1||
       (r->smr&0xffff0000u)!=0x80000000u)return -1;
    unsigned sid=r->smr&0xffffu;if(sid==0x1401)return 0;if(sid==0x1c00)return 1;
    unsigned session=sid&15u,base=sid&~15u;
    return session>=3&&session<=8&&(base==0x1000||base==0x1040||base==0x1060||base==0x1080)?2:-1;
}
static int valid_set(const EldenSmmuBootRoute *r,unsigned count){
    if(!r||!count||count>EldenSmmuBootMaxRoutes)return 0;unsigned sensor=0,session=0;
    for(unsigned i=0;i<count;i++){
        int g=group(&r[i]);if(g<0)return 0;
        if(g==2){unsigned s=r[i].smr&15u;if(session&&s!=session)return 0;session=s;sensor++;}
        for(unsigned j=0;j<i;j++){
            if(r[i].slot==r[j].slot||r[i].smr==r[j].smr)return 0;
            int prior=group(&r[j]);
            if(g==2&&prior==2){if(r[i].bank!=r[j].bank||r[i].cbar!=r[j].cbar||r[i].cba2r!=r[j].cba2r)return 0;}
            else if(r[i].bank==r[j].bank)return 0;
        }
    }
    return !sensor||sensor==4;
}
unsigned elden_smmu_boot_route_state(void){return __atomic_load_n(&state,__ATOMIC_ACQUIRE);}
int elden_smmu_boot_route_reserve(const void *provider,const EldenSmmuBootRoute *plan){
    if(!provider||!valid(plan))return -22;
    unsigned expected=EldenSmmuBootOpen;
    if(!__atomic_compare_exchange_n(&state,&expected,EldenSmmuBootPreparing,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    owner=provider;intended=*plan;
    __atomic_store_n(&reservation_ready,1u,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_routes_reserve(const void *provider,uint64_t epoch,const EldenSmmuBootRoute *plan,unsigned count){
    if(!provider||!epoch||!valid_set(plan,count))return -22;unsigned expected=EldenSmmuBootOpen;
    if(!__atomic_compare_exchange_n(&state,&expected,EldenSmmuBootPreparing,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    owner=provider;set_epoch=epoch;set_count=count;
    for(unsigned i=0;i<count;i++)planned_set[i]=plan[i];set_mode=1u;
    __atomic_store_n(&reservation_ready,1u,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_routes_commit(const void *provider,uint64_t epoch,const EldenSmmuBootRoute *actual,unsigned count){
    if(elden_smmu_boot_route_state()!=EldenSmmuBootPreparing||!__atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)||!set_mode)return -16;
    if(!provider||provider!=owner||epoch!=set_epoch)return -19;
    int accepted=count==set_count&&valid_set(actual,count);
    if(accepted)for(unsigned i=0;i<count;i++)accepted&=same(&planned_set[i],&actual[i]);
    if(!accepted){__atomic_store_n(&state,EldenSmmuBootPoisoned,__ATOMIC_RELEASE);return -71;}
    for(unsigned i=0;i<count;i++)published_set[i]=actual[i];__atomic_store_n(&state,EldenSmmuBootReady,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_routes_poison(const void *provider,uint64_t epoch){
    if(elden_smmu_boot_route_state()!=EldenSmmuBootPreparing||!__atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)||!set_mode)return -16;
    if(!provider||provider!=owner||epoch!=set_epoch)return -19;
    __atomic_store_n(&state,EldenSmmuBootPoisoned,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_routes_early(const void *provider,uint64_t epoch){
    unsigned current=elden_smmu_boot_route_state();
    if(current!=EldenSmmuBootPreparing&&current!=EldenSmmuBootReady)return -16;
    return __atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)&&set_mode&&provider&&provider==owner&&epoch==set_epoch?0:-16;
}
static uint32_t route_value(const EldenSmmuBootRoute *r,uint32_t offset,uint32_t value){
    if(offset==0x800+4*r->slot)return r->smr;if(offset==0xc00+4*r->slot)return r->s2cr;
    if(offset==0x1000+4*r->bank)return r->cbar;if(offset==0x1800+4*r->bank)return r->cba2r;return value;
}
int elden_smmu_boot_routes_peek(const void *provider,uint64_t epoch,uint32_t offset,uint32_t baseline,uint32_t *expected){
    if(!expected)return -22;unsigned current=elden_smmu_boot_route_state();
    if((current!=EldenSmmuBootReady&&current!=EldenSmmuBootSealedReady)||!__atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)||!set_mode||provider!=owner||!provider||epoch!=set_epoch)return -16;
    uint32_t value=baseline;for(unsigned i=0;i<set_count;i++)value=route_value(&published_set[i],offset,value);*expected=value;return 0;
}
int elden_smmu_boot_route_commit(const void *provider,const EldenSmmuBootRoute *observed){
    if(elden_smmu_boot_route_state()!=EldenSmmuBootPreparing||!__atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)||set_mode)return -16;
    /* Only the serialized early provider may commit/poison. Other threads use
     * seal/expected; they never read owner or these mutable records. */
    if(!provider||provider!=owner)return -19;
    if(!valid(observed)||!same(&intended,observed)){
        __atomic_store_n(&state,EldenSmmuBootPoisoned,__ATOMIC_RELEASE);return -71;
    }
    published=*observed;__atomic_store_n(&state,EldenSmmuBootReady,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_route_poison(const void *provider){
    if(elden_smmu_boot_route_state()!=EldenSmmuBootPreparing||!__atomic_load_n(&reservation_ready,__ATOMIC_ACQUIRE)||set_mode)return -16;
    if(!provider||provider!=owner)return -19;
    __atomic_store_n(&state,EldenSmmuBootPoisoned,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_boot_route_seal(void){
    for(unsigned i=0;i<2;i++){
        unsigned current=elden_smmu_boot_route_state(),next;
        if(current==EldenSmmuBootSealedEmpty||current==EldenSmmuBootSealedReady)return 0;
        if(current==EldenSmmuBootOpen)next=EldenSmmuBootSealedEmpty;
        else if(current==EldenSmmuBootReady)next=EldenSmmuBootSealedReady;
        else return -16;
        if(__atomic_compare_exchange_n(&state,&current,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
    }
    return -16;
}
int elden_smmu_boot_route_expected(uint32_t offset,uint32_t baseline,uint32_t *expected){
    if(!expected)return -22;
    unsigned current=elden_smmu_boot_route_state();
    if(current!=EldenSmmuBootSealedEmpty&&current!=EldenSmmuBootSealedReady)return -16;
    uint32_t value=baseline;
    if(current==EldenSmmuBootSealedReady){
        if(set_mode)for(unsigned i=0;i<set_count;i++)value=route_value(&published_set[i],offset,value);
        else value=route_value(&published,offset,value);
    }
    *expected=value;return 0;
}
