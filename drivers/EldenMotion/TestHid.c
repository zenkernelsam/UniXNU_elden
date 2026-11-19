/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    for(unsigned kind=1;kind<=2;kind++){
        uint32_t length;const uint8_t *p=elden_motion_hid_descriptor(kind,&length);const EldenMotionHidKind *k=elden_motion_hid_kind(kind);
        CHECK(p&&length==60&&k&&k->page==0xff00&&k->usage==(kind==1?3:9));
        CHECK(!memcmp(p,(uint8_t[]){6,0,255,9,(uint8_t)k->usage,161,1},7)&&p[length-1]==0xc0);
        EldenMotionSample s={{1,kind},19200000,{65536,-1234,5678},kind,kind==1?EldenMotionG:EldenMotionRadiansPerSecond,3};
        for(unsigned sign=0;sign<8;sign++){
            EldenMotionHidEvent e={0};CHECK(!elden_motion_hid_event(&s,sign,1,&e));
            CHECK(e.type==(kind==1?13:20)&&e.timestamp_ticks==s.timestamp_ticks&&e.sequence==1);
            for(unsigned axis=0;axis<3;axis++)CHECK(e.q16[axis]==(sign&(1u<<axis)?-s.q16[axis]:s.q16[axis]));
        }
        EldenMotionHidEvent e,old;memset(&e,0xa5,sizeof(e));old=e;s.q16[0]=INT32_MIN;
        CHECK(elden_motion_hid_event(&s,1,1,&e)==-22&&!memcmp(&old,&e,sizeof(e)));s.q16[0]=0;
        s.unit=EldenMotionMetersPerSecond2;CHECK(elden_motion_hid_event(&s,0,1,&e)==-22&&!memcmp(&old,&e,sizeof(e)));
    }
    uint32_t hz=7;CHECK(!elden_motion_hid_interval(0,&hz)&&hz==0);CHECK(!elden_motion_hid_interval(40000,&hz)&&hz==25);
    CHECK(!elden_motion_hid_interval(2500,&hz)&&hz==400);CHECK(!elden_motion_hid_interval(33333,&hz)&&hz==31);
    hz=9;CHECK(elden_motion_hid_interval(2499,&hz)==-22&&hz==9);CHECK(elden_motion_hid_interval(UINT64_MAX,&hz)==-22&&hz==9);
    CHECK(!elden_motion_hid_kind(0)&&!elden_motion_hid_kind(3));
    printf("PASS: %u actual motion HID descriptor/units/events/demand checks; no physical sensor samples\n",checks);return 0;
}
