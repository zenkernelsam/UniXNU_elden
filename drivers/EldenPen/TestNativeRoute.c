/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY route qualification: exercises the actual elden_pen_route_*
 * push-model edge the serialized NT36536 worker uses — staged packet, real
 * caller frame-owner identity, monotonic timestamp/sequence — through the
 * bound owner facade and existing delivery to a send peer. The send peer,
 * caller identities and clock are test doubles; no real NT36536 SPI read,
 * AP501U, HID service or Pencil acceptance is present or claimed. */
#include "NativeRoute.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenPenLimits limits={19040,30400,4095,60};
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
static void coord(uint8_t *p,unsigned pressure){
    memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);
}
static void leave(uint8_t *p){memset(p,0,109);p[66]=0xff;}
typedef struct {unsigned count,fail,stop_at;EldenPenNativeRoute *route;EldenPenFrame trace[64];} Sink;
static int send(void *p,const EldenPenFrame *f){
    Sink *s=p;
    if(s->fail){--s->fail;return -99;}
    CHECK(s->count<64);s->trace[s->count++]=*f;
    /* Callback-reentrant stop: the owner's deferred-stop path must carry it. */
    if(s->stop_at&&s->count==s->stop_at){s->stop_at=0;elden_pen_route_stop(s->route,900);}
    return 0;
}
static uint8_t buf[109];
static void setup(EldenPenNativeRoute *r,Sink *s){
    memset(r,0,sizeof(*r));memset(s,0,sizeof(*s));s->route=r;
}
static void begin(EldenPenNativeRoute *r,Sink *s){
    CHECK(!elden_pen_route_begin(r,&limits,send,s,"elden-pen-native",7,7));
    CHECK(r->armed&&r->gen);
}
static int released(EldenPenNativeRoute *r){
    EldenPenOwnerStatus st;
    return elden_pen_route_status(r,&st)?0:st.delivery_released;
}
int main(void){
    /* 1. Default-off: a pushed packet before any begin is refused by the
     * owner — nothing reaches decode, delivery or send. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);coord(buf,300);
     CHECK(elden_pen_route_packet(&r,buf,109,7,100)==EldenPenNoProvider);
     CHECK(!s.count&&!r.staged.live);   /* refused before staging */
     CHECK(elden_pen_route_packet(&r,buf,109,7,100)==EldenPenNoProvider);}

    /* 2. Argument fencing on both the entry and begin. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);coord(buf,300);
     CHECK(elden_pen_route_packet(0,buf,109,7,100)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_packet(&r,0,109,7,100)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_packet(&r,buf,109,0,100)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(0,&limits,send,&s,"n",7,7)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(&r,0,send,&s,"n",7,7)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(&r,&limits,0,&s,"n",7,7)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(&r,&limits,send,&s,0,7,7)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(&r,&limits,send,&s,"",7,7)==EldenPenDeliveryArgument);
     CHECK(elden_pen_route_begin(&r,&limits,send,&s,"n",0,7)==EldenPenDeliveryArgument);
     CHECK(!r.armed&&!r.gen);}

    /* 3. The real edge: begin -> push -> owner pulls the exact staged packet
     * -> decode -> delivery -> send. Decoded fields match the packet bytes. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);
     CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     CHECK(s.count==1&&s.trace[0].tip&&s.trace[0].in_range);
     CHECK(s.trace[0].raw_x==9520&&s.trace[0].raw_y==15200&&s.trace[0].pressure==300);
     CHECK(!r.staged.live);                                  /* consumed once */
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.bound&&!st.busy&&st.packets==1&&st.decoded==1&&st.transport_delivered==1);
     CHECK(st.owner==(uint64_t)(uintptr_t)&r&&st.frame_owner==7&&st.last_seq==1);
     /* The same caller identity and the next real sequence keep flowing. */
     coord(buf,400);CHECK(!elden_pen_route_packet(&r,buf,109,7,110));
     CHECK(!elden_pen_route_status(&r,&st)&&st.packets==2&&st.last_seq==2);}

    /* 4. F1 metadata: counted, never a contact, existing tip retained. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     memset(buf,0,109);buf[66]=0xf1;
     CHECK(!elden_pen_route_packet(&r,buf,109,7,110));
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.metadata==1&&st.decoded==2&&s.count==1);}

    /* 5. Foreign frame owner: the packet is refused and a held contact lifts
     * through the existing refusal release. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,400);
     CHECK(elden_pen_route_packet(&r,buf,109,0xdead,110)==EldenPenOwnerForeign);
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.foreign_frames==1&&st.packets==1&&s.count==2&&!s.trace[1].tip);
     /* The next real packet under the bound owner flows again. */
     coord(buf,500);CHECK(!elden_pen_route_packet(&r,buf,109,7,120));}

    /* 6. Timestamp rollback: refused Stale, contact lifted. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,200));
     coord(buf,400);
     CHECK(elden_pen_route_packet(&r,buf,109,7,50)==EldenPenOwnerStale);
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.stale_ts==1&&s.count==2&&!s.trace[1].tip);}

    /* 7. Wrong size: mirrors the old decode-failure path — fault plus refusal,
     * never a packet. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     CHECK(elden_pen_route_packet(&r,buf,108,7,110)==EldenPenDeliveryArgument);
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.faults==1&&s.count==2&&!s.trace[1].tip);}

    /* 8. Explicit transport fault drives the existing fault release. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     CHECK(!elden_pen_route_fault(&r,110));
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.faults==1&&s.count==2&&!s.trace[1].tip);}

    /* 9. Pause/resume: pause lifts and holds, packets refuse Paused, resume
     * restores the same delivery clock. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     CHECK(!elden_pen_route_pause(&r,110));CHECK(released(&r));
     coord(buf,400);
     CHECK(elden_pen_route_packet(&r,buf,109,7,120)==EldenPenPaused);
     CHECK(!elden_pen_route_resume(&r,130));
     CHECK(!elden_pen_route_packet(&r,buf,109,7,140));
     CHECK(s.count==3&&s.trace[2].tip&&s.trace[2].pressure==400);}

    /* 10. Stop drains and retires; a fresh session rebinds a new generation
     * while every old-generation token stays fenced. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     uint64_t old=r.gen;
     CHECK(!elden_pen_route_stop(&r,110));
     CHECK(!r.bridge.started);
     coord(buf,400);
     CHECK(elden_pen_route_packet(&r,buf,109,7,120)==EldenPenOwnerDisabled);
     while(!released(&r))CHECK(!elden_pen_route_release(&r));
     CHECK(!elden_pen_route_retire(&r)&&!r.armed&&!r.gen);
     CHECK(elden_pen_route_packet(&r,buf,109,7,130)==EldenPenNoProvider);
     begin(&r,&s);CHECK(r.gen!=old);
     CHECK(!elden_pen_route_packet(&r,buf,109,7,140));
     CHECK(elden_pen_bridge_packet(&r.bridge,old)==EldenPenOwnerDisabled);
     CHECK(elden_pen_bridge_stop(&r.bridge,old,150)==EldenPenOwnerStale);}

    /* 11. A second begin while armed refuses Busy — one session at a time. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     CHECK(elden_pen_route_begin(&r,&limits,send,&s,"elden-pen-native",7,7)==EldenPenOwnerBusy);}

    /* 12. A failing send leaves the owed lift pending; the provider-free
     * route_release retries until the bound send accepts it. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     leave(buf);s.fail=1;
     CHECK(elden_pen_route_packet(&r,buf,109,7,110)!=0);   /* send failure surfaces */
     CHECK(!released(&r));
     CHECK(!elden_pen_route_release(&r));CHECK(released(&r)&&s.count==2);}

    /* 13. Stop inside the send callback: the owner's deferred path applies it
     * at the unwind — the push reports Stopped and the gate is off. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     s.stop_at=2;
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,400);
     CHECK(elden_pen_route_packet(&r,buf,109,7,110)==EldenPenStopped);
     CHECK(!r.bridge.started);
     while(!released(&r))CHECK(!elden_pen_route_release(&r));
     CHECK(!elden_pen_route_retire(&r));}

    /* 14. tick/fault/pause/resume on an unarmed route refuse through the gate,
     * never touching a binding that does not exist. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     CHECK(elden_pen_route_tick(&r,100)==EldenPenNoProvider);
     CHECK(elden_pen_route_fault(&r,100)==EldenPenNoProvider);
     CHECK(elden_pen_route_pause(&r,100)==EldenPenNoProvider);
     CHECK(elden_pen_route_resume(&r,100)==EldenPenNoProvider);
     CHECK(!elden_pen_route_stop(&r,100));
     CHECK(!elden_pen_route_release(&r));
     CHECK(!elden_pen_route_retire(&r));
     CHECK(!s.count);}

    /* 15. Hover through the route: an in-range-only packet delivers an
     * in-range/no-tip report; a stale hover lifts via route_tick; pause
     * during hover releases through the real path and resume admits the
     * next real packet without replaying the old hover. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,0);put(buf,75,0xffff);                 /* hover: distance set, no pressure */
     CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     CHECK(s.count==1&&!s.trace[0].tip&&s.trace[0].in_range&&s.trace[0].distance==0xffff);
     CHECK(!elden_pen_route_tick(&r,100+99999));
     CHECK(s.count==1);                               /* inside 100ms: still held */
     CHECK(!elden_pen_route_tick(&r,100+100000));
     CHECK(s.count==2&&s.trace[1].kind==EldenPenLeave&&!s.trace[1].in_range);
     CHECK(released(&r));
     /* Pause during hover releases the same way; resume never replays it. */
     coord(buf,0);put(buf,75,0xffff);
     CHECK(!elden_pen_route_packet(&r,buf,109,7,200100));
     CHECK(!elden_pen_route_pause(&r,200110));
     CHECK(released(&r));
     CHECK(elden_pen_route_packet(&r,buf,109,7,200120)==EldenPenPaused);
     CHECK(!elden_pen_route_resume(&r,200130));
     coord(buf,300);
     CHECK(!elden_pen_route_packet(&r,buf,109,7,200140));
     CHECK(s.count==5&&s.trace[4].tip&&s.trace[4].pressure==300);}

    /* 16. Out-of-bounds pen data is refused at decode under the owner — the
     * existing fault path runs, a held contact lifts, and the facade counts
     * it as a fault, never a synthetic frame. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);begin(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,300);buf[73]=195;                      /* signed tilt -61 > bound 60 */
     CHECK(elden_pen_route_packet(&r,buf,109,7,110)==EldenPenRange);
     EldenPenOwnerStatus st;CHECK(!elden_pen_route_status(&r,&st));
     CHECK(st.faults==1&&st.decoded==1&&s.count==2&&!s.trace[1].tip);
     /* The next in-bounds real packet flows again under the same session. */
     coord(buf,400);CHECK(!elden_pen_route_packet(&r,buf,109,7,120));
     CHECK(s.count==3&&s.trace[2].tip&&s.trace[2].pressure==400);}

    printf("TestNativeRoute: %u checks passed\n",checks);return 0;
}
