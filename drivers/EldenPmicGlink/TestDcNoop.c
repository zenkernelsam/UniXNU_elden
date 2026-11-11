/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcNoop.h"
#include "ColdToken.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint8_t expected[19]={0xdb,1,0,10,0x10,0x12,1,1,0,0x80,0x80,0x10,0x12,1,1,0,0x80,0x80,0xdb};
static void start(EldenDcNoop *s){elden_dc_noop_init(s);elden_dc_noop_link(s,true,7);CHECK(elden_dc_noop_begin(s)==1);}
static void read_snapshot(EldenDcNoop *s,int changed){
    uint8_t req[28],reply[284];
    for(unsigned i=0;i<19;i++){
        CHECK(elden_dc_noop_next(s,true,req)==28&&get(req+8)==0x37&&get(req+16)==7);
        memset(reply,0,sizeof(reply));memcpy(reply,req,28);reply[28]=expected[i];if((int)i==changed)reply[28]^=1;
        int rc=elden_dc_noop_receive(s,reply,sizeof(reply));
        if(i<18)CHECK(rc==1);else CHECK(rc==2||rc==3||rc==-1);
    }
}
int main(void){
    EldenDcNoop s;uint8_t req[28],ack[16]={0};put(ack,32783);put(ack+4,1);put(ack+8,0x38);
    start(&s);read_snapshot(&s,-1);CHECK(s.phase==EldenDcNoopWrite&&!s.write_attempted);
    CHECK(elden_dc_noop_next(&s,true,req)==28);CHECK(get(req)==32783&&get(req+4)==1&&get(req+8)==0x38&&get(req+12)==0&&get(req+16)==7&&get(req+20)==0x2a52&&get(req+24)==10);
    elden_dc_noop_unsent(&s);CHECK(!s.write_attempted&&!s.pending);
    CHECK(elden_dc_noop_next(&s,true,req)==28);CHECK(elden_dc_noop_receive(&s,ack,16)==1);
    read_snapshot(&s,-1);CHECK(s.verified&&s.phase==EldenDcNoopDone&&s.write_acks==1);
    CHECK(!elden_dc_noop_next(&s,true,req)&&!elden_dc_noop_begin(&s));
    for(int field=0;field<19;field++){
        start(&s);read_snapshot(&s,field);
        CHECK(s.phase==EldenDcNoopFailed&&!s.write_attempted&&!elden_dc_noop_next(&s,true,req));
    }
    start(&s);read_snapshot(&s,-1);CHECK(!elden_dc_noop_next(&s,false,req)&&!s.write_attempted);
    for(unsigned n=0;n<20;n++)if(n!=16){
        start(&s);read_snapshot(&s,-1);CHECK(elden_dc_noop_next(&s,true,req)==28);
        uint8_t *p=calloc(n?n:1,1);CHECK(p);memcpy(p,ack,n<16?n:16);
        CHECK(elden_dc_noop_receive(&s,p,n)==-1&&s.phase==EldenDcNoopFailed);
        CHECK(s.reply_bytes==n&&s.reply_type==(n>=8?1u:UINT32_MAX));
        CHECK(s.reply_opcode==(n>=12?0x38u:UINT32_MAX)&&s.firmware_status==(n>=16?0u:UINT32_MAX));free(p);
        elden_dc_noop_link(&s,true,8);CHECK(!elden_dc_noop_begin(&s));
    }
    start(&s);read_snapshot(&s,-1);CHECK(elden_dc_noop_next(&s,true,req)==28);put(ack+12,1);
    CHECK(elden_dc_noop_receive(&s,ack,16)==-1&&!s.verified);CHECK(s.reply_bytes==16&&s.reply_type==1&&s.reply_opcode==0x38&&s.firmware_status==1);put(ack+12,0);
    start(&s);read_snapshot(&s,-1);CHECK(elden_dc_noop_next(&s,true,req)==28);
    elden_dc_noop_fail(&s,EldenDcNoopTimeout);CHECK(s.write_attempted&&!s.verified&&!elden_dc_noop_next(&s,true,req));
    start(&s);read_snapshot(&s,-1);CHECK(elden_dc_noop_next(&s,true,req)==28);CHECK(elden_dc_noop_receive(&s,ack,16)==1);
    read_snapshot(&s,3);CHECK(!s.verified&&s.error==EldenDcNoopReadback);
    start(&s);elden_dc_noop_cancel(&s);CHECK(s.phase==EldenDcNoopFailed&&!s.write_attempted&&!elden_dc_noop_next(&s,true,req));
    start(&s);read_snapshot(&s,-1);elden_dc_noop_cancel(&s);CHECK(s.phase==EldenDcNoopFailed&&!s.write_attempted);
    start(&s);read_snapshot(&s,-1);CHECK(elden_dc_noop_next(&s,true,req)==28);
    elden_dc_noop_cancel(&s);CHECK(s.pending&&s.phase==EldenDcNoopWrite);
    CHECK(elden_dc_noop_receive(&s,ack,16)==1);elden_dc_noop_cancel(&s);CHECK(s.phase==EldenDcNoopVerify);
    read_snapshot(&s,-1);CHECK(s.verified&&s.write_acks==1);elden_dc_noop_cancel(&s);CHECK(s.phase==EldenDcNoopDone);
    CHECK(!elden_cold_token_valid(0,1,0));CHECK(!elden_cold_token_valid(elden_cold_token(7,100),8,100));
    CHECK(elden_cold_token_valid(elden_cold_token(7,100),7,1100));CHECK(!elden_cold_token_valid(elden_cold_token(7,100),7,1101));
    CHECK(elden_cold_token_valid(elden_cold_token(7,UINT32_MAX-100),7,100));
    printf("DcNoop: %u checks; only bus0/SID7/2a52=10, pre/post readback, one attempt and no error retries\n",checks);
}
