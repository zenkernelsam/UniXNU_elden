/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargeVotes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static void list(uint8_t out[548]){
    memset(out,0,548);put(out,32781);put(out+4,1);put(out+8,0x15);put(out+12,4);put(out+16,3);
    const char *names[]={"fcc_1","fcc_2","fv","iusb0_tot"},*clients[]={"plat","thermal","i_sdp"};
    for(unsigned i=0;i<4;++i)memcpy(out+20+12*i,names[i],strlen(names[i]));
    for(unsigned i=0;i<3;++i)memcpy(out+164+12*i,clients[i],strlen(clients[i]));
}
static void item(uint8_t out[192],unsigned id){
    memset(out,0,192);put(out,32781);put(out+4,1);put(out+8,0x17);put(out+12,id);
    put(out+16,500);put(out+60,500);put(out+180,4);put(out+184,2);put(out+188,UINT32_MAX);
}
int main(void){
    EldenChargeVotes s;uint8_t request[16],data[548];list(data);
    elden_charge_votes_init(&s);CHECK(!elden_charge_votes_next(&s,request));elden_charge_votes_link(&s,true);
    CHECK(elden_charge_votes_next(&s,request)==16&&get(request)==32781&&get(request+8)==0x15&&get(request+12)==0);
    CHECK(elden_charge_votes_receive(&s,data,548)==1&&s.tables==4&&s.voters==3);
    for(unsigned cycle=1;cycle<=2;++cycle){
        for(unsigned i=0;i<4;++i){
            CHECK(elden_charge_votes_next(&s,request)==16&&get(request+8)==0x17&&get(request+12)==i);
            CHECK(!elden_charge_votes_next(&s,request));item(data,i);
            CHECK(elden_charge_votes_receive(&s,data,192)==(i==3?2:1));
        }
        CHECK(s.ready&&s.cycle==cycle&&!strcmp(s.names[3],"iusb0_tot")&&!strcmp(s.clients[2],"i_sdp"));
        CHECK(s.votes[3].effective==500&&s.votes[3].values[2]==500);
        CHECK(!elden_charge_votes_next(&s,request));elden_charge_votes_refresh(&s);
    }
    CHECK(elden_charge_votes_next(&s,request)==16);elden_charge_votes_timeout(&s);
    CHECK(s.failed&&s.timeouts==1);CHECK(!elden_charge_votes_receive(&s,data,192));
    elden_charge_votes_link(&s,true);CHECK(!elden_charge_votes_next(&s,request));
    elden_charge_votes_link(&s,false);elden_charge_votes_link(&s,true);CHECK(s.epoch==2&&s.tables==0);
    for(unsigned bad=0;bad<6;++bad){
        elden_charge_votes_init(&s);elden_charge_votes_link(&s,true);CHECK(elden_charge_votes_next(&s,request)==16);list(data);
        size_t bytes=548;
        if(bad==0)bytes=547;if(bad==1)put(data+12,13);if(bad==2)put(data+16,33);
        if(bad==3)memset(data+20,'x',12);if(bad==4)data[164]=1;if(bad==5)put(data+12,0);
        CHECK(elden_charge_votes_receive(&s,data,bytes)==-1&&s.failed);
    }
    elden_charge_votes_init(&s);elden_charge_votes_link(&s,true);elden_charge_votes_next(&s,request);list(data);
    put(data,32778);CHECK(!elden_charge_votes_receive(&s,data,548)&&s.pending);
    put(data,32781);CHECK(elden_charge_votes_receive(&s,data,548)==1);elden_charge_votes_next(&s,request);item(data,1);
    CHECK(elden_charge_votes_receive(&s,data,192)==-1&&s.failed);
    printf("ChargeVotes: %u checks passed\n",checks);return 0;
}
