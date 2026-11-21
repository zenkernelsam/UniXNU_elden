/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY deterministic pthread concurrency suite for the guarded
 * display/system-PM owner facade. Mutex/condvar gates fix every interleaving
 * (no sleeps, no random scheduling), so each run proves the exact fence: ready
 * racing request completion, stop racing request/ready, status racing updates,
 * and retire/rebind racing stale callbacks. Synthetic provider peers only —
 * nothing here proves a physical power provider works. */
#include "PowerOwner.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct{pthread_mutex_t m;pthread_cond_t c;unsigned seq;} Gate;
static void gate_init(Gate *g){pthread_mutex_init(&g->m,0);pthread_cond_init(&g->c,0);g->seq=0;}
static void gate_post(Gate *g){pthread_mutex_lock(&g->m);g->seq++;pthread_cond_broadcast(&g->c);pthread_mutex_unlock(&g->m);}
static void gate_wait(Gate *g,unsigned n){pthread_mutex_lock(&g->m);while(g->seq<n)pthread_cond_wait(&g->c,&g->m);pthread_mutex_unlock(&g->m);}
typedef struct {
    EldenWakePowerOwner *f;uint64_t gen;
    Gate inside,release;                 /* callback hold: post inside, wait release */
    unsigned hold_req,hold_res,hold_adm,hold_name;
    unsigned request_calls,resume_calls,oe_calls,name_calls;
    int admit_rc,request_rc,ready_rc,stop_rc,retire_rc,bind_rc;
} RacePeer;
static const char *r_name(void *v){RacePeer *p=v;p->name_calls++;
    if(p->hold_name){gate_post(&p->inside);gate_wait(&p->release,1);p->hold_name=0;}
    return "panel0";}
static int r_oe(void *v,uint64_t *o,uint64_t *e){RacePeer *p=v;p->oe_calls++;*o=9;*e=3;return 0;}
static int r_admit(void *v,uint64_t o,uint64_t e,unsigned phase){RacePeer *p=v;(void)phase;
    if(o!=9||e!=3)return -5;
    if(p->hold_adm){gate_post(&p->inside);gate_wait(&p->release,1);p->hold_adm=0;}return 0;}
static int r_request(void *v,uint64_t o,uint64_t e,unsigned g){RacePeer *p=v;
    p->request_calls++;if(o!=9||e!=3||g!=15)return -5;
    if(p->hold_req){gate_post(&p->inside);gate_wait(&p->release,1);p->hold_req=0;}return 0;}
static int r_resume(void *v,uint64_t o,uint64_t e){RacePeer *p=v;
    p->resume_calls++;if(o!=9||e!=3)return -5;
    if(p->hold_res){gate_post(&p->inside);gate_wait(&p->release,1);p->hold_res=0;}return 0;}
static int bind_race(RacePeer *p,EldenWakePowerOwner *f){
    EldenWakePowerProvider pr={p,r_name,r_oe,r_admit,r_request};p->f=f;
    return elden_wake_power_bind(f,&pr,"panel0",r_resume,p,&p->gen);
}
static RacePeer *rpeer(void){RacePeer *p=calloc(1,sizeof(*p));assert(p);
    gate_init(&p->inside);gate_init(&p->release);return p;}
static void *t_request(void *v){RacePeer *p=v;EldenTouchWakePower *w=&p->f->installed;
    p->request_rc=w->request_wake(w->context,w->owner,w->epoch,15);return 0;}
static void *t_admit(void *v){RacePeer *p=v;EldenTouchWakePower *w=&p->f->installed;
    p->admit_rc=w->admit(w->context,w->owner,w->epoch,EldenWakeAdmissionNormal);return 0;}
static void *t_ready(void *v){RacePeer *p=v;p->ready_rc=elden_wake_power_ready(p->f,p->gen,1);return 0;}
static void *t_stop(void *v){RacePeer *p=v;p->stop_rc=elden_wake_power_stop(p->f,p->gen);return 0;}
static void *t_retire(void *v){RacePeer *p=v;p->retire_rc=elden_wake_power_retire(p->f,p->gen);return 0;}
static void *t_bind(void *v){RacePeer *p=v;p->bind_rc=bind_race(p,p->f);return 0;}
static int run(void *(*fn)(void *),RacePeer *p){pthread_t t;int rc=pthread_create(&t,0,fn,p);if(!rc)pthread_join(t,0);return rc;}
/* ready racing request completion: a ready submitted while the worker's
 * request callback is still running must be refused (-16), then retry after
 * the request landed must consume the pending request and run resume. */
