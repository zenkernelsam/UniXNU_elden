/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Deterministic pthread fences for the default-off native bridge: opt-in
 * mutex/cond gates pin provider callbacks at exact boundaries while a second
 * thread calls bridge hooks — no timing assumptions. HOST-ONLY synthetic
 * peers; no real worker/SPI/AP501U/touch owner/HID is present or claimed. */
#include "NativeBridge.h"
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
    EldenPenNativeBridge *sb;uint64_t stop_gen,stop_now;int stop_once;
} Sink;
/* send runs under the claim on whichever thread holds it — a bound abort
 * only (no CHECK counter bump, which would race main-thread CHECKs). A
 * reentrant stop inside send exercises the lc-deferred path deterministically:
 * lc is held by the op that entered the send, so the reentry posts a pending
 * tag consumed at that op's own release. */
static int send(void *p,const EldenPenFrame *f){
    Sink*s=p;if(s->fail){--s->fail;return -99;}if(s->count>=64)abort();s->trace[s->count++]=*f;
    if(s->stop_once){s->stop_once=0;elden_pen_bridge_stop(s->sb,s->stop_gen,s->stop_now);}return 0;
}
typedef struct {
    EldenPenNativeBridge *b;
    const char *pname;uint64_t ro_owner,ro_epoch,rf_owner,rf_epoch;
    const uint8_t *pkts[24];uint64_t pts[24],pseq[24];unsigned npkts,pk_at;
    unsigned name_calls,oe_calls,fo_calls,pk_calls;
    uint64_t cb_gen;int cb_stop;
    Gate hold_name,hold_oe,hold_fo,hold_pk;
} M;
static uint8_t PK[24][109];
static void queue(M*m,const uint8_t*p,uint64_t ts,uint64_t seq){
    assert(m->npkts<24);memcpy(PK[m->npkts],p,109);
    m->pkts[m->npkts]=PK[m->npkts];m->pts[m->npkts]=ts;m->pseq[m->npkts]=seq;++m->npkts;
}
static const char *r_name(void *c){M*m=c;++m->name_calls;gate_hold(&m->hold_name);return m->pname;}
static int r_oe(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->oe_calls;gate_hold(&m->hold_oe);*o=m->ro_owner;*e=m->ro_epoch;return 0;}
static int r_fo(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->fo_calls;gate_hold(&m->hold_fo);*o=m->rf_owner;*e=m->rf_epoch;return 0;}
static int r_pk(void *c,uint8_t *out,uint64_t *ts,uint64_t *seq){
    M*m=c;++m->pk_calls;gate_hold(&m->hold_pk);
    if(m->pk_at>=m->npkts)return -1;
    memcpy(out,m->pkts[m->pk_at],109);*ts=m->pts[m->pk_at];*seq=m->pseq[m->pk_at];++m->pk_at;
    if(m->cb_stop){m->cb_stop=0;elden_pen_bridge_stop(m->b,m->cb_gen,900);}
    return 0;
}
static EldenPenProvider rprov(M*m){return (EldenPenProvider){m,r_name,r_oe,r_fo,r_pk};}
static void peer_init(M*m,EldenPenNativeBridge *b){
    memset(m,0,sizeof(*m));m->b=b;m->pname="ap501u-nt36536-sampler";
    m->ro_owner=7;m->ro_epoch=3;m->rf_owner=11;m->rf_epoch=22;
    gate_init(&m->hold_name);gate_init(&m->hold_oe);gate_init(&m->hold_fo);gate_init(&m->hold_pk);
}
static uint8_t buf[109];
static uint8_t *down(unsigned p){coord(buf,p);return buf;}
typedef struct{EldenPenNativeBridge*b;M*m;uint64_t gen,now;int rc;uint64_t out_gen;} Job;
static void*t_packet(void*a){Job*j=a;j->rc=elden_pen_bridge_packet(j->b,j->gen);return 0;}
static void*t_stop(void*a){Job*j=a;j->rc=elden_pen_bridge_stop(j->b,j->gen,j->now);return 0;}
static void*t_bind(void*a){Job*j=a;EldenPenProvider p=rprov(j->m);j->rc=elden_pen_bridge_bind(j->b,&p,"ap501u-nt36536-sampler",11,22,&j->out_gen);return 0;}
static void*t_start(void*a){Job*j=a;j->rc=elden_pen_bridge_start(j->b,j->gen);return 0;}
static void*t_status(void*a){Job*j=a;EldenPenOwnerStatus st;j->rc=elden_pen_bridge_status(j->b,&st);return 0;}
static void*t_retire(void*a){Job*j=a;j->rc=elden_pen_bridge_retire(j->b,j->gen);return 0;}
static void spawn(pthread_t*t,void*(*f)(void*),Job*j){CHECK(!pthread_create(t,0,f,j));}
static void join(pthread_t t){CHECK(!pthread_join(t,0));}
static uint64_t bindit(M*m,EldenPenNativeBridge *b){
    EldenPenProvider p=rprov(m);uint64_t gen=0;
    CHECK(!elden_pen_bridge_bind(b,&p,"ap501u-nt36536-sampler",11,22,&gen)&&gen);return gen;
}
int main(void){
    /* 1. packet-vs-stop: a foreign-thread stop inside a held provider
     * packet posts the deferred tag; the operation applies it at the
     * unwind — no publication, generation stopped. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);queue(&m,down(400),110,2);
     CHECK(elden_pen_bridge_packet(&b,gen)==0);            /* contact held */
     m.hold_pk.active=1;
     Job pj={&b,&m,gen,0,0,0},tj={&b,&m,gen,900,0,0};pthread_t s1,t;
     spawn(&s1,t_packet,&pj);gate_wait_armed(&m.hold_pk);
     CHECK(b.owner.busy==1);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);          /* deferred post */
     gate_release(&m.hold_pk);join(s1);
     CHECK(pj.rc==EldenPenStopped);                         /* post-callback fence */
     CHECK(b.owner.stopped==gen&&b.owner.busy==0&&elden_pen_bridge_started(&b)==0);
     CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerDisabled);}  /* gate off */

    /* 2. Stop inside provider owner_epoch: deferred before packet pull. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);
     m.hold_oe.active=1;
     Job pj={&b,&m,gen,0,0,0},tj={&b,&m,gen,900,0,0};pthread_t s1,t;
     spawn(&s1,t_packet,&pj);gate_wait_armed(&m.hold_oe);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);
     gate_release(&m.hold_oe);join(s1);
     CHECK(pj.rc==EldenPenStopped&&m.pk_calls==0);          /* packet never pulled */
     CHECK(b.owner.stopped==gen);}

    /* 3. Concurrent bridge packets serialize: the second refuses Busy inside
     * the held provider callback and never reaches the provider. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);queue(&m,down(400),110,2);
     m.hold_pk.active=1;
     Job s1={&b,&m,gen,0,0,0},s2={&b,&m,gen,0,0,0};pthread_t t1,t2;
     spawn(&t1,t_packet,&s1);gate_wait_armed(&m.hold_pk);
     spawn(&t2,t_packet,&s2);join(t2);
     CHECK(s2.rc==EldenPenOwnerBusy&&m.pk_calls==1);
     gate_release(&m.hold_pk);join(t1);CHECK(s1.rc==0);
     CHECK(elden_pen_bridge_stop(&b,gen,900)==0&&elden_pen_bridge_retire(&b,gen)==0);}

    /* 4. Concurrent binds on a fresh bridge: one winner; the loser refuses
     * Busy inside the winner's name() callback without touching its own
     * provider's name(). */
    {EldenPenNativeBridge b;Sink s;M a,d;peer_init(&a,&b);peer_init(&d,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     a.hold_name.active=1;
     Job ja={&b,&a,0,0,0,0},jd={&b,&d,0,0,0,0};pthread_t t1,t2;
     spawn(&t1,t_bind,&ja);gate_wait_armed(&a.hold_name);
     spawn(&t2,t_bind,&jd);join(t2);
     CHECK(jd.rc==EldenPenOwnerBusy&&d.name_calls==0);
     gate_release(&a.hold_name);join(t1);
     CHECK(ja.rc==0&&ja.out_gen!=0&&elden_pen_bridge_bound(&b)==1);
     CHECK(elden_pen_bridge_stop(&b,ja.out_gen,900)==0&&elden_pen_bridge_retire(&b,ja.out_gen)==0);}

    /* 5. start-vs-bind: a concurrent start while the winner's bind holds its
     * name() probe refuses on the owner's contended status claim. */
    {EldenPenNativeBridge b;Sink s;M a;peer_init(&a,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     a.hold_name.active=1;
     Job jb={&b,&a,0,0,0,0},js={&b,&a,1,0,0,0};pthread_t t1,t2;
     spawn(&t1,t_bind,&jb);gate_wait_armed(&a.hold_name);
     spawn(&t2,t_start,&js);join(t2);CHECK(js.rc==EldenPenOwnerBusy);
     gate_release(&a.hold_name);join(t1);CHECK(jb.rc==0);
     CHECK(elden_pen_bridge_start(&b,jb.out_gen)==0&&elden_pen_bridge_started(&b)==1);}

    /* 6. Status inside a held provider callback refuses Busy. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);
     m.hold_pk.active=1;
     Job pj={&b,&m,gen,0,0,0},st={&b,&m,0,0,0,0};pthread_t s1,t;
     spawn(&s1,t_packet,&pj);gate_wait_armed(&m.hold_pk);
     spawn(&t,t_status,&st);join(t);CHECK(st.rc==EldenPenOwnerBusy);
     gate_release(&m.hold_pk);join(s1);CHECK(pj.rc==0);
     CHECK(elden_pen_bridge_stop(&b,gen,900)==0&&elden_pen_bridge_retire(&b,gen)==0);}

    /* 7. Retire inside a held packet refuses Busy; after stop+release it
     * completes and clears the binding. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);queue(&m,down(400),110,2);
     CHECK(elden_pen_bridge_packet(&b,gen)==0);
     m.hold_pk.active=1;
     Job pj={&b,&m,gen,0,0,0},rj={&b,&m,gen,0,0,0};pthread_t s1,t;
     spawn(&s1,t_packet,&pj);gate_wait_armed(&m.hold_pk);
     spawn(&t,t_retire,&rj);join(t);CHECK(rj.rc==EldenPenOwnerBusy);
     gate_release(&m.hold_pk);join(s1);CHECK(pj.rc==0);
     CHECK(elden_pen_bridge_stop(&b,gen,900)==0);
     CHECK(elden_pen_bridge_retire(&b,gen)==0&&elden_pen_bridge_bound(&b)==0);
     /* Fresh bind gets a new generation; the retired token stays fenced —
     * Disabled both while the gate is off and once the successor's tag
     * arms it (generation-gated admission). */
     m.pk_at=0;uint64_t gen2=bindit(&m,&b);CHECK(gen2!=gen);
     CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerDisabled);
     CHECK(elden_pen_bridge_start(&b,gen2)==0);
     CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerDisabled);} /* gate tag */

    /* 8. Foreign frame owner under a held frame_owner() probe: the live
     * generation refuses and lifts a held contact through send. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);queue(&m,down(400),110,2);
     CHECK(elden_pen_bridge_packet(&b,gen)==0);            /* contact held */
     m.hold_fo.active=1;
     Job pj={&b,&m,gen,0,0,0};pthread_t s1;
     spawn(&s1,t_packet,&pj);gate_wait_armed(&m.hold_fo);
     m.rf_owner=99;                                        /* drift while held */
     gate_release(&m.hold_fo);join(s1);
     CHECK(pj.rc==EldenPenOwnerForeign&&b.owner.foreign_frames==1);
     CHECK(elden_pen_bridge_stop(&b,gen,900)==0&&elden_pen_bridge_retire(&b,gen)==0);}

    /* 9. Lifecycle claim contention: a bind inside its name() probe holds
     * `lc` for the whole transition — start/retire/bind refuse Busy rather
     * than interleave, and a stop POSTS its deferred tag instead of touching
     * the owner (consumed at the holder's release). */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen1=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen1));
     CHECK(elden_pen_bridge_stop(&b,gen1,900)==0&&elden_pen_bridge_retire(&b,gen1)==0);
     m.hold_name.active=1;
     Job jb={&b,&m,0,0,0,0},js={&b,&m,0,0,0,0},jr={&b,&m,gen1,0,0,0},jd={&b,&m,0,0,0,0};
     Job jt={&b,&m,gen1,900,0,0};pthread_t t1,t2,t3,t4,t5;
     spawn(&t1,t_bind,&jb);gate_wait_armed(&m.hold_name);
     CHECK(b.lc==1);                                        /* provable hold */
     spawn(&t2,t_start,&js);join(t2);CHECK(js.rc==EldenPenOwnerBusy);
     spawn(&t3,t_retire,&jr);join(t3);CHECK(jr.rc==EldenPenOwnerBusy);
     spawn(&t4,t_bind,&jd);join(t4);CHECK(jd.rc==EldenPenOwnerBusy);
     spawn(&t5,t_stop,&jt);join(t5);CHECK(jt.rc==0);        /* posted, not applied */
     CHECK(b.stop_pending==gen1&&b.lc==1);                  /* deferred tag held */
     gate_release(&m.hold_name);join(t1);
     CHECK(jb.rc==0&&jb.out_gen!=0&&jb.out_gen!=gen1);
     CHECK(b.stop_pending==0&&b.lc==0&&b.started==0);       /* consumed, dropped */
     CHECK(b.owner.stopped==0);                             /* dead tag was moot */
     CHECK(elden_pen_bridge_stop(&b,jb.out_gen,900)==0);
     CHECK(elden_pen_bridge_retire(&b,jb.out_gen)==0);}

    /* 10. Callback-reentrant stop: a stop invoked from inside the provider
     * packet() callback (lc free during data ops) claims lc, reaches the
     * owner's deferred path, applies at the in-flight unwind — no deadlock. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);
     m.cb_gen=gen;m.cb_stop=1;                              /* stop inside packet() */
     CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenStopped);
     CHECK(b.owner.stopped==gen&&b.started==0&&b.lc==0&&b.stop_pending==0);
     CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerDisabled);
     CHECK(elden_pen_bridge_retire(&b,gen)==0);}

    /* 11. Send-reentrant stop: a stop invoked inside send() during the outer
     * stop's own lift can't take lc — it posts the deferred tag and the outer
     * call consumes it at release; outcome identical, no claim leaked. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
     queue(&m,down(300),100,1);
     CHECK(elden_pen_bridge_packet(&b,gen)==0);            /* contact held */
     s.sb=&b;s.stop_gen=gen;s.stop_now=900;s.stop_once=1;  /* reenter in send */
     CHECK(elden_pen_bridge_stop(&b,gen,900)==0);
     CHECK(b.owner.stopped==gen&&b.started==0&&b.lc==0&&b.stop_pending==0);
     CHECK(elden_pen_bridge_retire(&b,gen)==0);}

    /* 12. The fixed race: stop→retire→rebind must be followed by a delayed
     * old-generation start that can never open the successor's gate. The
     * rebind pins `lc` inside name(); the stale start refuses at the claim
     * (it can neither validate nor publish), and after the successor is
     * armed the same token is fenced — the gate stays 0 until an explicit,
     * validated start arms the NEW generation's tag. */
    {EldenPenNativeBridge b;Sink s;M m;peer_init(&m,&b);memset(&s,0,sizeof(s));
     CHECK(!elden_pen_bridge_init(&b,&limits,send,&s));
     uint64_t gen1=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen1));
     queue(&m,down(300),100,1);CHECK(elden_pen_bridge_packet(&b,gen1)==0);
     CHECK(elden_pen_bridge_stop(&b,gen1,900)==0);
     CHECK(elden_pen_bridge_retire(&b,gen1)==0);
     m.pk_at=0;queue(&m,down(320),50,7);
     m.hold_name.active=1;
     Job jb={&b,&m,0,0,0,0},js={&b,&m,gen1,0,0,0},jr={&b,&m,gen1,0,0,0};
     pthread_t t1,t2,t3;
     spawn(&t1,t_bind,&jb);gate_wait_armed(&m.hold_name);
     CHECK(b.lc==1);
     spawn(&t2,t_start,&js);join(t2);CHECK(js.rc==EldenPenOwnerBusy);   /* can't publish */
     spawn(&t3,t_retire,&jr);join(t3);CHECK(jr.rc==EldenPenOwnerBusy);
     CHECK(elden_pen_bridge_stop(&b,gen1,900)==0);                      /* posts pending */
     CHECK(b.stop_pending==gen1);
     gate_release(&m.hold_name);join(t1);
     uint64_t gen2=jb.out_gen;
     CHECK(jb.rc==0&&gen2&&gen2!=gen1);
     CHECK(b.stop_pending==0&&b.started==0&&b.lc==0);                   /* no late publish */
     CHECK(elden_pen_bridge_start(&b,gen1)==EldenPenOwnerStale);        /* stale forever */
     CHECK(b.started==0);
     CHECK(elden_pen_bridge_start(&b,gen2)==0&&b.started==gen2);        /* tag = successor */
     CHECK(elden_pen_bridge_packet(&b,gen1)==EldenPenOwnerDisabled);    /* old token gated */
     CHECK(elden_pen_bridge_packet(&b,gen2)==0);                        /* successor flows */
     CHECK(elden_pen_bridge_stop(&b,gen2,900)==0&&b.started==0);
     CHECK(elden_pen_bridge_retire(&b,gen2)==0&&b.lc==0);}

    printf("PASS %u\n",checks);return 0;
}
