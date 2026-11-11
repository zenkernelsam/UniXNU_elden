/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargePoll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t word(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static void answer(EldenChargeQuery q,uint32_t raw,uint32_t error,uint8_t out[24]) {
    memset(out,0,24);put(out,32778);put(out+4,1);put(out+8,q.opcode);
    put(out+12,q.property);put(out+16,raw);put(out+20,error);
}
static void cycle(bool extended,int fail) {
    EldenChargePoll p;elden_charge_poll_init(&p,extended);uint8_t bytes[24],reply[24];
    CHECK(elden_charge_poll_next(&p,bytes)==0);
    elden_charge_poll_link(&p,true);unsigned count=extended?EldenChargeFieldCount:EldenChargeUsbAdapter+1;
    for(unsigned field=0;field<count;field++) {
        CHECK(p.cursor==field && !p.pending);
        CHECK(elden_charge_poll_next(&p,bytes)==24 && p.pending);
        CHECK(word(bytes)==32778 && word(bytes+4)==1 && word(bytes+12)==0 && word(bytes+20)==0);
        CHECK(word(bytes+8)==0x30 || word(bytes+8)==0x32 || word(bytes+8)==0x60);
        if(field==EldenChargeTemperature)CHECK(word(bytes+8)==0x30 && word(bytes+16)==12);
        if(field==EldenChargeFg1Voltage)CHECK(word(bytes+8)==0x60 && word(bytes+16)==16);
        CHECK(elden_charge_poll_next(&p,bytes)==0 && p.committed.valid==0);
        EldenChargeQuery q=elden_charge_poll_query(field);answer(q,field==EldenChargeCurrent?0xfffffc18u:field+100,field==(unsigned)fail?5:0,reply);
        CHECK(elden_charge_poll_receive(&p,reply,24)==(field+1==count?EldenChargePollCycle:EldenChargePollAnswer));
    }
    uint64_t valid=(UINT64_C(1)<<count)-1;if(fail>=0)valid&=~(UINT64_C(1)<<fail);
    CHECK(p.committed.valid==valid && p.cycles==1 && p.responses==count-(fail>=0));
    CHECK(p.committed.value[EldenChargeCurrent].signed_value==-1000);
    CHECK(!p.active && !p.pending && p.committed.epoch==1);
    elden_charge_poll_refresh(&p);CHECK(p.active && !p.working.valid && p.committed.valid==valid);
    CHECK(elden_charge_poll_next(&p,bytes)==24);
    uint8_t same[24];memcpy(same,bytes,24);elden_charge_poll_unsent(&p);
    CHECK(elden_charge_poll_next(&p,bytes)==24 && !memcmp(bytes,same,24));
    answer(elden_charge_poll_query(0),0,0,reply);put(reply+12,1);
    CHECK(elden_charge_poll_receive(&p,reply,24)==0 && p.pending && p.cursor==0);
    elden_charge_poll_timeout(&p);CHECK(p.quarantined && !p.committed.valid && p.timeouts==1);
    elden_charge_poll_refresh(&p);CHECK(elden_charge_poll_next(&p,bytes)==0);
    elden_charge_poll_link(&p,true);CHECK(p.quarantined); /* duplicate up never clears timeout */
    elden_charge_poll_link(&p,false);elden_charge_poll_link(&p,true);
    CHECK(!p.quarantined && p.epoch==2 && p.active && !p.committed.valid);
    CHECK(elden_charge_poll_next(&p,bytes)==24);
    answer(elden_charge_poll_query(0),1,0,reply);
    CHECK(elden_charge_poll_receive(&p,reply,20)==EldenChargePollStopped && !p.committed.valid);
}
int main(void) {
    cycle(false,-1);cycle(true,EldenChargeUsb2Online);
    EldenChargePoll p;elden_charge_poll_init(&p,true);elden_charge_poll_link(&p,true);
    uint8_t note[16]={0};put(note,32778);put(note+4,2);put(note+8,7);
    CHECK(elden_charge_poll_receive(&p,note,16)==EldenChargePollNotified && p.refresh_wanted && p.notifications==1);
    put(note+4,1);CHECK(elden_charge_poll_receive(&p,note,16)==0 && p.notifications==1);
    uint8_t request[24],reply[24];
    for(unsigned i=0;i<EldenChargeFieldCount;i++) {
        CHECK(elden_charge_poll_next(&p,request)==24);answer(elden_charge_poll_query(i),100,0,reply);
        elden_charge_poll_receive(&p,reply,24);
    }
    CHECK(p.cycles==1 && p.active && p.cursor==0 && !p.working.valid);
    CHECK(p.committed.valid==(UINT64_C(1)<<EldenChargeFieldCount)-1);
    printf("PASS: %u checks; stock-property GET cycles, coherent commits, timeout quarantine and fresh epochs\n",checks);
    return 0;
}
