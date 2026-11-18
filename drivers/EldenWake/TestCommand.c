/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../EldenNvt36536/Protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {unsigned calls,fail;uint8_t wanted;} Peer;
static int transfer(void *v,const uint8_t *tx,uint8_t *rx,size_t n){
    Peer *p=v;CHECK(!rx);p->calls++;
    if(p->calls==1)CHECK(n==3&&tx[0]==255&&tx[1]==(ELDEN_NVT_EVENT>>15)&&tx[2]==((ELDEN_NVT_EVENT>>7)&255));
    if(p->calls==2)CHECK(n==2&&tx[0]==0xd0&&tx[1]==p->wanted);
    return p->calls==p->fail?-5:0;
}
static void delay(void *v,unsigned us){(void)v;(void)us;CHECK(0);}
int main(void){
    uint8_t tx[ELDEN_NVT_FW_CHUNK+1],rx[110];
    for(unsigned mode=0;mode<2;mode++)for(unsigned fault=0;fault<3;fault++){
        Peer p={0,fault,mode?0x13:0x11};EldenNvtDevice d={0};d.context=&p;d.transfer=transfer;d.delay_us=delay;d.tx=tx;d.rx=rx;
        d.tx_bytes=sizeof(tx);d.rx_bytes=sizeof(rx);d.identified=d.ready=true;d.pid=0x6097;
        int r=elden_nvt_low_power_command(&d,mode!=0);CHECK(r==(fault?-5:0));
        if(fault){CHECK(d.poisoned&&!d.ready);unsigned calls=p.calls;CHECK(elden_nvt_low_power_command(&d,true)!=0&&p.calls==calls);}
        else CHECK(p.calls==2&&d.ready&&!d.poisoned);
    }
    Peer p={0};EldenNvtDevice d={0};d.context=&p;d.transfer=transfer;d.delay_us=delay;d.tx=tx;d.rx=rx;d.tx_bytes=sizeof(tx);d.rx_bytes=sizeof(rx);
    CHECK(elden_nvt_low_power_command(&d,true)==-22&&!p.calls);
    d.identified=d.ready=true;d.pid=0x6098;CHECK(elden_nvt_low_power_command(&d,true)==-22&&!p.calls);
    printf("PASS: %u actual NVT low-power transfer checks; exact page/50:11/50:13, failure poisoning and no reset/IRQ writes\n",checks);
}
