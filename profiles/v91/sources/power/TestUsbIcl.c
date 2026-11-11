/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UsbIcl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}} while(0)
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static EldenChargeRawSnapshot baseline(void){
    EldenChargeRawSnapshot r={0};r.epoch=1;r.cycle=4;r.valid=(UINT64_C(1)<<EldenChargeFieldCount)-1;
    unsigned ones[]={EldenChargeUsbOnline,EldenChargeUsbAdapter,EldenChargeUsbRealType,EldenChargePresent,EldenChargeHealth,EldenChargeEnabled};
    for(unsigned i=0;i<sizeof(ones)/sizeof(ones[0]);++i)r.value[ones[i]].raw=1;
    r.value[EldenChargeVoltage].raw=r.value[EldenChargeFg1Voltage].raw=r.value[EldenChargeFg2Voltage].raw=3900000;
    r.value[EldenChargeTemperature].signed_value=3400;r.value[EldenChargeUsbLimit].raw=100000;r.value[EldenChargeUsbMax].raw=900000;
    return r;
}
static EldenUcsiSnapshot ports(void){
    EldenUcsiSnapshot u={0};u.epoch=1;u.connectors=2;
    u.port[0].status_valid=u.port[0].connected=u.port[0].usb=true;u.port[0].power_mode=1;return u;
}
static int ack(EldenUsbIcl *s,uint32_t ret){
    uint8_t a[24]={0};put(a,32778);put(a+4,1);put(a+8,0x33);put(a+12,5);put(a+20,ret);
    return elden_usb_icl_receive(s,a,sizeof(a));
}
int main(void){
    uint8_t out[24];EldenUsbIcl s;EldenChargeRawSnapshot r=baseline();EldenUcsiSnapshot u=ports();
    elden_usb_icl_init(&s);CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));elden_usb_icl_link(&s,true);
    for(unsigned fault=0;fault<15;++fault){
        EldenChargeRawSnapshot q=r;EldenUcsiSnapshot v=u;bool fresh=true,notified=true;uint32_t budget=500000;
        switch(fault){
        case 0:fresh=false;break;case 1:notified=false;break;case 2:budget=100000;break;
        case 3:q.valid^=1;break;case 4:q.epoch++;break;case 5:v.epoch++;break;
        case 6:v.port[0].source=true;break;case 7:v.port[0].power_mode=3;break;
        case 8:q.value[EldenChargeInputSuspended].raw=1;break;
        case 9:q.value[EldenChargeTemperature].signed_value=5000;break;
        case 10:q.value[EldenChargeFg2Voltage].raw=0;break;
        case 11:q.value[EldenChargeUsbLimit].raw=900000;break;
        case 12:q.cycle=1;break;case 13:q.value[EldenChargeUsbAdapter].raw=6;break;
        case 14:q.value[EldenChargeVoltage].raw=3400000;break;
        }
        CHECK(!elden_usb_icl_next(&s,&q,&v,fresh,notified,budget,out));CHECK(!s.pending&&!s.owns_limit);
    }
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);
    CHECK(get(out)==32778&&get(out+4)==1&&get(out+8)==0x33&&get(out+12)==0&&get(out+16)==5&&get(out+20)==500000);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));CHECK(ack(&s,0)==1&&s.applied==500000);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,2000,out)==24&&get(out+20)==2000);CHECK(ack(&s,0)==1);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);CHECK(ack(&s,0)==1);
    CHECK(elden_usb_icl_next(&s,&r,&u,false,true,500000,out)==24&&get(out+20)==100000);CHECK(ack(&s,0)==1);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);elden_usb_icl_timeout(&s);
    CHECK(s.quarantined&&s.owns_limit&&s.timeouts==1);CHECK(ack(&s,0)==0);
    elden_usb_icl_link(&s,true);CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    elden_usb_icl_link(&s,false);elden_usb_icl_link(&s,true);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24&&get(out+20)==100000);CHECK(ack(&s,0)==1);
    r.epoch=u.epoch=2;CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);CHECK(ack(&s,7)==-1&&s.quarantined);
    r=baseline();u=ports();r.value[EldenChargeEnabled].raw=0;
    elden_usb_icl_init(&s);elden_usb_icl_link(&s,true);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24&&get(out+20)==500000);
    CHECK(ack(&s,0)==1);CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.value[EldenChargeEnabled].raw=1;CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.value[EldenChargeUsbLimit].raw=500000;elden_usb_icl_suspend(&s,true);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24&&get(out+20)==0);
    CHECK(ack(&s,0)==1&&s.applied==0&&s.owns_limit);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    elden_usb_icl_suspend(&s,false);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24&&get(out+20)==500000);CHECK(ack(&s,0)==1);
    elden_usb_icl_suspend(&s,true);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);CHECK(ack(&s,0)==1);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,2000,out)==24&&get(out+20)==2000);CHECK(ack(&s,0)==1);
    elden_usb_icl_link(&s,false);CHECK(!s.suspend_requested);
    elden_usb_icl_init(&s);elden_usb_icl_link(&s,true);elden_usb_icl_suspend(&s,true);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out)); /* foreign500mA never paused */
    r=baseline();u=ports();elden_usb_icl_init(&s);elden_usb_icl_link(&s,true);
    CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);CHECK(ack(&s,0)==1);
    r.cycle=5;CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out)); /* possibly in-flight before ACK */
    r.cycle=6;CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24&&get(out+20)==500000);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));CHECK(ack(&s,0)==1);
    CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.cycle=7;r.value[EldenChargeUsbLimit].raw=500000;CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.cycle=10;r.value[EldenChargeUsbLimit].raw=0;CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.value[EldenChargeUsbLimit].raw=300000;CHECK(!elden_usb_icl_next(&s,&r,&u,true,true,500000,out));
    r.value[EldenChargeUsbLimit].raw=2000;CHECK(elden_usb_icl_next(&s,&r,&u,true,true,500000,out)==24);CHECK(ack(&s,0)==1);
    printf("UsbIcl: %u checks passed\n",checks);return 0;
}
