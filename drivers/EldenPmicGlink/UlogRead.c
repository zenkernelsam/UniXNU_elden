/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UlogRead.h"
#include <string.h>
static uint32_t get(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put(uint8_t *p, uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
uint32_t elden_ulog_crc(const uint8_t *p, size_t bytes) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<bytes;i++) {
        crc^=p[i];
        for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
void elden_ulog_init(EldenUlogRead *s) { memset(s,0,sizeof(*s)); }
void elden_ulog_enable_paging(EldenUlogRead *s) { if(!s->up&&!s->requests)s->paging=true; }
void elden_ulog_enable_policy_trace(EldenUlogRead *s) { if(!s->up&&!s->requests){s->paging=true;s->policy_trace=true;} }
void elden_ulog_link(EldenUlogRead *s, bool up, uint32_t epoch) {
    if(s->up==up && s->epoch==epoch)return;
    s->up=up;s->epoch=epoch;s->pending=s->failed=s->ready=false;
    s->error=s->opcode=s->last_opcode=s->crc=0;
    /* Keep the boot-wide request/completion caps across link changes. */
}
void elden_ulog_fail(EldenUlogRead *s, unsigned reason) {
    s->failed=true;s->pending=s->ready=false;s->error=reason;
}
int elden_ulog_next(EldenUlogRead *s, uint8_t request[16]) {
    if(!request||!s->up||s->pending||s->ready||s->failed)return 0;
    if(s->policy_trace){
        if(s->requests>=ELDEN_ULOG_MAX_PAGES)return 0;
        s->opcode=0x18u;
    }else if(s->paging){
        if(s->requests>=ELDEN_ULOG_MAX_PAGES||s->current_empty||s->current_pages>=ELDEN_ULOG_CURRENT_PAGES)return 0;
        s->opcode=(!s->init_empty&&s->init_pages<ELDEN_ULOG_INIT_PAGES)?0x23u:0x18u;
    }else{
        if(s->requests>=2||s->completed>=2)return 0;
        s->opcode=s->completed?0x18u:0x23u;
    }
    put(request,ELDEN_ULOG_OWNER);put(request+4,1);put(request+8,s->opcode);
    put(request+12,ELDEN_ULOG_PAYLOAD);s->pending=true;++s->requests;return 16;
}
void elden_ulog_unsent(EldenUlogRead *s) {
    if(s->pending){s->pending=false;--s->requests;}
}
int elden_ulog_receive(EldenUlogRead *s, uint32_t epoch,const uint8_t *p,size_t bytes) {
    if(!s->up||!s->pending||s->failed||epoch!=s->epoch||!p)return 0;
    if(bytes>=4&&get(p)!=ELDEN_ULOG_OWNER)return 0;
    if(bytes!=ELDEN_ULOG_REPLY){elden_ulog_fail(s,EldenUlogLength);return -1;}
    if(!s->paging&&get(p+8)==s->last_opcode)return 0;
    if(get(p+4)!=1||get(p+8)!=s->opcode){elden_ulog_fail(s,EldenUlogTuple);return -1;}
    size_t text_bytes=0;while(text_bytes<ELDEN_ULOG_PAYLOAD&&p[12+text_bytes])++text_bytes;
    if(s->paging){
        if(text_bytes==ELDEN_ULOG_PAYLOAD){elden_ulog_fail(s,EldenUlogTuple);return -1;}
        if(text_bytes&&s->last_opcode==s->opcode&&text_bytes==s->text_bytes&&
           !memcmp(s->payload,p+12,text_bytes)){
            elden_ulog_fail(s,EldenUlogDuplicate);return -1;
        }
        if(s->opcode==0x23u){++s->init_pages;s->init_empty=text_bytes==0;}
        else{++s->current_pages;s->current_empty=text_bytes==0;}
    }
    s->text_bytes=(uint32_t)text_bytes;
    memcpy(s->payload,p+12,ELDEN_ULOG_PAYLOAD);
    s->crc=elden_ulog_crc(s->payload,ELDEN_ULOG_PAYLOAD);
    s->last_opcode=s->opcode;s->pending=false;s->ready=true;++s->completed;return 1;
}
void elden_ulog_release(EldenUlogRead *s) { s->ready=false; }
