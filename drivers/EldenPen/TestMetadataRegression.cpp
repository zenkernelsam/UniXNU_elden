/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Delivery.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static unsigned enabled=1,stopped,reports,lifts;
static uint64_t clock_us;
static EldenPenLimits limits={19040,30400,4095,60};
static EldenPenDelivery delivery;
static uint64_t nowUs(){return clock_us;}
static int sendPen(void *,const EldenPenFrame *f){++reports;if(!f->tip&&!f->in_range)++lifts;return 0;}
#include "ActualPacket.inc"
int main(){
    assert(!elden_pen_delivery_init(&delivery,100));
    uint8_t packet[109]={0};packet[66]=1;packet[68]=1;packet[70]=2;packet[72]=1;clock_us=1;
    assert(!elden_pen_native_packet(packet,109));assert(delivery.accepted.tip&&reports==1);
    packet[66]=0xf1;clock_us=50;int rc=elden_pen_native_packet(packet,109);
#if ORIGINAL
    assert(rc==EldenPenUnsupported&&reports==2&&lifts==1&&!delivery.accepted.tip);
#else
    assert(rc==0&&reports==1&&lifts==0&&delivery.accepted.tip);
#endif
    clock_us=101;elden_pen_native_packet(packet,109);assert(!delivery.accepted.tip&&lifts==1);
    packet[66]=1;clock_us=102;assert(!elden_pen_native_packet(packet,109));
    packet[1]=1;clock_us=103;rc=elden_pen_native_packet(packet,109);
#if ORIGINAL
    assert(rc==0&&delivery.accepted.tip);
#else
    assert(rc==EldenPenEnvelope&&!delivery.accepted.tip);
#endif
    std::puts(ORIGINAL?"REPRODUCED: original native path lifts on F1 and admits a bad envelope":"PASS: actual native packet path preserves contact on F1, expires metadata, and rejects bad envelope");
}
