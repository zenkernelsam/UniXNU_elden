/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UlogRead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static EldenUlogRead s;
static uint8_t reply[ELDEN_ULOG_REPLY],request[16];
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void start(void){elden_ulog_init(&s);elden_ulog_enable_paging(&s);elden_ulog_link(&s,true,7);CHECK(s.paging);}
static void send(unsigned op){CHECK(elden_ulog_next(&s,request)==16&&get(request+8)==op);memcpy(reply,request,12);}
static void content(unsigned value){memset(reply+12,0,ELDEN_ULOG_PAYLOAD);if(value)memset(reply+12,value,100);}
static void receive(void){CHECK(elden_ulog_receive(&s,7,reply,sizeof(reply))==1);CHECK(s.ready&&!s.pending);elden_ulog_release(&s);}
int main(void){
    start();send(0x23);content(0);receive();CHECK(s.init_empty&&s.init_pages==1);
    send(0x18);content(0);receive();CHECK(s.current_empty&&s.current_pages==1&&!elden_ulog_next(&s,request));
    /* Every allowed empty-page position and nonempty page limit. */
    for(unsigned empty=0;empty<=8;empty++){
        start();unsigned init=empty?empty:8;
        for(unsigned i=1;i<=init;i++){send(0x23);content(i==empty?0:64+i);receive();}
        CHECK(s.init_pages==init&&s.init_empty==(empty!=0));
        for(unsigned i=1;i<=4;i++){send(0x18);content(96+i);receive();}
        CHECK(s.current_pages==4&&!s.current_empty&&s.requests<=12&&!elden_ulog_next(&s,request));
    }
    start();send(0x23);content(7);receive();send(0x23);
    CHECK(elden_ulog_receive(&s,7,reply,sizeof(reply))==-1&&s.error==EldenUlogDuplicate);
    CHECK(!elden_ulog_next(&s,request));
    /* Bytes past the first NUL are firmware padding and cannot make a repeat new. */
    start();send(0x23);content(8);receive();send(0x23);reply[8000]=0x99;
    CHECK(elden_ulog_receive(&s,7,reply,sizeof(reply))==-1&&s.error==EldenUlogDuplicate);
    start();send(0x23);memset(reply+12,'A',8192);
    CHECK(elden_ulog_receive(&s,7,reply,sizeof(reply))==-1&&s.error==EldenUlogTuple);
    start();send(0x23);content(9);CHECK(!elden_ulog_receive(&s,6,reply,sizeof(reply))&&s.pending);
    elden_ulog_unsent(&s);CHECK(s.requests==0);send(0x23);receive();
    /* Unknown requests are not retried in the same epoch; quota survives fresh links. */
    start();
    for(unsigned i=0;i<12;i++){
        CHECK(elden_ulog_next(&s,request)==16);elden_ulog_fail(&s,EldenUlogTimeout);
        CHECK(!elden_ulog_next(&s,request));elden_ulog_link(&s,true,8+i);
    }
    CHECK(s.requests==12&&!elden_ulog_next(&s,request));
    elden_ulog_init(&s);elden_ulog_link(&s,true,1);elden_ulog_enable_paging(&s);CHECK(!s.paging);
    printf("UlogPaging: %u checks; bounded pages, empty terminators, repeat rejection and epoch quota\n",checks);
}
