/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define main legacy_usb_icl_main
#include "TestUsbIcl.c"
#undef main
int main(void){
 uint8_t out[24];EldenUsbIcl s;EldenChargeRawSnapshot raw=baseline();EldenUcsiSnapshot u=ports();
 elden_usb_icl_init(&s);elden_usb_icl_sdp_only(&s,true);elden_usb_icl_link(&s,true);
 CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24);CHECK(ack(&s,0)==1);
 raw.cycle=8;raw.value[EldenChargeUsbLimit].raw=500000;
 /* Captured v57 transition: adapter1,real6,mixed epoch-consistent cycle. */
 raw.value[EldenChargeUsbRealType].raw=6;unsigned sent=s.requests;
 CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)&&s.deferred);
 CHECK(s.requests==sent&&s.applied==500000&&s.owns_limit&&!s.pending);
 raw.value[EldenChargeUsbAdapter].raw=6;raw.value[EldenChargeUsbLimit].raw=3000000;
 for(unsigned type=6;type<=8;type++){
  raw.value[EldenChargeUsbAdapter].raw=type;raw.value[EldenChargeUsbRealType].raw=type;
  for(unsigned i=0;i<4;i++){uint32_t budget[]={500000,100000,2000,0};
   CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,budget[i],out)&&s.deferred&&s.requests==sent);
  }
 }
 raw=baseline();raw.cycle=12;raw.value[EldenChargeUsbLimit].raw=500000;
 CHECK(!elden_usb_icl_next(&s,&raw,&u,false,true,500000,out)&&s.deferred);
 EldenUcsiSnapshot stale=u;stale.epoch++;CHECK(!elden_usb_icl_next(&s,&raw,&stale,true,true,500000,out));
 raw.valid^=1;CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,100000,out));raw.valid^=1;
 CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,100000,out)==24&&!s.deferred&&get(out+20)==100000);CHECK(ack(&s,0)==1);
 CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,2000,out)==24&&get(out+20)==2000);CHECK(ack(&s,0)==1);
 CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24&&get(out+20)==500000);CHECK(ack(&s,0)==1);
 elden_usb_icl_suspend(&s,true);CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24&&get(out+20)==0);CHECK(ack(&s,0)==1);
 elden_usb_icl_suspend(&s,false);CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24&&get(out+20)==500000);CHECK(ack(&s,0)==1);
 raw.value[EldenChargeTemperature].signed_value=5000;
 CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24&&get(out+20)==100000);CHECK(ack(&s,0)==1);
 elden_usb_icl_init(&s);elden_usb_icl_sdp_only(&s,true);elden_usb_icl_link(&s,true);raw=baseline();
 raw.value[EldenChargeUsbLimit].raw=0;CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)&&!s.owns_limit);
 raw.value[EldenChargeUsbLimit].raw=900000;CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)&&!s.owns_limit);
 raw.value[EldenChargeUsbLimit].raw=100000;CHECK(elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)==24);elden_usb_icl_timeout(&s);
 CHECK(!elden_usb_icl_next(&s,&raw,&u,true,true,500000,out)&&s.quarantined);
 printf("UsbHandoff: %u checks; mixed/PD/stale deferred,SDP budget/thermal/suspend and ownership preserved\n",checks);return 0;
}
