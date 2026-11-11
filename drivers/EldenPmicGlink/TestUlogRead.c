/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UlogRead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void start(EldenUlogRead *s,uint8_t req[16]){
    elden_ulog_init(s);elden_ulog_link(s,true,7);CHECK(elden_ulog_next(s,req)==16);
}
int main(void) {
    EldenUlogRead *s=malloc(sizeof(*s));uint8_t req[16];
    uint8_t *reply=malloc(ELDEN_ULOG_REPLY+1);CHECK(s&&reply);
    memset(reply,0,ELDEN_ULOG_REPLY+1);
    CHECK(elden_ulog_crc((const uint8_t *)"123456789",9)==0xcbf43926u);
    start(s,req);CHECK(get(req)==32778&&get(req+4)==1&&get(req+8)==0x23&&get(req+12)==8192);
    elden_ulog_unsent(s);CHECK(s->requests==0&&!s->pending);CHECK(elden_ulog_next(s,req)==16);
    memcpy(reply,req,12);for(unsigned i=0;i<8192;i++)reply[12+i]=(uint8_t)i;
    CHECK(!elden_ulog_receive(s,6,reply,ELDEN_ULOG_REPLY)&&s->pending);
    CHECK(elden_ulog_receive(s,7,reply,ELDEN_ULOG_REPLY)==1&&s->ready&&s->completed==1);
    CHECK(!memcmp(s->payload,reply+12,8192));CHECK(!elden_ulog_next(s,req));
    elden_ulog_release(s);CHECK(elden_ulog_next(s,req)==16&&get(req+8)==0x18);
    CHECK(!elden_ulog_receive(s,7,reply,ELDEN_ULOG_REPLY)&&s->pending); /* duplicate init */
    memcpy(reply,req,12);CHECK(elden_ulog_receive(s,7,reply,ELDEN_ULOG_REPLY)==1);
    elden_ulog_release(s);CHECK(!elden_ulog_next(s,req));
    elden_ulog_link(s,false,8);elden_ulog_link(s,true,9);CHECK(!elden_ulog_next(s,req));
    /* Every undersize (including near-8KiB) and one oversize must stop before body copy. */
    for(unsigned n=0;n<=ELDEN_ULOG_REPLY+1;n++)if(n!=ELDEN_ULOG_REPLY){
        start(s,req);memcpy(reply,req,12);
        uint8_t *exact=malloc(n?n:1);CHECK(exact);if(n)memcpy(exact,reply,n);
        CHECK(elden_ulog_receive(s,7,exact,n)==-1&&s->error==EldenUlogLength);free(exact);
    }
    for(unsigned field=0;field<3;field++){
        start(s,req);memcpy(reply,req,12);put(reply+field*4,get(reply+field*4)+1);
        int result=elden_ulog_receive(s,7,reply,ELDEN_ULOG_REPLY);
        CHECK(field==0?(result==0&&s->pending):(result==-1&&s->failed));
    }
    start(s,req);elden_ulog_fail(s,EldenUlogTimeout);CHECK(!elden_ulog_next(s,req));
    elden_ulog_link(s,true,7);CHECK(s->failed);elden_ulog_link(s,true,8);
    CHECK(elden_ulog_next(s,req)==16&&s->requests==2);elden_ulog_fail(s,EldenUlogOverflow);
    elden_ulog_link(s,true,9);CHECK(!elden_ulog_next(s,req));
    start(s,req);elden_ulog_fail(s,EldenUlogStopped);CHECK(!elden_ulog_next(s,req));
    free(reply);free(s);printf("UlogRead: %u checks; exact 8204B, epochs, duplicates, no SET, boot-wide cap\n",checks);
}
