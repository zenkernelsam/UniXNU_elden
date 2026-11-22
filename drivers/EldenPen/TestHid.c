/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "HidReport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"line%d: %s\n",__LINE__,#v);exit(1);}}while(0)
int main(void){
    uint8_t d[256],unchanged[256],r[EldenPenHidBytes],saved[EldenPenHidBytes];size_t bytes=123;
    memset(d,0xa5,sizeof(d));memcpy(unchanged,d,sizeof(d));
    CHECK(elden_pen_hid_descriptor(4095,d,4,&bytes)!=0&&bytes==123&&!memcmp(d,unchanged,sizeof(d)));
    CHECK(!elden_pen_hid_descriptor(4095,d,sizeof(d),&bytes)&&bytes<sizeof(d));
    unsigned maxItems=0,pencil=0;
    for(size_t i=0;i+5<bytes;++i){
        if(d[i]==0x16&&d[i+1]==1&&d[i+2]==0xf0&&d[i+3]==0x26&&d[i+4]==0xff&&d[i+5]==0xf)maxItems++;
        if(d[i]==5&&d[i+1]==0xd&&d[i+2]==9&&d[i+3]==0x20)pencil++;
    }
    CHECK(maxItems==1&&pencil==1);
    CHECK(!elden_pen_hid_descriptor(8191,d,sizeof(d),&bytes));
    maxItems=0;for(size_t i=0;i+5<bytes;++i)if(d[i]==0x16&&d[i+1]==1&&d[i+2]==0xe0&&d[i+3]==0x26&&d[i+4]==0xff&&d[i+5]==0x1f)maxItems++;
    CHECK(maxItems==1);
    EldenPenFrame f={0};f.kind=EldenPenCoordinate;f.x=0x1234;f.y=0x4567;f.pressure=4095;f.tip=1;f.in_range=1;f.buttons=3;f.tilt_x=-60;f.tilt_y=60;
    const uint8_t expected[EldenPenHidBytes]={7,0x33,0x34,0x12,0x67,0x45,0xff,0xf,0xc4,0x3c,0xe7,1,0xc4,0x3c,0x34,0x12,0x67,0x45};
    CHECK(!elden_pen_hid_report(&f,4095,r)&&!memcmp(r,expected,sizeof(r)));
    memcpy(saved,r,sizeof(r));f.pressure=4096;CHECK(elden_pen_hid_report(&f,4095,r)!=0&&!memcmp(saved,r,sizeof(r)));
    f.pressure=0;f.tip=0;f.buttons=0;CHECK(!elden_pen_hid_report(&f,4095,r)&&r[1]==0x10);
    /* Hover with a barrel button: in-range and the first button bit. */
    f.buttons=1;CHECK(!elden_pen_hid_report(&f,4095,r)&&r[1]==0x12);
    /* Second barrel bit reports on bit5 without inventing an eraser. */
    f.buttons=2;CHECK(!elden_pen_hid_report(&f,4095,r)&&r[1]==0x30);
    /* A button-only frame is emitted as-is — flags carry the button alone. */
    f.in_range=0;f.buttons=1;CHECK(!elden_pen_hid_report(&f,4095,r)&&r[1]==0x02);
    /* Report boundary: tilt beyond ±60 is refused, as is a tip with no
     * in-range, or a Leave that still claims contact. */
    f.buttons=0;f.tilt_x=61;CHECK(elden_pen_hid_report(&f,4095,r)==EldenPenRange);
    f.tilt_x=-61;CHECK(elden_pen_hid_report(&f,4095,r)==EldenPenRange);
    f.tilt_x=0;f.tip=1;f.pressure=100;CHECK(elden_pen_hid_report(&f,4095,r)==EldenPenRange);
    f.tip=0;f.pressure=0;f.in_range=0;f.kind=EldenPenLeave;CHECK(!elden_pen_hid_report(&f,4095,r)&&r[1]==0);
    f.tip=1;CHECK(elden_pen_hid_report(&f,4095,r)==EldenPenRange);
    f.tip=0;f.kind=EldenPenIdentity;CHECK(elden_pen_hid_report(&f,4095,r)==EldenPenUnsupported);
    int32_t scaled=42;CHECK(elden_pen_pressure_fixed(-1,&scaled)!=0&&scaled==42);
    CHECK(elden_pen_pressure_fixed(65537,&scaled)!=0&&scaled==42);
    CHECK(!elden_pen_pressure_fixed(0,&scaled)&&scaled==0);
    CHECK(!elden_pen_pressure_fixed(32768,&scaled)&&scaled==32768000);
    CHECK(!elden_pen_pressure_fixed(65536,&scaled)&&scaled==65536000);
    printf("PASS: %u HID encoding and fixed-pressure checks; no OS/HID acceptance claim\n",checks);return 0;
}