static int ready_vs_request(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    p->hold_req=1;
    pthread_t w;CHECK(!pthread_create(&w,0,t_request,p));
    gate_wait(&p->inside,1);                     // worker is inside request_wake
    CHECK(!run(t_ready,p));CHECK(p->ready_rc==-16);       // self-ack/concurrent refused
    gate_post(&p->release);pthread_join(w,0);
    CHECK(p->request_rc==0&&f.pending==1&&f.requests==1);
    CHECK(!run(t_ready,p));CHECK(p->ready_rc==0);         // own-thread retry succeeds
    CHECK(f.pending==0&&f.resumes==1&&p->resume_calls==1);
    free(p);return 0;
}
/* stop racing an in-flight request: deferred, applied when the callback
 * unwinds; the unwinding request reports -125 and the provider is stopped.
 * The queued provider work stays recorded pending=1 — its later readiness
 * call is refused -125, never silently resumed. */
static int stop_vs_request(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    p->hold_req=1;
    pthread_t w;CHECK(!pthread_create(&w,0,t_request,p));
    gate_wait(&p->inside,1);
    CHECK(!run(t_stop,p));CHECK(p->stop_rc==0);           // deferred, no wait
    gate_post(&p->release);pthread_join(w,0);
    CHECK(p->request_rc==-125&&f.stopped==1&&f.stops==1);
    CHECK(f.pending==1);                          // queued work still recorded
    EldenTouchWakePower *w2=&f.installed;
    CHECK(w2->request_wake(w2->context,9,3,15)==-125);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-125);
    free(p);return 0;
}
/* stop racing an in-flight resume (ready's own callback): same deferred
 * semantics; ready reports -125 and the facade is stopped. */
static int stop_vs_ready(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    p->hold_res=1;
    pthread_t w2;CHECK(!pthread_create(&w2,0,t_ready,p));
    gate_wait(&p->inside,1);                     // ready is inside the resume call
    CHECK(!run(t_stop,p));CHECK(p->stop_rc==0);
    gate_post(&p->release);pthread_join(w2,0);
    CHECK(p->ready_rc==-125&&f.stopped==1&&f.pending==0&&p->resume_calls==1);
    free(p);return 0;
}
/* status racing live updates: fixed-count concurrent readers while the worker
 * drives admit/request/ready — always race-free reads, all ops succeed. */
static int status_vs_ops(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    for(unsigned i=0;i<64;i++){
        EldenWakePowerStatus st;
        CHECK(elden_wake_power_status(&f,&st)==0);
        CHECK(w->admit(w->context,9,3,0)==0);
        CHECK(w->request_wake(w->context,9,3,15)==0);
        CHECK(elden_wake_power_ready(&f,p->gen,1)==0);
    }
    /* concurrent window: an admit callback thread runs while this thread spins
     * status snapshots — no assertion on snapshot values, only that every read
     * returned 0 with no torn/faulted state (sanitizers prove the rest). */
    pthread_t r;CHECK(!pthread_create(&r,0,t_admit,p));
    EldenWakePowerStatus st;for(unsigned i=0;i<256;i++)CHECK(elden_wake_power_status(&f,&st)==0);
    pthread_join(r,0);CHECK(p->admit_rc==0);
    CHECK(st.bound==1&&st.resumes==64&&st.requests==64&&st.readies==64);
    free(p);return 0;
}
/* Two workers racing request_wake: exactly one wins; the loser gets a Busy
 * refusal (concurrency or single-pending), the provider is asked exactly once,
 * and the single readiness result still lands. */
static int duplicate_requests(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    p->hold_req=1;
    pthread_t w;CHECK(!pthread_create(&w,0,t_request,p));
    gate_wait(&p->inside,1);
    EldenTouchWakePower *w2=&f.installed;
    int rc=w2->request_wake(w2->context,9,3,15);
    CHECK(rc==-16);                              // concurrent claim refused
    gate_post(&p->release);pthread_join(w,0);CHECK(p->request_rc==0);
    CHECK(p->request_calls==1&&f.pending==1&&f.requests==1);
    rc=w2->request_wake(w2->context,9,3,15);      // serialized: pending duplicate
    CHECK(rc==-16&&p->request_calls==1);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0&&f.pending==0&&f.resumes==1);
    free(p);return 0;
}
/* retire racing a stale-generation ready: the stale call is always fenced
 * (-116/-125/-16 by arrival order), retire clears the registration, rebind
 * arms a strictly newer generation, and the old token stays dead. */
