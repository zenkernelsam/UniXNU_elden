/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY synthetic provider peers — deterministic pthread fences for the
 * pen owner facade's nonblocking atomic-claim design. Every provider/send
 * callback is a test double that proves the serialized fences across the
 * sampling worker, policy/stop callers and status/cleanup paths; no real
 * AP501U, NT36536 worker, SPI bus, touch owner or Pencil acceptance is present
 * or modeled as evidence. */
#include "Owner.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct{pthread_mutex_t m;pthread_cond_t c;int v;} Gate;
static void ginit(Gate*g){pthread_mutex_init(&g->m,0);pthread_cond_init(&g->c,0);g->v=0;}
static void gopen(Gate*g){pthread_mutex_lock(&g->m);g->v=1;pthread_cond_broadcast(&g->c);pthread_mutex_unlock(&g->m);}
static void gwait(Gate*g){pthread_mutex_lock(&g->m);while(!g->v)pthread_cond_wait(&g->c,&g->m);pthread_mutex_unlock(&g->m);}
enum {HOLD_NONE,HOLD_OE,HOLD_PK,HOLD_FO,HOLD_SEND,HOLD_NAME};
static EldenPenLimits limits={19040,30400,4095,60};
typedef struct {
    EldenPenOwner *f;EldenPenDelivery *d;
    const char *pname;uint64_t ro_owner,ro_epoch,rf_owner,rf_epoch;
    int oe_fail,fo_fail,pk_fail;
    uint8_t pkts[16][109];uint64_t pts[16],pseq[16];unsigned npkts,pk_at;
    unsigned oe_calls,fo_calls,pk_calls,name_calls;
    unsigned scount,sfail;EldenPenFrame strace[32];
    int hold;Gate entered,release;
    uint64_t gen;uint64_t stop_now;
    int packet_rc,tick_rc,stop_rc,retire_rc,bind_rc,status_rc,release_rc,fault_rc;
    uint64_t bind_gen;EldenPenOwnerStatus snap;
} R;
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
static void coord(uint8_t *p,unsigned pressure){
    memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);
}
static void queue(R*r,unsigned pressure,uint64_t ts,uint64_t seq){
    assert(r->npkts<16);coord(r->pkts[r->npkts],pressure);
    r->pts[r->npkts]=ts;r->pseq[r->npkts]=seq;++r->npkts;
}
static int send(void *c,const EldenPenFrame *f){
    R*r=c;
    if(r->hold==HOLD_SEND){gopen(&r->entered);gwait(&r->release);}
    if(r->sfail){--r->sfail;return -99;}
    assert(r->scount<32);r->strace[r->scount++]=*f;return 0;
}
static const char *p_name(void *c){R*r=c;++r->name_calls;
    if(r->hold==HOLD_NAME){gopen(&r->entered);gwait(&r->release);}return r->pname;}
static int p_oe(void *c,uint64_t *o,uint64_t *e){
    R*r=c;++r->oe_calls;
    if(r->hold==HOLD_OE){gopen(&r->entered);gwait(&r->release);}
    if(r->oe_fail)return -1;*o=r->ro_owner;*e=r->ro_epoch;return 0;
}
static int p_fo(void *c,uint64_t *o,uint64_t *e){
    R*r=c;++r->fo_calls;
    if(r->hold==HOLD_FO){gopen(&r->entered);gwait(&r->release);}
    if(r->fo_fail)return -1;*o=r->rf_owner;*e=r->rf_epoch;return 0;
}
static int p_pk(void *c,uint8_t *out,uint64_t *ts,uint64_t *seq){
    R*r=c;++r->pk_calls;
    if(r->hold==HOLD_PK){gopen(&r->entered);gwait(&r->release);}
    if(r->pk_fail)return -17;
    if(r->pk_at>=r->npkts)return -1;
    memcpy(out,r->pkts[r->pk_at],109);*ts=r->pts[r->pk_at];*seq=r->pseq[r->pk_at];
    ++r->pk_at;return 0;
}
static void rinit(R*r,EldenPenDelivery *d,EldenPenOwner *f){
    memset(r,0,sizeof(*r));memset(d,0,sizeof(*d));
    r->f=f;r->d=d;r->pname="ap501u-nt36536-sampler";
    r->ro_owner=7;r->ro_epoch=3;r->rf_owner=11;r->rf_epoch=22;r->stop_now=900;
    ginit(&r->entered);ginit(&r->release);
    assert(!elden_pen_delivery_init(d,100));
    assert(!elden_pen_owner_init(f,d,&limits,send,r));
}
static EldenPenProvider prov(R*r){EldenPenProvider p={r,p_name,p_oe,p_fo,p_pk};return p;}
static void rbind(R*r){EldenPenProvider p=prov(r);
    assert(!elden_pen_owner_bind(r->f,&p,"ap501u-nt36536-sampler",11,22,&r->gen));}
