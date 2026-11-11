/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PowerPathRead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void begin(EldenPowerPathRead *s){elden_power_reg_init(s);elden_power_reg_link(s,true,7);CHECK(elden_power_reg_begin(s)==1);}
static void reply(uint8_t out[284],const uint8_t req[28],uint8_t value){memset(out,0xa5,284);memcpy(out,req,28);out[28]=value;}
int main(void){
    EldenPowerPathRead s;uint8_t req[28],resp[284];const uint32_t regs[]={0x270b,0x2952,0x2a52,0x270b};
    elden_power_reg_init(&s);CHECK(!elden_power_reg_begin(&s)&&!elden_power_reg_next(&s,req));
    begin(&s);CHECK(elden_power_reg_next(&s,req)==28);elden_power_reg_unsent(&s);CHECK(elden_power_reg_next(&s,req)==28&&get(req+20)==0x270b);
    reply(resp,req,0xe2);CHECK(elden_power_reg_receive(&s,resp,sizeof(resp))==1);CHECK(!elden_power_reg_receive(&s,resp,sizeof(resp)));
    for(unsigned n=1;n<4;++n){CHECK(elden_power_reg_next(&s,req)==28&&get(req+20)==regs[n]);reply(resp,req,n==1?10:n==2?2:0xe2);CHECK(elden_power_reg_receive(&s,resp,sizeof(resp))==(n==3?2:1));}
    CHECK(s.cycles==1&&!s.active&&!s.failed&&s.values[0]==0xe2&&s.values[1]==10&&s.values[2]==2&&s.values[3]==0xe2);
    for(unsigned field=0;field<7;++field){
        begin(&s);CHECK(elden_power_reg_next(&s,req)==28);reply(resp,req,0xe2);put(resp+field*4,get(resp+field*4)+1);
        int result=elden_power_reg_receive(&s,resp,sizeof(resp));
        if(!field){CHECK(result==0&&s.pending);reply(resp,req,0xe2);CHECK(elden_power_reg_receive(&s,resp,sizeof(resp))==1);}
        else{CHECK(result==-1&&s.failed&&!s.pending);CHECK(!elden_power_reg_next(&s,req));elden_power_reg_link(&s,true,7);CHECK(s.failed);elden_power_reg_link(&s,true,8);CHECK(!s.failed);}
    }
    for(unsigned size=0;size<=285;++size){
        if(size==284)continue;
        begin(&s);CHECK(elden_power_reg_next(&s,req)==28);reply(resp,req,0xe2);
        CHECK(elden_power_reg_receive(&s,resp,size)==-1&&s.error==EldenPowerRegLength); /* size is rejected before body reads */
    }
    begin(&s);CHECK(elden_power_reg_next(&s,req)==28);reply(resp,req,0xe2);put(resp+24,0xfffffffbu);
    CHECK(elden_power_reg_receive(&s,resp,284)==-1&&s.error==EldenPowerRegFirmware);
    begin(&s);CHECK(elden_power_reg_next(&s,req)==28);elden_power_reg_fail(&s,EldenPowerRegTimeout);CHECK(!elden_power_reg_begin(&s));
    elden_power_reg_link(&s,true,7);CHECK(s.failed);elden_power_reg_link(&s,false,8);CHECK(!elden_power_reg_begin(&s));elden_power_reg_link(&s,true,9);CHECK(elden_power_reg_begin(&s));
    for(unsigned n=0;n<4;++n){CHECK(elden_power_reg_next(&s,req)==28);CHECK(get(req)==ELDEN_POWER_REG_OWNER&&get(req+4)==1&&get(req+8)==0x37&&get(req+12)==0&&get(req+16)==7&&get(req+24)==1);reply(resp,req,n==3?0xd2:0xe2);CHECK(elden_power_reg_receive(&s,resp,284)>0);}
    CHECK(((s.values[0]^s.values[3])&0x30)!=0); /* Source switched within this read set; report unstable. */
    begin(&s);s.index=4;CHECK(!elden_power_reg_next(&s,req)&&s.failed);
    printf("PowerPathRead: %u checks; fixed read-only sequence, full response identity, epochs and failures\n",checks);
}
