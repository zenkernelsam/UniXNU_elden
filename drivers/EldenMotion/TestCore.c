/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent wire construction, actual public decoder/math; no device IO. */
#include "Core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint8_t b[2048];size_t n;} Bytes;
static void byte(Bytes *b,unsigned v){CHECK(b->n<sizeof(b->b));b->b[b->n++]=(uint8_t)v;}
static void u16(Bytes *b,unsigned v){byte(b,v);byte(b,v>>8);}
static void u32(Bytes *b,uint32_t v){for(unsigned i=0;i<4;i++)byte(b,v>>(i*8));}
static void u64(Bytes *b,uint64_t v){for(unsigned i=0;i<8;i++)byte(b,(unsigned)(v>>(i*8)));}
static void raw(Bytes *b,const void *p,size_t n){CHECK(n<=sizeof(b->b)-b->n);memcpy(b->b+b->n,p,n);b->n+=n;}
static void field(Bytes *b,unsigned tag,const void *p,size_t n){byte(b,tag);CHECK(n<128);byte(b,(unsigned)n);raw(b,p,n);}
static void tlv(Bytes *b,unsigned tag,const void *p,size_t n){byte(b,tag);u16(b,(unsigned)n);raw(b,p,n);}
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
static Bytes report(EldenMotionSuid from,unsigned id,uint64_t stamp,const uint8_t *payload,size_t n){
    Bytes suid={0},event={0},events={0},t={0},q={0},value={0};
    byte(&suid,9);u64(&suid,from.low);byte(&suid,17);u64(&suid,from.high);
    byte(&event,13);u32(&event,id);byte(&event,17);u64(&event,stamp);field(&event,26,payload,n);
    field(&events,10,suid.b,suid.n);field(&events,18,event.b,event.n);
    uint8_t client[8]={11,0,0,0,0,0,0,0};tlv(&t,1,client,8);u16(&value,(unsigned)events.n);raw(&value,events.b,events.n);tlv(&t,2,value.b,value.n);
    byte(&q,4);u16(&q,0);u16(&q,0x21);u16(&q,(unsigned)t.n);raw(&q,t.b,t.n);return q;
}
static Bytes sensor(uint32_t x,uint32_t y,uint32_t z,unsigned extra,unsigned tail,unsigned status){
    Bytes p={0};byte(&p,10);byte(&p,12+extra*4+tail);u32(&p,x);u32(&p,y);u32(&p,z);
    if(extra)u32(&p,0);if(tail)byte(&p,0);byte(&p,16);byte(&p,status);return p;
}
int main(void){
    EldenMotionSpec spec={{1,2},EldenMotionAccel,EldenMotionG,16*65536,{1,2,3}};
    EldenMotionSample out[16],old[16];memset(out,0xa5,sizeof(out));memcpy(old,out,sizeof(out));
    Bytes p=sensor(0,bits(1),0,0,0,3),q=report(spec.suid,1025,19200000,p.b,p.n);
    CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)==1&&out[0].q16[1]==65536&&out[0].timestamp_ticks==19200000);
    for(size_t i=0;i<q.n;i++){memcpy(out,old,sizeof(out));CHECK(elden_motion_samples(q.b,i,11,&spec,out,16)<0&&!memcmp(out,old,sizeof(out)));}
    memcpy(out,old,sizeof(out));CHECK(elden_motion_samples(q.b,q.n,12,&spec,out,16)==-13&&!memcmp(out,old,sizeof(out)));
    Bytes bad_container=q;byte(&bad_container,16);byte(&bad_container,0);
    bad_container.b[5]+=2;bad_container.b[19]+=2;bad_container.b[21]+=2;
    memcpy(out,old,sizeof(out));CHECK(elden_motion_samples(bad_container.b,bad_container.n,11,&spec,out,16)<0&&!memcmp(out,old,sizeof(out)));
    spec.mount[0]=-1;CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)==-22);spec.mount[0]=1;
    spec.mount[0]=-2;spec.mount[1]=1;CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)==1&&out[0].q16[0]==-65536);spec.mount[0]=1;spec.mount[1]=2;
    const uint32_t invalid[]={0x7f800000,0xff800000,0x7fc00001,bits(17.0f)};
    for(unsigned i=0;i<4;i++){p=sensor(invalid[i],bits(1),0,0,0,3);q=report(spec.suid,1025,1,p.b,p.n);memcpy(out,old,sizeof(out));CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)<0&&!memcmp(out,old,sizeof(out)));}
    for(unsigned mode=0;mode<3;mode++){
        p=sensor(0,bits(1),0,mode==0,mode==1,mode==2?4:3);q=report(spec.suid,1025,1,p.b,p.n);
        memcpy(out,old,sizeof(out));CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)<0&&!memcmp(out,old,sizeof(out)));
    }
    // Overflowing 10-byte status varint was accepted by the original reader.
    p=sensor(0,bits(1),0,0,0,3);p.n--;for(unsigned i=0;i<9;i++)byte(&p,0x80);byte(&p,2);
    q=report(spec.suid,1025,1,p.b,p.n);CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)<0);
    for(int i=-16384;i<=16384;i++){
        p=sensor(bits((float)i/1024.0f),0,0,0,0,3);q=report(spec.suid,1025,1,p.b,p.n);
        CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)==1&&out[0].q16[0]==i*64);
    }
    spec.unit=EldenMotionMetersPerSecond2;spec.max_abs_q16=200*65536;p=sensor(0,bits(9.80665f),0,0,0,3);q=report(spec.suid,1025,1,p.b,p.n);
    CHECK(elden_motion_samples(q.b,q.n,11,&spec,out,16)==1&&out[0].unit==EldenMotionG&&out[0].q16[1]>=65535&&out[0].q16[1]<=65536);
    Bytes found={0},uid={0};byte(&uid,9);u64(&uid,1);byte(&uid,17);u64(&uid,2);field(&found,10,"accel",5);field(&found,18,uid.b,uid.n);
    EldenMotionSuid pseud={UINT64_C(0xabababababababab),UINT64_C(0xabababababababab)},result={3,4};
    q=report(pseud,768,1,found.b,found.n);CHECK(elden_motion_discovery_report(q.b,q.n,11,1,&result)==1&&result.low==1&&result.high==2);
    CHECK(elden_motion_discovery_report(q.b,q.n,11,2,&result)==0);
    Bytes bad_discovery=found;byte(&bad_discovery,16);byte(&bad_discovery,0);q=report(pseud,768,1,bad_discovery.b,bad_discovery.n);
    CHECK(elden_motion_discovery_report(q.b,q.n,11,1,&result)<0);
    field(&found,18,uid.b,uid.n);q=report(pseud,768,1,found.b,found.n);CHECK(elden_motion_discovery_report(q.b,q.n,11,1,&result)<0);
    uint8_t small[4]={0xaa,0xaa,0xaa,0xaa};CHECK(!elden_motion_discovery_request(small,sizeof(small),1,1)&&small[0]==0xaa);
    uint8_t wire[512];size_t n=elden_motion_discovery_request(wire,sizeof(wire),1,1);CHECK(n>20&&wire[0]==0&&wire[3]==0x20);
    CHECK(!elden_motion_discovery_request(wire,sizeof(wire),0,1));CHECK(!elden_motion_sampling_request(wire,sizeof(wire),1,pseud,30,0));
    CHECK(elden_motion_sampling_request(wire,sizeof(wire),1,spec.suid,30,0)>20);CHECK(elden_motion_sampling_request(wire,sizeof(wire),2,spec.suid,0,1)>20);
    Bytes t={0},response={0};uint8_t ok[4]={0},client[8]={11,0,0,0,0,0,0,0};tlv(&t,2,ok,4);tlv(&t,16,client,8);tlv(&t,17,ok,4);
    byte(&response,2);u16(&response,7);u16(&response,32);u16(&response,(unsigned)t.n);raw(&response,t.b,t.n);uint64_t cl=99;
    CHECK(!elden_motion_client_response(response.b,response.n,7,&cl)&&cl==11);CHECK(elden_motion_client_response(response.b,response.n,8,&cl)<0&&cl==11);
    EldenMotionRotation rot={0};EldenMotionSample s={{1,2},1,{0,65536,0},1,1,3};int32_t axis=77;
    CHECK(!elden_motion_rotation(&rot,&s,150000,&axis)&&axis==77);s.timestamp_ticks+=2880000;
    CHECK(elden_motion_rotation(&rot,&s,150000,&axis)==1&&axis==2);
    CHECK(elden_motion_rotation(&rot,&s,150000,&axis)==-116);s.q16[0]=65536;s.q16[1]=0;s.timestamp_ticks+=19200000;
    CHECK(!elden_motion_rotation(&rot,&s,150000,&axis));s.timestamp_ticks+=2880000;CHECK(elden_motion_rotation(&rot,&s,150000,&axis)==1&&axis==1);
    s.timestamp_ticks--;CHECK(elden_motion_rotation(&rot,&s,150000,&axis)==-110);s.timestamp_ticks+=19200000;CHECK(elden_motion_rotation(&rot,&s,150000,&axis)==-110);
    printf("PASS: %u actual motion codec/math checks, source discovery/client/samples, malformed/truncation/float bounds, proper mount and gravity hold; hardware absent\n",checks);
}
