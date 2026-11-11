/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PowerPathRead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void){
    const uint32_t expected[]={0x270b,0x2954,0x2952,0x2a52,0x9104,0x9105,0x9140,0x9141,0x9144,0x9146,0x9108,0x9204,0x9205,0x9240,0x9241,0x9244,0x9246,0x9208,0x270b};
    const uint8_t stock[]={0xe2,0,10,10,0x10,0x12,1,1,0,0x80,0x80,0x10,0x12,1,1,0,0x80,0x80,0xe2};
    EldenPowerPathRead s;uint8_t req[28],reply[284];
    for(unsigned bad=0;bad<=19;bad++){
        elden_power_reg_init(&s);elden_power_reg_extend(&s);CHECK(elden_power_reg_count(&s)==19);
        elden_power_reg_link(&s,true,7);CHECK(s.extended&&elden_power_reg_begin(&s));
        for(unsigned i=0;i<19;i++){
            CHECK(elden_power_reg_next(&s,req)==28&&get(req)==32783&&get(req+8)==0x37&&get(req+12)==0&&get(req+16)==7&&get(req+20)==expected[i]&&get(req+24)==1);
            memset(reply,0,sizeof(reply));memcpy(reply,req,28);reply[28]=stock[i];
            if(bad==i){put(reply+20,expected[i]+1);CHECK(elden_power_reg_receive(&s,reply,sizeof(reply))==-1&&s.failed);break;}
            CHECK(elden_power_reg_receive(&s,reply,sizeof(reply))==(i==18?2:1));
        }
        if(bad==19){CHECK(s.cycles==1&&!s.failed&&!memcmp(s.values,stock,19));}
        else CHECK(s.cycles==0&&!elden_power_reg_next(&s,req));
        elden_power_reg_link(&s,true,8);CHECK(s.extended&&!s.failed);
    }
    elden_power_reg_init(&s);elden_power_reg_link(&s,true,1);elden_power_reg_extend(&s);CHECK(elden_power_reg_count(&s)==4);
    elden_power_reg_init(&s);elden_power_reg_extend(&s);elden_power_reg_link(&s,true,1);CHECK(elden_power_reg_begin(&s));s.index=19;
    CHECK(!elden_power_reg_next(&s,req)&&s.failed);
    printf("PowerPathGpio: %u checks; exact19-register whitelist, stock replay, partial rejection and legacy default\n",checks);
}
