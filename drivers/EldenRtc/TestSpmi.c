/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Spmi.h"
#include "TransactionGate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t map[896],owner[896],status,data,command,reads,writes;unsigned timeout,fail;} Model;
static int read32(void *p,unsigned window,uint32_t reg,uint32_t *out){
    Model *m=p;if(++m->reads==m->fail)return -71;
    if(window==EldenRtcSpmiCore){if(reg==0)*out=0x70020000;else if(reg==4)*out=896;else *out=m->map[(reg-0x2000)/4];}
    else if(window==EldenRtcSpmiOwner)*out=m->owner[reg/4];
    else if(reg==8)*out=m->timeout?0:m->status;else{assert(reg==0x18);*out=m->data;}
    return 0;
}
static int write32(void *p,unsigned window,uint32_t reg,uint32_t value){Model *m=p;assert(window==EldenRtcSpmiObserver&&reg==0);m->writes++;m->command=value;return 0;}
static void delay(void *p,unsigned us){(void)p;assert(us==1);}
static void setup(Model *m,EldenRtcSpmi *r){
    memset(m,0,sizeof(*m));memset(r,0,sizeof(*r));m->map[31]=0x061<<8|1;m->status=1;m->data=0x12345678;
    r->io=(EldenRtcSpmiIo){m,read32,write32,delay};assert(!elden_rtc_spmi_select(r)&&r->apid==31);
}
int main(void){
    Model m;EldenRtcSpmi r;uint8_t bytes[4]={1,2,3,4};setup(&m,&r);
    assert(!elden_rtc_spmi_read(&r,0x6148,bytes,4)&&bytes[0]==0x78&&bytes[3]==0x12&&m.command==((1u<<27)|(0x48<<4)|3));
    assert(!elden_spmi_transaction_acquire(EldenSpmiAdcOwner,&m,delay));unsigned writes=m.writes;
    assert(elden_rtc_spmi_read(&r,0x6148,bytes,4)==-16&&m.writes==writes);
    assert(!elden_spmi_transaction_release(EldenSpmiAdcOwner,false));
    const uint8_t before[4]={1,2,3,4};memcpy(bytes,before,4);m.status=9;
    assert(elden_rtc_spmi_read(&r,0x6148,bytes,4)==-5&&!memcmp(bytes,before,4)&&elden_spmi_transaction_state()==0&&!r.uncertain);
    m.status=1;m.map[31]=0x071<<8|1;assert(elden_rtc_spmi_read(&r,0x6148,bytes,4)==-16);m.map[31]=0x061<<8|1;
    assert(elden_rtc_spmi_read(&r,0x6140,bytes,4)==-22);assert(elden_rtc_spmi_read(&r,0x6248,bytes,4)==-22);
    m.timeout=1;assert(elden_rtc_spmi_read(&r,0x6148,bytes,4)==-110&&r.uncertain&&elden_spmi_transaction_state()==UINT32_MAX);
    assert(elden_spmi_transaction_acquire(EldenSpmiBtOwner,&m,delay)==-5);assert(!memcmp(bytes,before,4));
    puts("PASS: RTC actual SPMI callback, read-only whitelist, shared ADC/WCN gate, identity change, DONE error, timeout poisoning");
}
