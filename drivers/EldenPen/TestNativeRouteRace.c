/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Deterministic pthread fences for the actual worker route: the serialized
 * pusher contract is kept (one thread pushes), while opt-in mutex/cond gates
 * pin the send callback inside the held owner claim so a second thread's
 * stop/pause/retire/begin lands at an exact boundary — no timing assumptions.
 * HOST-ONLY peers; no real worker/SPI/AP501U/HID is present or claimed. */
#include "NativeRoute.h"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {pthread_mutex_t m;pthread_cond_t c;int active,armed,released;} Gate;
static void gate_init(Gate*g){pthread_mutex_init(&g->m,0);pthread_cond_init(&g->c,0);g->active=0;g->armed=0;g->released=0;}
static void gate_hold(Gate*g){pthread_mutex_lock(&g->m);if(!g->active){pthread_mutex_unlock(&g->m);return;}g->armed=1;pthread_cond_broadcast(&g->c);while(!g->released)pthread_cond_wait(&g->c,&g->m);g->active=0;pthread_mutex_unlock(&g->m);}
static void gate_wait_armed(Gate*g){pthread_mutex_lock(&g->m);while(!g->armed)pthread_cond_wait(&g->c,&g->m);pthread_mutex_unlock(&g->m);}
static void gate_release(Gate*g){pthread_mutex_lock(&g->m);g->released=1;pthread_cond_broadcast(&g->c);pthread_mutex_unlock(&g->m);}

static EldenPenLimits limits={19040,30400,4095,60};
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
static void coord(uint8_t *p,unsigned pressure){memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);}
typedef struct {
    unsigned count,fail;EldenPenFrame trace[64];
    EldenPenNativeRoute *r;Gate hold_send;
} Sink;
/* send runs under the owner claim on whichever thread holds it. The opt-in
 * hold pins the op inside send so a second thread's route call provably
 * contends the same claim — a bound abort only, no CHECK counter here. */
