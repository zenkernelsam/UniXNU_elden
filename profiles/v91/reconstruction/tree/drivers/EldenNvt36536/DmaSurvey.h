/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#include <string.h>
/* SMMUv2 global-register inventory only: no context-bank/table accesses,
 * no configuration callback and no DMA-admission decision. The caller owns
 * a read-only global mapping of at least 0x2000 bytes. */
typedef struct {
    unsigned sid,slot,bank,type;
    uint32_t smr,s2cr,cbar,cba2r;
    int context_valid;
} EldenTouchDmaRoute;
typedef struct {
    uint32_t cr0,id0,id1,id2;
    unsigned groups,matches[2];
} EldenTouchDmaSurvey;
/* Full global inventory after the targeted survey has succeeded. Invalid
 * SMRs and unused banks are still data, never free-resource authorization. */
static inline int elden_touch_dma_inventory(void *context,uint32_t (*read32)(void *,unsigned),
    const EldenTouchDmaSurvey *s,
    void (*slot)(void *,unsigned,uint32_t,uint32_t),
    void (*bank)(void *,unsigned,uint32_t,uint32_t)){
    if(!read32||!s||!slot||!bank||!s->groups||s->groups>255||
       s->groups!=(s->id0&255)||(s->id0&0x48000000u)!=0x48000000u||
       (s->cr0&8)||s->id1==UINT32_MAX)return -22;
    if(read32(context,0)!=s->cr0||read32(context,0x20)!=s->id0||
       read32(context,0x24)!=s->id1||read32(context,0x28)!=s->id2)return -5;
    for(unsigned i=0;i<s->groups;i++){
        uint32_t smr=read32(context,0x800+i*4),s2cr=read32(context,0xc00+i*4);
        slot(context,i,smr,s2cr);
    }
    for(unsigned i=0;i<(s->id1&255);i++){
        uint32_t cbar=read32(context,0x1000+i*4),cba2r=read32(context,0x1800+i*4);
        bank(context,i,cbar,cba2r);
    }
    return 0;
}
static inline int elden_touch_dma_survey(void *context,uint32_t (*read32)(void *,unsigned),
    void (*route)(void *,const EldenTouchDmaRoute *),unsigned se_sid,unsigned gpi_sid,
    EldenTouchDmaSurvey *out){
    if(!read32||!out||se_sid>0x7fff||gpi_sid>0x7fff)return -22;
    memset(out,0,sizeof(*out));
    out->cr0=read32(context,0);out->id0=read32(context,0x20);
    out->id1=read32(context,0x24);out->id2=read32(context,0x28);
    out->groups=out->id0&255;
    if(out->cr0==UINT32_MAX||out->id0==UINT32_MAX||out->id1==UINT32_MAX||out->id2==UINT32_MAX||
       !out->groups||(out->cr0&8)||(out->id0&0x48000000u)!=0x48000000u)return -95;
    const unsigned ids[]={se_sid,gpi_sid};
    for(unsigned n=0;n<2;n++)for(unsigned slot=0;slot<out->groups;slot++){
        uint32_t smr=read32(context,0x800+slot*4);unsigned mask=(smr>>16)&0x7fff;
        if(!(smr&0x80000000u)||((smr^ids[n])&0xffffu&~mask))continue;
        EldenTouchDmaRoute r={0};r.sid=ids[n];r.slot=slot;r.smr=smr;
        r.s2cr=read32(context,0xc00+slot*4);r.bank=r.s2cr&255;r.type=(r.s2cr>>16)&3;
        if(r.type==0&&r.bank<(out->id1&255)){
            r.cbar=read32(context,0x1000+r.bank*4);r.cba2r=read32(context,0x1800+r.bank*4);r.context_valid=1;
        }
        ++out->matches[n];if(route)route(context,&r);
    }
    return 0;
}