static void *t_packet(void*x){R*r=x;r->packet_rc=elden_pen_owner_packet(r->f,r->gen);return 0;}
static void *t_tick(void*x){R*r=x;r->tick_rc=elden_pen_owner_tick(r->f,r->gen,7);return 0;}
static void *t_fault(void*x){R*r=x;r->fault_rc=elden_pen_owner_fault(r->f,r->gen,9);return 0;}
static void *t_fault2(void*x){R*r=x;r->status_rc=elden_pen_owner_fault(r->f,r->gen,9);return 0;}
static void *t_stop(void*x){R*r=x;r->stop_rc=elden_pen_owner_stop(r->f,r->gen,r->stop_now);return 0;}
static void *t_retire(void*x){R*r=x;r->retire_rc=elden_pen_owner_retire(r->f,r->gen);return 0;}
static void *t_status(void*x){R*r=x;r->status_rc=elden_pen_owner_status(r->f,&r->snap);return 0;}
static void *t_bind(void*x){R*r=x;EldenPenProvider p=prov(r);
    r->bind_rc=elden_pen_owner_bind(r->f,&p,r->pname,r->rf_owner,r->rf_epoch,&r->bind_gen);return 0;}
int main(void){
    EldenPenDelivery d;EldenPenOwner f;pthread_t a,b;R r,r2;
    /* packet-vs-stop: a stop landing inside provider packet() defers its
     * release to the op's unwind — the sample is dropped, the held contact is
     * still lifted, and later ops fence Stopped. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,100,1,1);queue(&r,110,2,2);
    CHECK(elden_pen_owner_packet(&f,r.gen)==0&&r.scount==1&&r.strace[0].tip);
    r.hold=HOLD_PK;
    pthread_create(&a,0,t_packet,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&f.stopped==r.gen&&f.stop_deferred==r.gen);  /* posted, deferred */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.packet_rc==EldenPenStopped&&f.dropped==1);
    CHECK(r.scount==2&&!r.strace[1].tip);                          /* deferred lift emitted */
    CHECK(elden_pen_owner_released(&f)&&f.stops==1&&!f.busy&&!f.stop_deferred);
    CHECK(elden_pen_owner_packet(&f,r.gen)==EldenPenStopped);
    /* tick-vs-retire: the retire claim refuses while an op holds the claim
     * inside owner_epoch; after the unwind the stop→retire order completes. */
    rinit(&r,&d,&f);rbind(&r);r.hold=HOLD_OE;
    pthread_create(&a,0,t_tick,&r);gwait(&r.entered);
    pthread_create(&b,0,t_retire,&r);pthread_join(b,0);
    CHECK(r.retire_rc==EldenPenOwnerBusy&&f.bound);                /* never inside a callback */
    gopen(&r.release);pthread_join(a,0);CHECK(r.tick_rc==0);
    CHECK(elden_pen_owner_stop(&f,r.gen,r.stop_now)==0);
    CHECK(elden_pen_owner_retire(&f,r.gen)==0&&!f.bound&&!f.gen);
    /* status during send: the snapshot refuses Busy while an op is mid-send,
     * then returns a coherent snapshot after unwind. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,200,1,1);r.hold=HOLD_SEND;
    pthread_create(&a,0,t_packet,&r);gwait(&r.entered);
    for(int i=0;i<20;++i){pthread_create(&b,0,t_status,&r);pthread_join(b,0);
        CHECK(r.status_rc==EldenPenOwnerBusy);}
    gopen(&r.release);pthread_join(a,0);CHECK(r.packet_rc==0);
    CHECK(elden_pen_owner_status(&f,&r.snap)==0&&r.snap.bound&&r.snap.packets==1
          &&r.snap.last_ts==1&&r.snap.transport_delivered==d.sent&&!r.snap.busy);
    /* concurrent binds: while the winner's name() probe holds the claim the
     * loser is refused Busy before reaching its own provider callback. */
    rinit(&r,&d,&f);memset(&r2,0,sizeof(r2));r2.f=&f;r2.d=&d;
    r2.pname="ap501u-nt36536-sampler";r2.ro_owner=7;r2.ro_epoch=3;
    r2.rf_owner=11;r2.rf_epoch=22;
    r.hold=HOLD_NAME;
    pthread_create(&a,0,t_bind,&r);gwait(&r.entered);
    pthread_create(&b,0,t_bind,&r2);pthread_join(b,0);
    CHECK(r2.bind_rc==EldenPenOwnerBusy&&r2.bind_gen==0&&r2.name_calls==0);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==0&&r.bind_gen==1&&f.gen==1);
    /* generation exhaustion refuses before any provider callback — nothing
     * armed, out-param untouched, refusal sticky. */
    rinit(&r,&d,&f);f.seq=UINT64_MAX;uint64_t g0=77;EldenPenProvider p=prov(&r);
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&g0)
          ==EldenPenDeliveryArgument&&g0==77);
    CHECK(r.name_calls==0&&r.oe_calls==0&&r.fo_calls==0&&!f.bound&&!f.gen);
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&g0)
          ==EldenPenDeliveryArgument);                              /* sticky */
    /* owner drift vs release: a drifted identity observed by the in-flight op
     * lifts the held contact through the neutral release — the concurrent op
     * is refused Busy and never sees half-released state. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,100,1,1);queue(&r,110,2,2);
    CHECK(elden_pen_owner_packet(&f,r.gen)==0&&r.scount==1&&r.strace[0].tip);
    r.ro_owner=99;r.hold=HOLD_OE;
    pthread_create(&a,0,t_packet,&r);gwait(&r.entered);
    pthread_create(&b,0,t_tick,&r);pthread_join(b,0);
    CHECK(r.tick_rc==EldenPenOwnerBusy);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.packet_rc==EldenPenOwnerStale);
    CHECK(r.scount==2&&!r.strace[1].tip&&elden_pen_owner_released(&f));
    /* frame-owner drift lifts the same way — packet refused Foreign, held
     * contact released. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,100,1,1);queue(&r,120,2,2);
    CHECK(elden_pen_owner_packet(&f,r.gen)==0&&r.scount==1);
    r.rf_owner=77;
    CHECK(elden_pen_owner_packet(&f,r.gen)==EldenPenOwnerForeign&&f.foreign_frames==1);
    CHECK(r.scount==2&&!r.strace[1].tip&&elden_pen_owner_released(&f));
    /* stop during provider owner_epoch: deferred in the probe, consumed at the
     * guard's recheck — the op reports Stopped and the release still emits. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,100,1,1);
    CHECK(elden_pen_owner_packet(&f,r.gen)==0&&r.scount==1);
    r.hold=HOLD_OE;
    pthread_create(&a,0,t_tick,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&f.stop_deferred==r.gen);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.tick_rc==EldenPenStopped&&f.stopped==r.gen&&!f.busy);
    CHECK(r.scount==2&&!r.strace[1].tip&&elden_pen_owner_released(&f));
    /* stop during provider frame_owner: same deferred application. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,100,1,1);queue(&r,130,2,2);
    CHECK(elden_pen_owner_packet(&f,r.gen)==0);
    r.hold=HOLD_FO;
    pthread_create(&a,0,t_packet,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.packet_rc==EldenPenStopped&&f.dropped==1&&f.stopped==r.gen);
    CHECK(r.scount==2&&!r.strace[1].tip&&elden_pen_owner_released(&f));
    /* stop during send: the deferred delivery_stop runs its own send at the
     * unwind — nested send is impossible, the lift is emitted after the
     * in-flight send completes. */
    rinit(&r,&d,&f);rbind(&r);queue(&r,140,1,1);r.hold=HOLD_SEND;
    pthread_create(&a,0,t_packet,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&r.scount==0);                               /* still inside send */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.packet_rc==EldenPenStopped&&f.stopped==r.gen);
    CHECK(r.scount==2&&!r.strace[1].tip&&elden_pen_owner_released(&f));
    /* retire-vs-operation lifecycle race on a stopped facade: either order is
     * legal — the op is fenced Stopped/Busy/NoProvider — and a refused retire
     * retries to completion. The gen-tagged stop can never poison the
     * successor binding. */
    for(int i=0;i<30;++i){
        rinit(&r,&d,&f);rbind(&r);
        CHECK(elden_pen_owner_stop(&f,r.gen,5)==0);
        pthread_create(&a,0,t_retire,&r);pthread_create(&b,0,t_packet,&r);
        pthread_join(a,0);pthread_join(b,0);
        CHECK(r.retire_rc==0||r.retire_rc==EldenPenOwnerBusy);
        CHECK(r.packet_rc==EldenPenStopped||r.packet_rc==EldenPenOwnerBusy
              ||r.packet_rc==EldenPenNoProvider||r.packet_rc==EldenPenOwnerStale);
        if(r.retire_rc==EldenPenOwnerBusy)
            CHECK(elden_pen_owner_retire(&f,r.gen)==0);
        CHECK(!f.bound&&!f.gen&&!f.busy&&!f.stop_deferred&&!f.stopped);
        CHECK(elden_pen_owner_packet(&f,r.gen)==EldenPenNoProvider);
        /* a rebind right after retire proves no stop tag survived: fresh
         * generation, first packet lands. */
        r.pk_at=0;r.npkts=0;r.scount=0;r.ro_owner=7;r.ro_epoch=3;
        queue(&r,150,1,1);EldenPenProvider p=prov(&r);uint64_t g2=0;
        CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&g2)==0&&g2!=r.gen);
        r.gen=g2;
        CHECK(elden_pen_owner_packet(&f,g2)==0&&r.strace[0].pressure==150);
    }
    /* sustained op-vs-op contention conserves counters exactly: every op ends
     * in exactly one outcome — a counted fault or a counted Busy rejection,
     * never a torn or double-counted result. */
    rinit(&r,&d,&f);rbind(&r);
    for(int i=0;i<50;++i){
        pthread_create(&a,0,t_fault,&r);pthread_create(&b,0,t_fault2,&r);
        pthread_join(a,0);pthread_join(b,0);
        CHECK((r.fault_rc==0||r.fault_rc==EldenPenOwnerBusy)
              &&(r.status_rc==0||r.status_rc==EldenPenOwnerBusy));
    }
    CHECK(f.faults+f.rejected==100);
    CHECK(!f.busy);
    printf("PASS %d\n",checks);return 0;
}
