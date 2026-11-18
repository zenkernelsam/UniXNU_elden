/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Mailbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenHapticsRequest req(uint64_t id){EldenHapticsRequest r={1,2,id,10000,10000,3,1,{0,0},0};return r;}
int main(void){
    EldenHapticsMailbox m={0};CHECK(!elden_haptics_mail_open(&m,1,2));CHECK(elden_haptics_mail_open(&m,2,3)==-16);
    for(unsigned i=1;i<=32;i++){EldenHapticsRequest r=req(i);CHECK(!elden_haptics_mail_submit(&m,&r,16384));}
    EldenHapticsRequest r=req(33),taken;uint16_t intensity=0;CHECK(elden_haptics_mail_submit(&m,&r,16384)==-16);
    EldenHapticsResult out,before;memset(&out,0xa5,sizeof(out));before=out;
    CHECK(elden_haptics_mail_result(&m,1,2,1,&out)==-16&&!memcmp(&out,&before,sizeof(out)));
    for(unsigned i=1;i<=32;i++){
        CHECK(!elden_haptics_mail_take(&m,&taken,&intensity)&&taken.id==i);
        EldenHapticsResult done={1,2,i,EldenHapticsComplete,0};CHECK(!elden_haptics_mail_finish(&m,&done));
    }
    CHECK(!elden_haptics_mail_work(&m));CHECK(elden_haptics_mail_submit(&m,&r,16384)==-16); // receipts reserve every slot
    for(unsigned i=1;i<=32;i++)CHECK(!elden_haptics_mail_result(&m,1,2,i,&out)&&out.id==i&&out.state==EldenHapticsComplete);
    r=req(1);CHECK(elden_haptics_mail_submit(&m,&r,16384)==-116);r=req(33);CHECK(!elden_haptics_mail_submit(&m,&r,16384));
    CHECK(!elden_haptics_mail_cancel(&m,1,2,33));CHECK(!elden_haptics_mail_result(&m,1,2,33,&out)&&out.state==EldenHapticsCancelled);
    r=req(34);CHECK(!elden_haptics_mail_submit(&m,&r,16384)&&!elden_haptics_mail_take(&m,&taken,&intensity));
    CHECK(!elden_haptics_mail_close(&m,1,2)&&m.closing);CHECK(elden_haptics_mail_open(&m,3,4)==-16);
    CHECK(!elden_haptics_mail_next_cancel(&m,&taken)&&taken.id==34);
    CHECK(elden_haptics_mail_next_cancel(&m,&taken)==-16);
    EldenHapticsResult stopped={1,2,34,EldenHapticsCancelled,0};CHECK(!elden_haptics_mail_finish(&m,&stopped)&&!m.owner&&!m.closing);
    CHECK(!elden_haptics_mail_open(&m,3,4));r=req(35);CHECK(elden_haptics_mail_submit(&m,&r,16384)==-13);
    stopped.owner=3;CHECK(elden_haptics_mail_finish(&m,&stopped)==-116);
    CHECK(elden_haptics_mail_result(&m,1,2,34,&out)==-13);
    CHECK(!elden_haptics_mail_close(&m,3,4));CHECK(!elden_haptics_mail_open(&m,1,2));
    r=req(1);r.gain[0]=64;CHECK(elden_haptics_mail_submit(&m,&r,16384)==-22);r.gain[0]=0;CHECK(!elden_haptics_mail_submit(&m,&r,16384));
    elden_haptics_mail_stop(&m);CHECK(!m.owner&&!elden_haptics_mail_work(&m));CHECK(elden_haptics_mail_open(&m,1,2)==-13);
    // Exhaust every possible active slot on disconnect; no active result can
    // be discarded until the actual queue callback confirms its stop outcome.
    for(unsigned active=0;active<32;active++){
        memset(&m,0,sizeof(m));CHECK(!elden_haptics_mail_open(&m,1,2));
        for(unsigned i=1;i<=32;i++){r=req(i);CHECK(!elden_haptics_mail_submit(&m,&r,16384));}
        for(unsigned i=0;i<=active;i++){CHECK(!elden_haptics_mail_take(&m,&taken,&intensity));if(i<active){stopped=(EldenHapticsResult){1,2,i+1,EldenHapticsComplete,0};CHECK(!elden_haptics_mail_finish(&m,&stopped));}}
        CHECK(!elden_haptics_mail_close(&m,1,2)&&m.closing&&elden_haptics_mail_work(&m));
        CHECK(elden_haptics_mail_open(&m,3,4)==-16);CHECK(!elden_haptics_mail_next_cancel(&m,&taken)&&taken.id==active+1);
        stopped=(EldenHapticsResult){1,2,active+1,EldenHapticsCancelled,0};CHECK(!elden_haptics_mail_finish(&m,&stopped)&&!m.closing&&!m.owner);
    }
    printf("PASS: %u actual mailbox checks; owner, reserved receipts, monotonically increasing IDs, close/cancel and stale outputs\n",checks);
}
