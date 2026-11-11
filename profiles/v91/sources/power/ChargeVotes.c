/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargeVotes.h"
#include <string.h>
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static int stop(EldenChargeVotes *s){s->failed=true;s->pending=s->ready=false;++s->errors;return -1;}
void elden_charge_votes_init(EldenChargeVotes *s){memset(s,0,sizeof(*s));}
void elden_charge_votes_link(EldenChargeVotes *s,bool up){
    if(s->up==up)return;
    s->up=up;s->pending=s->failed=s->ready=false;s->index=s->tables=s->voters=0;
    if(up)++s->epoch;
}
void elden_charge_votes_refresh(EldenChargeVotes *s){if(s->up&&s->ready&&!s->failed){s->index=0;s->ready=false;}}
int elden_charge_votes_next(EldenChargeVotes *s,uint8_t out[16]){
    if(!s->up||s->pending||s->failed||s->ready||!out)return 0;
    memset(out,0,16);put(out,ELDEN_VOTE_OWNER);put(out+4,1);put(out+8,s->tables?0x17:0x15);
    if(s->tables)put(out+12,s->index);
    s->pending=true;return 16;
}
void elden_charge_votes_unsent(EldenChargeVotes *s){s->pending=false;}
void elden_charge_votes_timeout(EldenChargeVotes *s){if(s->pending){++s->timeouts;stop(s);}}
static bool name(char out[13],const uint8_t *p){
    unsigned i=0;for(;i<12&&p[i];++i){if(p[i]<32||p[i]>126)return false;out[i]=(char)p[i];}
    if(!i||i==12)return false;
    out[i]=0;return true;
}
int elden_charge_votes_receive(EldenChargeVotes *s,const uint8_t *p,size_t n){
    if(!s->up||s->failed||!s->pending||!p||n<12||get(p)!=ELDEN_VOTE_OWNER||
       get(p+4)!=1||get(p+8)!=(s->tables?0x17u:0x15u))return 0;
    if(!s->tables){
        if(n!=548)return stop(s);
        uint32_t tables=get(p+12),voters=get(p+16);
        if(!tables||tables>12||!voters||voters>32)return stop(s);
        for(unsigned i=0;i<tables;++i)if(!name(s->names[i],p+20+12*i))return stop(s);
        for(unsigned i=0;i<voters;++i)if(!name(s->clients[i],p+164+12*i))return stop(s);
        s->tables=tables;s->voters=voters;s->index=0;s->pending=false;return 1;
    }
    if(n!=192||get(p+12)!=s->index||s->index>=s->tables)return stop(s);
    EldenVote v={0};v.effective=get(p+16);v.mask=get(p+180);v.winner=get(p+184);v.override_id=get(p+188);
    if((s->voters<32&&(v.mask>>s->voters))||(v.mask&&v.winner>=s->voters))return stop(s);
    for(unsigned i=0;i<32;++i)v.values[i]=get(p+52+4*i);
    s->votes[s->index]=v;s->pending=false;
    if(++s->index==s->tables){s->ready=true;++s->cycle;return 2;}
    return 1;
}
