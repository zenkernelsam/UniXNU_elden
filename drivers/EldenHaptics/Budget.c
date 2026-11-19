/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Budget.h"
static int clock_step(EldenSharedBudget *b,uint64_t now){
    if(!b||!b->initialized)return -22;
    if(b->clock_failed)return -110;
    if(b->has_clock&&now<b->clock_us){b->clock_failed=1;return -110;}
    b->has_clock=1;b->clock_us=now;return 0;
}
int elden_shared_budget_init(EldenSharedBudget *b,const EldenSharedBudgetLimits *l){
    if(!b||b->initialized||!l||!l->mask||l->mask>3||!l->window_us||l->window_us>10000000||l->window_us%125||
       !l->max_dual_on_us||l->max_dual_on_us>l->window_us)return -22;
    for(unsigned m=0;m<2;m++)if(l->mask&(1u<<m)){
        uint64_t frames=(uint64_t)l->window_us*24000/1000000;
        if(!l->max_on_us[m]||l->max_on_us[m]>l->window_us||!l->max_squares[m]||l->max_squares[m]>UINT64_C(16384)*16129*frames)return -22;
    }
    EldenSharedBudget value={0};value.limits=*l;value.initialized=1;*b=value;return 0;
}
int elden_shared_budget_reserve(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,uint32_t lease,
    const uint64_t squares[2],const uint32_t on[2],uint32_t dual){
    if(!b||!squares||!on||!owner||!epoch||!id||!lease||lease>65000000)return -22;
    int rc=clock_step(b,now);if(rc)return rc;
    if(now>UINT64_MAX-lease-b->limits.window_us)return -22;
    uint64_t used_squares[2]={0};uint32_t used_on[2]={0},used_dual=0;int empty=-1;
    for(unsigned i=0;i<EldenSharedBudgetSlots;i++){
        EldenSharedBudgetEntry *e=&b->slots[i];
        if(e->used&&!e->active&&!e->held&&now>=e->expires_us)e->used=0;
        if(!e->used){if(empty<0)empty=(int)i;continue;}
        if(e->owner==owner&&e->epoch==epoch&&e->id==id)return -116;
        for(unsigned m=0;m<2;m++){used_squares[m]+=e->squares[m];used_on[m]+=e->on_us[m];}
        used_dual+=e->dual_us;
    }
    if(empty<0)return -16;
    for(unsigned m=0;m<2;m++){
        if(!(b->limits.mask&(1u<<m))&&(squares[m]||on[m]))return -13;
        if(on[m]>b->limits.max_on_us[m]||used_on[m]>b->limits.max_on_us[m]-on[m]||
           squares[m]>b->limits.max_squares[m]||used_squares[m]>b->limits.max_squares[m]-squares[m])return -13;
    }
    if(dual>b->limits.max_dual_on_us||used_dual>b->limits.max_dual_on_us-dual)return -13;
    uint32_t overlap=on[0]<on[1]?on[0]:on[1];
    if(dual<overlap||dual>on[0]||dual>on[1]||(squares[0]&&!on[0])||(squares[1]&&!on[1]))return -22;
    EldenSharedBudgetEntry entry={0};entry.owner=owner;entry.epoch=epoch;entry.id=id;entry.expires_us=now+lease+b->limits.window_us;
    for(unsigned m=0;m<2;m++){entry.squares[m]=squares[m];entry.on_us[m]=on[m];}
    entry.dual_us=dual;entry.used=entry.active=1;b->slots[empty]=entry;return 0;
}
int elden_shared_budget_ram(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,uint32_t duration,const uint8_t gain[2]){
    if(!b||!b->initialized||!gain||!duration||duration>250000||gain[0]>128||gain[1]>128)return -22;
    uint32_t peak=duration<b->limits.window_us?duration:b->limits.window_us;
    uint32_t frames=(uint32_t)(((uint64_t)peak*24000+999999)/1000000);uint64_t squares[2]={0};uint32_t on[2]={0};
    // RAM waveform magnitude is not inferred from firmware bytes: reserve
    // worst signed8 peak127 for the whole admitted motor-on interval.
    for(unsigned m=0;m<2;m++)if(gain[m]){squares[m]=(uint64_t)gain[m]*gain[m]*16129*frames;on[m]=peak;}
    return elden_shared_budget_reserve(b,owner,epoch,id,now,duration+250000,squares,on,gain[0]&&gain[1]?peak:0);
}
static int finish(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,int confirmed,int actual){
    if(!b||!b->initialized||!owner||!epoch||!id||(confirmed!=0&&confirmed!=1))return -22;
    int clock=clock_step(b,now);
    for(unsigned i=0;i<EldenSharedBudgetSlots;i++){
        EldenSharedBudgetEntry *e=&b->slots[i];
        if(!e->used||e->owner!=owner||e->epoch!=epoch||e->id!=id)continue;
        if(!confirmed||clock){e->held=1;return clock?clock:-125;}
        if(e->held)return -125;
        e->active=0;
        if(now>UINT64_MAX-b->limits.window_us){e->held=1;return -22;}
        uint64_t after=now+b->limits.window_us;if(actual||after>e->expires_us)e->expires_us=after;return 0;
    }
    return clock?clock:-116; // no physical start/reservation for a rejected request
}
int elden_shared_budget_finish(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,int confirmed){
    return finish(b,owner,epoch,id,now,confirmed,0);
}
int elden_shared_budget_finish_actual(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,int confirmed){
    return finish(b,owner,epoch,id,now,confirmed,1);
}
int elden_shared_budget_settle_actual(EldenSharedBudget *b,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now,
    int confirmed,const uint64_t square[2],const uint32_t on[2]){
    if(!b||!b->initialized||!owner||!epoch||!id||(confirmed!=0&&confirmed!=1))return -22;
    if(!confirmed)return finish(b,owner,epoch,id,now,0,1);
    if(!square||!on)return -22;
    int rc=clock_step(b,now);if(rc)return finish(b,owner,epoch,id,now,0,1);
    for(unsigned i=0;i<EldenSharedBudgetSlots;i++){
        EldenSharedBudgetEntry *e=&b->slots[i];
        if(!e->used||e->owner!=owner||e->epoch!=epoch||e->id!=id)continue;
        if(!e->active||e->held)return -125;
        for(unsigned m=0;m<2;m++)if(square[m]>e->squares[m]||on[m]>e->on_us[m]){e->held=1;return -13;}
        for(unsigned m=0;m<2;m++){e->squares[m]=square[m];e->on_us[m]=on[m];}
        e->dual_us=on[0]<on[1]?on[0]:on[1];
        return finish(b,owner,epoch,id,now,1,1);
    }
    return -116;
}
int elden_shared_budget_active(const EldenSharedBudget *b){
    if(!b||!b->initialized)return 0;
    for(unsigned i=0;i<EldenSharedBudgetSlots;i++)if(b->slots[i].used&&(b->slots[i].active||b->slots[i].held))return 1;
    return 0;
}