static int send(void *p,const EldenPenFrame *f){
    Sink*s=p;if(s->fail){--s->fail;return -99;}if(s->count>=64)abort();s->trace[s->count++]=*f;
    gate_hold(&s->hold_send);return 0;
}
static void setup(EldenPenNativeRoute *r,Sink *s){
    memset(r,0,sizeof(*r));memset(s,0,sizeof(*s));s->r=r;gate_init(&s->hold_send);
    assert(!elden_pen_route_begin(r,&limits,send,s,"elden-pen-native",7,7));
}
static uint8_t buf[109];
static int released(EldenPenNativeRoute *r){EldenPenOwnerStatus st;return elden_pen_route_status(r,&st)?0:st.delivery_released;}
typedef struct{EldenPenNativeRoute*r;const uint8_t*p;uint64_t fo,ts;int rc;} Job;
static void*t_push(void*a){Job*j=a;j->rc=elden_pen_route_packet(j->r,j->p,109,j->fo,j->ts);return 0;}
static void*t_stop(void*a){Job*j=a;j->rc=elden_pen_route_stop(j->r,j->ts);return 0;}
static void*t_pause(void*a){Job*j=a;j->rc=elden_pen_route_pause(j->r,j->ts);return 0;}
static void*t_status(void*a){Job*j=a;EldenPenOwnerStatus st;j->rc=elden_pen_route_status(j->r,&st);return 0;}
static void*t_retire(void*a){Job*j=a;j->rc=elden_pen_route_retire(j->r);return 0;}
static void*t_begin(void*a){Job*j=a;j->rc=elden_pen_route_begin(j->r,&limits,send,(void*)j->p,"elden-pen-native",7,7);return 0;}
static void spawn(pthread_t*t,void*(*f)(void*),Job*j){CHECK(!pthread_create(t,0,f,j));}
static void join(pthread_t t){CHECK(!pthread_join(t,0));}
int main(void){
    /* 1. Push held inside send (owner claim held): a foreign-thread route
     * stop disarms the gate, defers through the owner and is consumed at the
     * pusher's unwind — the push reports Stopped, never a held contact. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,400);s.hold_send.active=1;
     Job pj={&r,buf,7,110,0},tj={&r,0,0,120,0};pthread_t p1,t;
     spawn(&p1,t_push,&pj);gate_wait_armed(&s.hold_send);
     CHECK(r.bridge.owner.busy==1);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);          /* deferred post */
     gate_release(&s.hold_send);join(p1);
     CHECK(pj.rc==EldenPenStopped);                         /* consumed at unwind */
     CHECK(!r.bridge.started&&r.bridge.owner.stopped==r.gen);
     coord(buf,500);
     CHECK(elden_pen_route_packet(&r,buf,109,7,130)==EldenPenOwnerDisabled);}

    /* 2. A status call during the held send either wins the claim (coherent
     * snapshot) or is refused Busy — never a torn read. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,400);s.hold_send.active=1;
     Job pj={&r,buf,7,110,0},sj={&r,0,0,0,0};pthread_t p1,t;
     spawn(&p1,t_push,&pj);gate_wait_armed(&s.hold_send);
     spawn(&t,t_status,&sj);join(t);
     CHECK(sj.rc==0||sj.rc==EldenPenOwnerBusy);
     gate_release(&s.hold_send);join(p1);CHECK(pj.rc==0||pj.rc==EldenPenStopped);}

    /* 3. A sleep-coordinator pause racing the held send is refused Busy on
     * the live claim — the push completes, no contact is dropped. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     coord(buf,400);s.hold_send.active=1;
     Job pj={&r,buf,7,110,0},aj={&r,0,0,120,0};pthread_t p1,t;
     spawn(&p1,t_push,&pj);gate_wait_armed(&s.hold_send);
     spawn(&t,t_pause,&aj);join(t);
     CHECK(aj.rc==EldenPenOwnerBusy);                       /* claim provably held */
     gate_release(&s.hold_send);join(p1);CHECK(pj.rc==0);
     CHECK(!r.bridge.delivery.paused);}

    /* 4. Retire before stop is fenced by the owner's ordered release — the
     * armed session keeps its generation, gate and binding. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     Job rj={&r,0,0,0,0};pthread_t t;spawn(&t,t_retire,&rj);join(t);
     CHECK(rj.rc==EldenPenOwnerDisabled);                   /* stop first */
     CHECK(r.armed&&r.gen&&r.bridge.started==r.gen&&r.bridge.owner.bound);
     coord(buf,400);CHECK(!elden_pen_route_packet(&r,buf,109,7,110));}

    /* 5. A second begin while armed is refused Busy and changes nothing. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     Job bj={&r,(const uint8_t *)&s,0,0,0};pthread_t t;spawn(&t,t_begin,&bj);join(t);
     CHECK(bj.rc==EldenPenOwnerBusy&&r.gen==1&&r.armed==1);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));}

    /* 6. Two concurrent stops on the live generation: exactly one may apply;
     * the other gets the honest already-stopped/posted answer — never a
     * double delivery stop and never a leaked pending tag. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     Job a={&r,0,0,110,0},b={&r,0,0,120,0};pthread_t t1,t2;
     spawn(&t1,t_stop,&a);spawn(&t2,t_stop,&b);join(t1);join(t2);
     CHECK((a.rc==0||a.rc==EldenPenStopped)&&(b.rc==0||b.rc==EldenPenStopped||b.rc==EldenPenOwnerStale));
     CHECK(!r.bridge.started&&r.bridge.owner.stopped==r.gen);
     while(!released(&r))CHECK(!elden_pen_route_release(&r));
     CHECK(!elden_pen_route_retire(&r)&&!r.armed);}

    /* 7. After a clean retire a fresh begin re-arms under the next generation;
     * every old-generation route token mutates nothing. */
    {EldenPenNativeRoute r;Sink s;setup(&r,&s);
     coord(buf,300);CHECK(!elden_pen_route_packet(&r,buf,109,7,100));
     uint64_t old=r.gen;
     CHECK(!elden_pen_route_stop(&r,110));
     while(!released(&r))CHECK(!elden_pen_route_release(&r));
     CHECK(!elden_pen_route_retire(&r));
     CHECK(!elden_pen_route_begin(&r,&limits,send,&s,"elden-pen-native",9,9));
     CHECK(r.gen!=old&&r.armed);
     coord(buf,400);CHECK(!elden_pen_route_packet(&r,buf,109,9,120));
     CHECK(elden_pen_bridge_packet(&r.bridge,old)==EldenPenOwnerDisabled);
     CHECK(elden_pen_bridge_stop(&r.bridge,old,130)==EldenPenOwnerStale);}

    printf("TestNativeRouteRace: %u checks passed\n",checks);return 0;
}
