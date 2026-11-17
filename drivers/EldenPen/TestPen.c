/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Delivery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenPenLimits limits={19040,30400,4095,60};
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
static void packet(uint8_t *p,unsigned pressure){
    memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);
}
static EldenPenFrame decoded(unsigned pressure){
    uint8_t p[109];EldenPenFrame f;packet(p,pressure);
    CHECK(!elden_pen_decode(p,sizeof(p),&limits,&f));return f;
}
static void decode_cases(void){
    uint8_t p[110];packet(p,4095);EldenPenFrame f,before;
    memset(&f,0xa5,sizeof(f));before=f;
    CHECK(elden_pen_decode(NULL,109,&limits,&f)==EldenPenArgument);
    CHECK(elden_pen_decode(p,108,&limits,&f)==EldenPenLength);
    CHECK(elden_pen_decode(p,110,&limits,&f)==EldenPenLength);
    CHECK(!memcmp(&f,&before,sizeof(f)));
    EldenPenLimits invalid=limits;invalid.max_pressure=16384;
    CHECK(elden_pen_decode(p,109,&invalid,&f)==EldenPenArgument);
    invalid=limits;invalid.max_x=0;CHECK(elden_pen_decode(p,109,&invalid,&f)==EldenPenArgument);
    CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(f.x==16384&&f.y==16384&&f.pressure==4095&&f.tip&&f.in_range);
    put(p,67,19040);put(p,69,30400);p[73]=196;p[74]=60;p[77]=0xff;p[78]=255;p[79]=0xa9;
    CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(f.x==32767&&f.y==32767&&f.tilt_x==-60&&f.tilt_y==60);
    CHECK(f.buttons==3&&f.battery_raw==255&&f.tail_raw==0xa9);
    before=f;put(p,67,19041);CHECK(elden_pen_decode(p,109,&limits,&f)==EldenPenRange);CHECK(!memcmp(&before,&f,sizeof(f)));
    put(p,67,19040);put(p,71,4096);CHECK(elden_pen_decode(p,109,&limits,&f)==EldenPenRange);
    invalid=limits;invalid.max_pressure=8191;put(p,71,8191);CHECK(!elden_pen_decode(p,109,&invalid,&f));
    put(p,71,8192);CHECK(elden_pen_decode(p,109,&invalid,&f)==EldenPenRange);
    packet(p,0);put(p,75,0xffff);CHECK(!elden_pen_decode(p,109,&limits,&f));
    CHECK(!f.tip&&f.in_range&&f.distance==65535); /* raw, not clamped/calibrated mm */
    put(p,75,0);CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(!f.tip&&!f.in_range);
    for(unsigned b=0;b<256;++b){
        p[73]=(uint8_t)b;int signed_value=b<128?(int)b:(int)b-256;
        int rc=elden_pen_decode(p,109,&limits,&f);
        CHECK(rc==(signed_value>=-60&&signed_value<=60?0:EldenPenRange));
        if(!rc)CHECK(f.tilt_x==signed_value);
    }
    packet(p,1);CHECK(!elden_pen_decode(p,109,&limits,&before));
    f=before;p[65]=1;CHECK(elden_pen_decode(p,109,&limits,&f)==EldenPenEnvelope);CHECK(!memcmp(&f,&before,sizeof(f)));p[65]=0;
    for(unsigned i=0;i<65;++i){
        p[i]^=0x5a;uint8_t sum=0;for(unsigned j=1;j<65;++j)sum=(uint8_t)(sum+p[j]);p[65]=(uint8_t)(0u-sum);
        CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(!memcmp(&f,&before,sizeof(f)));
    }
    p[66]=0xf2;CHECK(elden_pen_decode(p,109,&limits,&f)==EldenPenUnsupported);CHECK(!memcmp(&f,&before,sizeof(f)));
    p[66]=0xf0;for(unsigned i=0;i<6;++i)p[67+i]=(uint8_t)(i+1);
    CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(f.kind==EldenPenIdentity&&!f.tip&&!f.in_range&&f.identity[5]==6);
    p[66]=0xff;CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(f.kind==EldenPenLeave&&!f.tip&&!f.buttons);
    packet(p,1);unsigned previous=0;
    for(unsigned x=0;x<=19040;x+=119){put(p,67,x);CHECK(!elden_pen_decode(p,109,&limits,&f));CHECK(f.x>=previous&&f.x<=32767);previous=f.x;}
}
typedef struct {unsigned count,fail;EldenPenFrame trace[64];} Sink;
static int send(void *p,const EldenPenFrame *f){
    Sink *s=p;if(s->fail){--s->fail;return -99;}CHECK(s->count<64);s->trace[s->count++]=*f;return 0;
}
static void delivery_cases(void){
    EldenPenDelivery s;Sink sink={0};EldenPenFrame down=decoded(100),hover=decoded(0),identity={0},leave={0};
    identity.kind=EldenPenIdentity;leave.kind=EldenPenLeave;hover.distance=1;hover.in_range=1;
    CHECK(!elden_pen_delivery_init(&s,100));CHECK(elden_pen_delivery_released(&s));
    sink.fail=1;CHECK(elden_pen_delivery_feed(&s,&down,1,send,&sink)==-99);CHECK(!s.has_accepted&&s.dirty&&sink.count==0);
    CHECK(!elden_pen_delivery_tick(&s,2,send,&sink));CHECK(s.accepted.tip&&sink.count==1&&!s.dirty);
    CHECK(!elden_pen_delivery_feed(&s,&down,3,send,&sink));CHECK(sink.count==1);
    down.pressure=200;sink.fail=1;
    CHECK(elden_pen_delivery_feed(&s,&down,4,send,&sink)==-99);CHECK(s.accepted.pressure==100);
    CHECK(!elden_pen_delivery_tick(&s,5,send,&sink));CHECK(s.accepted.pressure==200&&sink.count==2);
    CHECK(!elden_pen_delivery_feed(&s,&hover,6,send,&sink));CHECK(!s.accepted.tip&&s.accepted.in_range);
    sink.fail=1;CHECK(elden_pen_delivery_feed(&s,&leave,7,send,&sink)==-99);CHECK(s.release_pending&&!elden_pen_delivery_released(&s));
    unsigned old=sink.count;CHECK(!elden_pen_delivery_feed(&s,&down,8,send,&sink));
    CHECK(sink.count==old+2&&!sink.trace[old].in_range&&sink.trace[old+1].tip);
    CHECK(!elden_pen_delivery_feed(&s,&identity,99,send,&sink));CHECK(s.accepted.tip);
    CHECK(!elden_pen_delivery_tick(&s,108,send,&sink));CHECK(elden_pen_delivery_released(&s));
    CHECK(!elden_pen_delivery_feed(&s,&down,110,send,&sink));sink.fail=2;
    CHECK(elden_pen_delivery_fault(&s,111,send,&sink)==-99);CHECK(s.accepted.tip&&s.release_pending);
    down.x++;
    CHECK(elden_pen_delivery_feed(&s,&down,112,send,&sink)==-99);CHECK(s.release_pending);
    old=sink.count;CHECK(!elden_pen_delivery_tick(&s,113,send,&sink));CHECK(sink.count==old+2&&!sink.trace[old].tip&&sink.trace[old+1].tip);
    sink.fail=1;CHECK(elden_pen_delivery_stop(&s,114,send,&sink)==-99);CHECK(!elden_pen_delivery_released(&s));
    CHECK(elden_pen_delivery_feed(&s,&down,115,send,&sink)==EldenPenStopped);CHECK(elden_pen_delivery_released(&s));
    old=sink.count;CHECK(!elden_pen_delivery_tick(&s,116,send,&sink));CHECK(sink.count==old);
    CHECK(!elden_pen_delivery_init(&s,100));CHECK(!elden_pen_delivery_feed(&s,&down,200,send,&sink));
    CHECK(elden_pen_delivery_tick(&s,199,send,&sink)==EldenPenClock);CHECK(elden_pen_delivery_released(&s));
    old=sink.count;CHECK(elden_pen_delivery_feed(&s,&down,201,send,&sink)==EldenPenClock);CHECK(sink.count==old);
    CHECK(!elden_pen_delivery_init(&s,100));CHECK(!elden_pen_delivery_feed(&s,&down,0,send,&sink));
    old=sink.count;CHECK(!elden_pen_delivery_feed(&s,&down,100,send,&sink));CHECK(sink.count==old+2&&!sink.trace[old].tip);
    leave.kind=0xf2;CHECK(elden_pen_delivery_feed(&s,&leave,101,send,&sink)==EldenPenUnsupported);CHECK(elden_pen_delivery_released(&s));
    CHECK(!elden_pen_delivery_init(&s,100));CHECK(!elden_pen_delivery_feed(&s,&down,0,send,&sink));
    identity.kind=EldenPenInfo;old=sink.count;
    CHECK(!elden_pen_delivery_feed(&s,&identity,99,send,&sink));CHECK(sink.count==old&&s.accepted.tip);
    CHECK(!elden_pen_delivery_tick(&s,100,send,&sink));CHECK(elden_pen_delivery_released(&s));
    CHECK(elden_pen_delivery_init(&s,0)==EldenPenDeliveryArgument);
}
int main(void){decode_cases();delivery_cases();printf("PASS: %u checks; synthetic protocol boundaries and actual delivery state; no raw SPI or hardware claim\n",checks);return 0;}
