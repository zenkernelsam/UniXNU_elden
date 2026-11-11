/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargeNotify.h"
#include "ChargeRead.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}} while(0)
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void begin(EldenChargeNotify *s,uint8_t out[28]) {
    elden_charge_notify_init(s);CHECK(!elden_charge_notify_next(s,out));
    elden_charge_notify_link(s,true);CHECK(s->epoch==1);
    CHECK(!elden_charge_notify_next(s,NULL));CHECK(!s->pending);
    CHECK(elden_charge_notify_next(s,out)==28);
    const uint8_t expected[28]={0x0a,0x80,0,0,2,0,0,0,4,0,0,0};
    CHECK(!memcmp(out,expected,sizeof(expected)));CHECK(!elden_charge_notify_next(s,out));
}
int main(void) {
    EldenChargeNotify s;uint8_t out[28],reply[32];
    for(unsigned type=1;type<=2;++type)for(unsigned variant=0;variant<3;++variant) {
        const size_t bytes=variant==0?12:variant==1?16:28;
        begin(&s,out);memcpy(reply,out,28);put32(reply+4,type);
        CHECK(elden_charge_notify_receive(&s,reply,bytes)==1);
        CHECK(s.registered&&!s.quarantined&&!s.pending&&s.reply_bytes==bytes);
        CHECK(!elden_charge_notify_next(&s,out));
        elden_charge_notify_link(&s,true);CHECK(!elden_charge_notify_next(&s,out));
        CHECK(!elden_charge_notify_receive(&s,reply,bytes));
    }
    begin(&s,out);memcpy(reply,out,28);
    put32(reply,ELDEN_CHARGE_OWNER+1);CHECK(!elden_charge_notify_receive(&s,reply,16));
    put32(reply,ELDEN_CHARGE_OWNER);put32(reply+8,0x30);
    CHECK(!elden_charge_notify_receive(&s,reply,16));CHECK(s.pending);
    put32(reply+8,4);CHECK(!elden_charge_notify_receive(&s,NULL,16));
    CHECK(!elden_charge_notify_receive(&s,reply,11));CHECK(s.pending);
    elden_charge_notify_timeout(&s);CHECK(s.quarantined&&s.timeouts==1&&!s.pending);
    CHECK(!elden_charge_notify_receive(&s,reply,16));
    elden_charge_notify_link(&s,true);CHECK(!elden_charge_notify_next(&s,out));
    elden_charge_notify_link(&s,false);CHECK(!elden_charge_notify_next(&s,out));
    elden_charge_notify_link(&s,true);CHECK(s.epoch==2&&elden_charge_notify_next(&s,out)==28);
    CHECK(elden_charge_notify_receive(&s,reply,16)==1);
    for(unsigned bad=0;bad<4;++bad) {
        begin(&s,out);memcpy(reply,out,28);size_t bytes=16;
        if(bad==0)put32(reply+4,3);
        if(bad==1)reply[12]=1;
        if(bad==2)bytes=20;
        if(bad==3){bytes=28;reply[27]=1;}
        CHECK(elden_charge_notify_receive(&s,reply,bytes)==-1);
        CHECK(s.quarantined&&!s.registered&&!s.pending);
        CHECK(!elden_charge_notify_next(&s,out));
    }
    begin(&s,out);elden_charge_notify_unsent(&s);
    CHECK(!s.pending&&elden_charge_notify_next(&s,out)==28);
    CHECK(s.requests==2);elden_charge_notify_link(&s,false);
    memcpy(reply,out,28);CHECK(!elden_charge_notify_receive(&s,reply,28));
    printf("ChargeNotify: %u checks passed\n",checks);return 0;
}
