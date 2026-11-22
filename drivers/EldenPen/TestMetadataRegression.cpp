/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Delivery.h"
#if !ORIGINAL
#include "NativeRoute.h"
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
static unsigned enabled=1,stopped,reports,lifts;
static uint64_t clock_us;
static EldenPenLimits limits={19040,30400,4095,60};
#if ORIGINAL
static EldenPenDelivery delivery;
#define DELIV delivery
#else
static EldenPenNativeRoute route;
static int routeBound;
#define DELIV route.bridge.delivery
#endif
static uint64_t nowUs(){return clock_us;}
static int sendPen(void *,const EldenPenFrame *f){++reports;if(!f->tip&&!f->in_range)++lifts;return 0;}
#include "ActualPacket.inc"
static int feed(uint8_t *packet){
#if ORIGINAL
    return elden_pen_native_packet(packet,109);
#else
    /* The actual callback now forwards into the real worker route; bind it
     * once with the harness's real send/clock and this run's owner token. */
    if(!routeBound){assert(!elden_pen_route_begin(&route,&limits,sendPen,nullptr,"oem-metadata",7,7));
        route.bridge.delivery.stale_us=100;routeBound=1;}
    return elden_pen_native_packet(packet,109,7);
#endif
}
int main(){
#if ORIGINAL
    assert(!elden_pen_delivery_init(&delivery,100));
#endif
    uint8_t packet[109]={0};packet[66]=1;packet[68]=1;packet[70]=2;packet[72]=1;clock_us=1;
    assert(!feed(packet));assert(DELIV.accepted.tip&&reports==1);
    packet[66]=0xf1;clock_us=50;int rc=feed(packet);
#if ORIGINAL
    assert(rc==EldenPenUnsupported&&reports==2&&lifts==1&&!DELIV.accepted.tip);
#else
    assert(rc==0&&reports==1&&lifts==0&&DELIV.accepted.tip);
#endif
    clock_us=101;feed(packet);assert(!DELIV.accepted.tip&&lifts==1);
    packet[66]=1;clock_us=102;assert(!feed(packet));
    packet[1]=1;clock_us=103;rc=feed(packet);
#if ORIGINAL
    assert(rc==0&&DELIV.accepted.tip);
#else
    assert(rc==EldenPenEnvelope&&!DELIV.accepted.tip);
#endif
    std::puts(ORIGINAL?"REPRODUCED: original native path lifts on F1 and admits a bad envelope":"PASS: actual native packet path preserves contact on F1, expires metadata, and rejects bad envelope");
}