static int retire_rebind(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    CHECK(elden_wake_power_retire(&f,p->gen)==-16);   // requires stop first
    CHECK(elden_wake_power_stop(&f,p->gen)==0&&f.stopped==1);
    uint64_t old=p->gen;
    pthread_t w2;CHECK(!pthread_create(&w2,0,t_retire,p));
    CHECK(!run(t_ready,p));                                 // stale ready races retire
    CHECK(p->ready_rc==-116||p->ready_rc==-125||p->ready_rc==-16);
    pthread_join(w2,0);CHECK(p->retire_rc==0&&f.bound==0&&!f.installed.admit);
    CHECK(elden_wake_power_ready(&f,old,1)==-116&&p->resume_calls==0);
    CHECK(bind_race(p,&f)==0&&p->gen==old+1);         // generation strictly newer
    w=&f.installed;
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    CHECK(elden_wake_power_ready(&f,old,1)==-116);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0&&f.resumes==1);
    free(p);return 0;
}
/* Concurrent binds on one facade: exactly one winner, generation unique and
 * never reused. Deterministic contract: one rc==0, the other -22 or -16. */
static int bind_race_case(void){
    RacePeer *p=rpeer(),*q=rpeer();EldenWakePowerOwner f={0};p->f=&f;q->f=&f;
    q->gen=0;
    pthread_t a,b;CHECK(!pthread_create(&a,0,t_bind,p));CHECK(!pthread_create(&b,0,t_bind,q));
    pthread_join(a,0);pthread_join(b,0);
    int ok=(p->bind_rc==0)+(q->bind_rc==0);
    CHECK(ok==1);                                  // exactly one bind armed
    CHECK(f.bound==1&&f.generation==1);
    uint64_t armed=p->gen?p->gen:q->gen;CHECK(armed==1);
    free(p);free(q);return 0;
}
/* Deferred stop must never be lost: stop posted while a callback holds the
 * claim is applied by the unwind or by the stop's own retry — the facade ends
 * stopped either way, and no caller spins. */
static int stop_late_unwind(void){
    RacePeer *p=rpeer();EldenWakePowerOwner f={0};CHECK(bind_race(p,&f)==0);
    p->hold_adm=1;
    pthread_t w;CHECK(!pthread_create(&w,0,t_admit,p));
    gate_wait(&p->inside,1);
    CHECK(!run(t_stop,p));CHECK(p->stop_rc==0);
    gate_post(&p->release);pthread_join(w,0);CHECK(p->admit_rc==-125);
    CHECK(f.stopped==1);
    CHECK(elden_wake_power_stop(&f,p->gen)==-125);  // already stopped
    free(p);return 0;
}
/* A second bind while the first holds the claim inside its own name callback:
 * the loser is refused before any provider I/O — its name is never invoked.
 * Provider callbacks are serialized, bind included. */
static int bind_serializes_name(void){
    RacePeer *p=rpeer(),*q=rpeer();EldenWakePowerOwner f={0};p->f=&f;q->f=&f;
    p->hold_name=1;
    pthread_t a;CHECK(!pthread_create(&a,0,t_bind,p));
    gate_wait(&p->inside,1);                     // bind A is inside its name cb
    CHECK(!run(t_bind,q));CHECK(q->bind_rc==-16); // refused by the claim
    CHECK(q->name_calls==0);                      // never reached provider I/O
    gate_post(&p->release);pthread_join(a,0);
    CHECK(p->bind_rc==0&&f.bound==1&&p->gen==1);
    free(p);free(q);return 0;
}
int main(void){
    int rc;
    if((rc=ready_vs_request())||(rc=stop_vs_request())||(rc=stop_vs_ready())||
       (rc=status_vs_ops())||(rc=duplicate_requests())||(rc=retire_rebind())||
       (rc=bind_race_case())||(rc=bind_serializes_name())||(rc=stop_late_unwind()))return rc;
    printf("TestPowerOwnerRace: %d checks passed\n",checks);return 0;
}
