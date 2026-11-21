/* SPDX-License-Identifier: GPL-2.0-only */
/* HOST-ONLY synthetic provider peers — deterministic pthread fences for the
 * RTC owner facade's nonblocking atomic-claim design. Every provider callback
 * is a test double that proves the serialized fences across the clock owner,
 * the IRQ owner and policy threads; no physical NVMEM cell, PMK8550 alarm
 * owner, IRQ or RTC hardware is present or modeled as evidence. */
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
enum {HOLD_NONE,HOLD_PWRITE,HOLD_PNAME,HOLD_RTC,HOLD_PREAD,HOLD_ANAME,HOLD_AOE};
typedef struct {
    EldenRtcOwner *f;EldenRtc *rtc;
    uint32_t raw[3];unsigned full;uint8_t low,control;
    uint8_t offset[4];unsigned preads,pwrites,poe_calls,pname_calls;
    uint8_t alarm[4],alarm_control,alarm_status;unsigned awrites,aoe_calls,aname_calls,nlog;char wlog[16];
    int hold;Gate entered,release;
    uint64_t ngen,agen;int irq_obj;
    int learn_rc,ack_rc,stop_rc,retire_rc,bind_rc,status_rc,restore_rc,set_rc;
    uint64_t bind_gen;EldenRtcOwnerStatus snap;
} R;
static int rtc_read(void *p,uint16_t a,uint8_t *o,size_t n){
    R*r=p;
    /* The core's own register read is not a facade provider callback: while a
     * high-level op waits here, op is held but busy is provably free. */
    if(r->hold==HOLD_RTC){gopen(&r->entered);gwait(&r->release);}
    if(a==0x6146){assert(n==1);*o=r->control;}
    else if(n==1){assert(a==0x6148);*o=r->low;}
    else{assert(a==0x6148&&n==4&&r->full<3);uint32_t v=r->raw[r->full++];for(unsigned i=0;i<4;++i)o[i]=(uint8_t)(v>>(i*8));}
    return 0;
}
static const char *rp_name(void *c){R*r=c;++r->pname_calls;
    if(r->hold==HOLD_PNAME){gopen(&r->entered);gwait(&r->release);}return "nvmem-offset";}
static const char *ra_name(void *c){R*r=c;++r->aname_calls;
    if(r->hold==HOLD_ANAME){gopen(&r->entered);gwait(&r->release);}return "pmk8550-alarm";}
static int rp_oe(void *c,uint64_t *o,uint64_t *e){R*r=c;++r->poe_calls;*o=7;*e=3;return 0;}
static int ra_oe(void *c,uint64_t *o,uint64_t *e){R*r=c;++r->aoe_calls;
    if(r->hold==HOLD_AOE){gopen(&r->entered);gwait(&r->release);}*o=9;*e=5;return 0;}
static int rp_read(void *c,uint8_t out[4]){R*r=c;++r->preads;
    if(r->hold==HOLD_PREAD){gopen(&r->entered);gwait(&r->release);}
    memcpy(out,r->offset,4);return 0;}
static int rp_write(void *c,const uint8_t d[4]){
    R*r=c;++r->pwrites;
    if(r->hold==HOLD_PWRITE){gopen(&r->entered);gwait(&r->release);}
    memcpy(r->offset,d,4);return 0;
}
static int ra_read(void *p,uint16_t a,uint8_t *o,size_t n){
    R*r=p;
    if(a==EldenRtcAlarmData){assert(n==4);memcpy(o,r->alarm,4);}
    else if(a==EldenRtcAlarmControl){assert(n==1);o[0]=r->alarm_control;}
    else if(a==EldenRtcAlarmStatus){assert(n==1);o[0]=r->alarm_status;}
    else return -1;return 0;
}
static int ra_write(void *c,uint16_t a,const uint8_t *d,size_t n){
    R*r=c;
    if(a!=EldenRtcAlarmData&&a!=EldenRtcAlarmControl&&a!=EldenRtcAlarmStatus)return -1;
    char tag=a==EldenRtcAlarmData?'D':a==EldenRtcAlarmControl?'C':'S';
    assert(r->nlog<sizeof(r->wlog));r->wlog[r->nlog++]=tag;
    if(tag=='D'){assert(n==4);memcpy(r->alarm,d,4);}
    else{assert(n==1);if(tag=='C')r->alarm_control=d[0];else r->alarm_status=d[0];}
    ++r->awrites;return 0;
}
static void put32(uint8_t p[4],uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void rinit(R*r,EldenRtc *rtc,EldenRtcOwner *f,uint32_t raw){
    memset(r,0,sizeof(*r));memset(rtc,0,sizeof(*rtc));
    r->raw[0]=r->raw[1]=r->raw[2]=raw;r->low=(uint8_t)raw;r->control=0x80;
    r->f=f;r->rtc=rtc;
    ginit(&r->entered);ginit(&r->release);
    EldenRtcIo io={r,rtc_read};assert(!elden_rtc_core_init(rtc,&io));
    assert(!elden_rtc_owner_init(f,rtc));
}
static void rbind(R*r){
    EldenRtcNvmem n={r,rp_name,rp_oe,rp_read,rp_write};
    assert(!elden_rtc_owner_bind_nvmem(r->f,&n,"nvmem-offset",&r->ngen));
    EldenRtcAlarmOwner a={r,ra_name,ra_oe,ra_read,ra_write};
    assert(!elden_rtc_owner_bind_alarm(r->f,&a,"pmk8550-alarm",&r->irq_obj,&r->agen));
}
static void *t_learn(void *x){R*r=x;r->learn_rc=elden_rtc_owner_learn(r->f,r->ngen,1800000700);return 0;}
static void *t_ack(void *x){R*r=x;r->ack_rc=elden_rtc_owner_alarm_ack(r->f,r->agen,&r->irq_obj);return 0;}
static void *t_stop(void *x){R*r=x;r->stop_rc=elden_rtc_owner_stop(r->f,r->ngen);return 0;}
static void *t_retire(void *x){R*r=x;r->retire_rc=elden_rtc_owner_retire(r->f,r->ngen);return 0;}
static void *t_status(void *x){R*r=x;r->status_rc=elden_rtc_owner_status(r->f,&r->snap);return 0;}
static void *t_bind(void *x){R*r=x;
    EldenRtcNvmem n={r,rp_name,rp_oe,rp_read,rp_write};
    r->bind_rc=elden_rtc_owner_bind_nvmem(r->f,&n,"nvmem-offset",&r->bind_gen);return 0;}
static void *t_bind_alarm(void *x){R*r=x;
    EldenRtcAlarmOwner al={r,ra_name,ra_oe,ra_read,ra_write};
    r->bind_rc=elden_rtc_owner_bind_alarm(r->f,&al,"pmk8550-alarm",&r->irq_obj,&r->bind_gen);return 0;}
static void *t_restore(void *x){R*r=x;r->restore_rc=elden_rtc_owner_restore(r->f,r->ngen);return 0;}
static void *t_set(void *x){R*r=x;r->set_rc=elden_rtc_owner_alarm_set(r->f,r->agen,1800005000,1);return 0;}
int main(void){
    EldenRtc rtc;EldenRtcOwner f;pthread_t a,b;R r,r2;
    /* learn's nvmem write holds the claim; a concurrent alarm ack on the IRQ
     * path is refused Busy instead of interleaving provider I/O, then
     * succeeds once the bridge unwinds. */
    rinit(&r,&rtc,&f,100);rbind(&r);r.full=0;r.hold=HOLD_PWRITE;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    pthread_create(&b,0,t_ack,&r);pthread_join(b,0);
    CHECK(r.ack_rc==EldenRtcOwnerBusy);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.learn_rc==0&&r.pwrites==1);
    CHECK(elden_rtc_owner_alarm_ack(&f,r.agen,&r.irq_obj)==0);
    CHECK(r.nlog==2&&!memcmp(r.wlog,"CS",2));
    /* learn-vs-alarm_set: two high-level operations can never overlap core
     * state — the second is refused Busy and a retry completes cleanly. */
    rinit(&r,&rtc,&f,105);rbind(&r);r.full=0;
    put32(r.offset,1800000000u-105);CHECK(elden_rtc_owner_restore(&f,r.ngen)==0);  /* has_offset */
    r.full=0;r.hold=HOLD_PWRITE;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    pthread_create(&b,0,t_set,&r);pthread_join(b,0);
    CHECK(r.set_rc==EldenRtcOwnerBusy);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.learn_rc==0);
    r.alarm_control=0xD5;r.nlog=0;r.full=0;
    CHECK(elden_rtc_owner_alarm_set(&f,r.agen,1800005000,1)==0&&r.nlog==3&&!memcmp(r.wlog,"CDC",3));
    /* restore-vs-alarm-ack: a restore inside its provider read still holds op,
     * so the concurrent ack cannot overlap the core call. */
    rinit(&r,&rtc,&f,108);rbind(&r);r.full=0;
    put32(r.offset,1800000000u-108);r.hold=HOLD_PREAD;
    pthread_create(&a,0,t_restore,&r);gwait(&r.entered);
    pthread_create(&b,0,t_ack,&r);pthread_join(b,0);
    CHECK(r.ack_rc==EldenRtcOwnerBusy);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.restore_rc==0&&rtc.persist_state==EldenRtcPersistVerified);
    /* Operation-level coverage beyond provider callbacks: a learn held inside
     * the core's own register read keeps op but provably leaves busy free —
     * ack/status/bind/retire are still all refused Busy rather than
     * overlapping shared core state. Only nvmem is bound so the mid-op alarm
     * bind exercises the op fence itself (unbound slot, busy provably free). */
    rinit(&r,&rtc,&f,115);
    {EldenRtcNvmem n={&r,rp_name,rp_oe,rp_read,rp_write};
     assert(!elden_rtc_owner_bind_nvmem(&f,&n,"nvmem-offset",&r.ngen));}
    r.full=0;r.hold=HOLD_RTC;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    CHECK(f.op&&!f.busy);                                  /* busy-free, op-held window */
    pthread_create(&b,0,t_status,&r);pthread_join(b,0);CHECK(r.status_rc==EldenRtcOwnerBusy);
    memset(&r2,0,sizeof(r2));r2.f=&f;r2.rtc=&rtc;ginit(&r2.entered);ginit(&r2.release);
    pthread_create(&b,0,t_bind_alarm,&r2);pthread_join(b,0);
    CHECK(r2.bind_rc==EldenRtcOwnerBusy&&r2.aname_calls==0); /* op fence, not busy/bound */
    pthread_create(&b,0,t_retire,&r);pthread_join(b,0);CHECK(r.retire_rc==EldenRtcOwnerBusy);
    gopen(&r.release);pthread_join(a,0);CHECK(r.learn_rc==0&&r.pwrites==1);
    {EldenRtcAlarmOwner al={&r,ra_name,ra_oe,ra_read,ra_write};
     CHECK(elden_rtc_owner_bind_alarm(&f,&al,"pmk8550-alarm",&r.irq_obj,&r.agen)==0);}
    /* Same window with both providers bound: a concurrent alarm ack is refused
     * Busy purely by the op claim, and a stop posted while the operation is
     * between provider callbacks applies immediately (busy is free) and
     * prevents every later provider read/write — the in-flight op reports the
     * uncertainty error, nothing publishes. */
    rinit(&r,&rtc,&f,116);rbind(&r);r.full=0;r.hold=HOLD_RTC;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    CHECK(f.op&&!f.busy);
    pthread_create(&b,0,t_ack,&r);pthread_join(b,0);CHECK(r.ack_rc==EldenRtcOwnerBusy);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&f.stopped);                        /* applied, busy was free */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.learn_rc==EldenRtcPersistError&&r.pwrites==0&&f.stops==1);
    CHECK(!rtc.has_offset&&rtc.persist_state==EldenRtcPersistUncertain);
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==EldenRtcStopped);
    /* stop posted while a persistence write holds the claim defers, is applied
     * by the holder's unwind, and is never lost: the in-flight op reports the
     * existing uncertainty error and every later op fences Stopped. */
    rinit(&r,&rtc,&f,110);rbind(&r);r.full=0;r.hold=HOLD_PWRITE;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&!f.stopped);                    /* deferred, not yet applied */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.learn_rc==EldenRtcPersistError&&f.stopped&&f.stops==1);
    CHECK(!rtc.has_offset&&rtc.persist_state==EldenRtcPersistUncertain);
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==EldenRtcStopped);
    CHECK(elden_rtc_owner_alarm_ack(&f,r.agen,&r.irq_obj)==EldenRtcStopped);
    /* status refuses Busy while an operation holds op (never a torn read of
     * core state mid-mutation); after the operation it returns a coherent
     * snapshot. */
    rinit(&r,&rtc,&f,120);rbind(&r);r.full=0;r.hold=HOLD_PWRITE;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    for(int i=0;i<40;++i){pthread_create(&b,0,t_status,&r);pthread_join(b,0);
        CHECK(r.status_rc==EldenRtcOwnerBusy);}
    gopen(&r.release);pthread_join(a,0);CHECK(r.learn_rc==0);
    CHECK(elden_rtc_owner_status(&f,&r.snap)==0&&r.snap.nvmem_bound&&r.snap.alarm_bound
          &&!r.snap.stopped&&r.snap.nvmem_gen==r.ngen&&r.snap.alarm_gen==r.agen);
    /* retire while an operation is in flight is refused Busy and the binding
     * is retained; after the deferred stop lands the retried retire clears it,
     * and the fenced generations stay stale/closed. */
    rinit(&r,&rtc,&f,130);rbind(&r);r.full=0;r.hold=HOLD_PWRITE;
    pthread_create(&a,0,t_learn,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);CHECK(r.stop_rc==0);
    pthread_create(&b,0,t_retire,&r);pthread_join(b,0);
    CHECK(r.retire_rc==EldenRtcOwnerBusy&&f.nvmem_bound);   /* retained */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.learn_rc==EldenRtcPersistError&&f.stopped);
    CHECK(elden_rtc_owner_retire(&f,r.ngen)==0&&!f.nvmem_bound&&!f.alarm_bound);
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==EldenRtcNoProvider);       /* binding released */
    CHECK(elden_rtc_owner_stop(&f,r.ngen)==EldenRtcOwnerStale);          /* gen fenced */
    /* concurrent binds race for the slot: exactly one wins, the loser is
     * refused Busy and never reaches its own provider name callback while the
     * winner's name probe holds the claim. */
    rinit(&r,&rtc,&f,140);memset(&r2,0,sizeof(r2));r2.f=&f;r2.rtc=&rtc;
    ginit(&r2.entered);ginit(&r2.release);
    r.hold=HOLD_PNAME;
    pthread_create(&a,0,t_bind,&r);gwait(&r.entered);    /* winner inside name() */
    pthread_create(&b,0,t_bind,&r2);pthread_join(b,0);
    CHECK(r2.bind_rc==EldenRtcOwnerBusy&&r2.bind_gen==0&&r2.pname_calls==0);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==0&&r.bind_gen&&f.nvmem_gen==r.bind_gen);
    /* rtc-006 fix: bind claims the operation claim for its whole lifecycle —
     * while a bind is inside its provider name probe (op+busy held), a facade
     * operation starting meanwhile is refused Busy instead of overlapping the
     * registration/publication, and a stop finds no live generation yet. */
    rinit(&r,&rtc,&f,160);r.hold=HOLD_PNAME;
    pthread_create(&a,0,t_bind,&r);gwait(&r.entered);
    CHECK(f.op&&f.busy);                                   /* bind holds both claims */
    r.ngen=0;
    pthread_create(&b,0,t_restore,&r);pthread_join(b,0);
    CHECK(r.restore_rc==EldenRtcOwnerBusy);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==EldenRtcOwnerStale);                  /* no live gen to fence yet */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==0&&r.bind_gen&&f.nvmem_gen==r.bind_gen);
    r.ngen=r.bind_gen;put32(r.offset,1800000000u-160);r.full=0;
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==0&&rtc.persist_state==EldenRtcPersistVerified);
    /* rtc-007 fix: a stop for the live nvmem generation posted while the
     * second (alarm) bind is inside its provider name() callback must abort
     * the bind — no owner_epoch probe, no core registration, no published
     * generation/bound state — and unwind applies the deferred stop. */
    rinit(&r,&rtc,&f,180);
    {EldenRtcNvmem n={&r,rp_name,rp_oe,rp_read,rp_write};
     assert(!elden_rtc_owner_bind_nvmem(&f,&n,"nvmem-offset",&r.ngen));}
    put32(r.offset,1800000000u-180);r.full=0;
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==0&&rtc.persist_state==EldenRtcPersistVerified);
    r.hold=HOLD_ANAME;
    pthread_create(&a,0,t_bind_alarm,&r);gwait(&r.entered);
    CHECK(f.op&&f.busy);                                   /* bind holds both claims in name() */
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0&&f.stopping&&!f.stopped);           /* deferred while busy held */
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==EldenRtcStopped);
    CHECK(r.aoe_calls==0);                                 /* no owner_epoch after pending stop */
    CHECK(!f.alarm_bound&&!f.alarm_gen&&!f.alarm_registered);
    CHECK(r.bind_gen==0);                                  /* out param untouched */
    CHECK(f.stopped&&!f.stopping&&!f.busy&&!f.op&&f.stops==1);
    CHECK(rtc.persist_state==EldenRtcPersistVerified);     /* no publication reached it */
    CHECK(elden_rtc_owner_restore(&f,r.ngen)==EldenRtcStopped);
    CHECK(elden_rtc_owner_retire(&f,r.ngen)==0&&!f.nvmem_bound);  /* stopped facade still reusable */
    /* same rule inside the owner_epoch probe: the abort happens before the
     * core bind API, so alarm_registered stays honest and nothing publishes. */
    rinit(&r,&rtc,&f,185);
    {EldenRtcNvmem n={&r,rp_name,rp_oe,rp_read,rp_write};
     assert(!elden_rtc_owner_bind_nvmem(&f,&n,"nvmem-offset",&r.ngen));}
    r.hold=HOLD_AOE;
    pthread_create(&a,0,t_bind_alarm,&r);gwait(&r.entered);
    pthread_create(&b,0,t_stop,&r);pthread_join(b,0);
    CHECK(r.stop_rc==0);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==EldenRtcStopped);
    CHECK(r.aname_calls==1&&r.aoe_calls==1);               /* probes ran, nothing after */
    CHECK(!f.alarm_bound&&!f.alarm_gen&&!f.alarm_registered);
    CHECK(r.bind_gen==0&&f.stopped&&!f.busy&&!f.op);
    CHECK(elden_rtc_owner_retire(&f,r.ngen)==0);
    /* and the NVMEM side follows the same rule when its bind is the in-flight
     * one: stop during name() aborts before owner_epoch/core registration. */
    rinit(&r,&rtc,&f,190);
    {EldenRtcAlarmOwner al={&r,ra_name,ra_oe,ra_read,ra_write};
     assert(!elden_rtc_owner_bind_alarm(&f,&al,"pmk8550-alarm",&r.irq_obj,&r.agen));}
    r.hold=HOLD_PNAME;
    pthread_create(&a,0,t_bind,&r);gwait(&r.entered);
    /* stop must target the live alarm generation while the nvmem bind is
     * held inside its name() callback. */
    CHECK(elden_rtc_owner_stop(&f,r.agen)==0&&f.stopping&&!f.stopped);
    gopen(&r.release);pthread_join(a,0);
    CHECK(r.bind_rc==EldenRtcStopped);
    CHECK(r.pname_calls==1&&r.poe_calls==0);               /* name ran, owner_epoch never */
    CHECK(!f.nvmem_bound&&!f.nvmem_gen&&!f.nvmem_registered);
    CHECK(r.bind_gen==0&&f.stopped&&!f.busy&&!f.op);
    CHECK(elden_rtc_owner_retire(&f,r.agen)==0&&!f.alarm_bound);
    /* retire-vs-operation lifecycle race: a stopped facade is retired while a
     * concurrent operation attempts to start. Either order is legal — the op
     * runs completely before the retire or is refused/fenced — but the two
     * can never interleave core registration/clearing, and a refused retire
     * retries to completion. */
    for(int i=0;i<30;++i){
        rinit(&r,&rtc,&f,170+i);rbind(&r);
        put32(r.offset,1800000000u-(170+i));r.full=0;
        CHECK(elden_rtc_owner_stop(&f,r.ngen)==0);
        pthread_create(&a,0,t_retire,&r);pthread_create(&b,0,t_restore,&r);
        pthread_join(a,0);pthread_join(b,0);
        CHECK(r.retire_rc==0||r.retire_rc==EldenRtcOwnerBusy);
        CHECK(r.restore_rc==EldenRtcStopped||r.restore_rc==EldenRtcOwnerBusy||r.restore_rc==EldenRtcNoProvider);
        if(r.retire_rc==EldenRtcOwnerBusy)
            CHECK(elden_rtc_owner_retire(&f,r.ngen)==0);   /* retry completes */
        CHECK(!f.nvmem_bound&&!f.alarm_bound&&!f.op&&!f.busy);
        CHECK(elden_rtc_owner_restore(&f,r.ngen)==EldenRtcNoProvider);
    }
    /* sustained concurrent ops across both providers serialize cleanly: every
     * op completes with a real result and counters stay exact. */
    rinit(&r,&rtc,&f,150);rbind(&r);
    put32(r.offset,1800000000u-150);r.full=0;
    for(int i=0;i<50;++i){
        r.full=0;r.nlog=0;pthread_create(&a,0,t_restore,&r);
        pthread_create(&b,0,t_ack,&r);
        pthread_join(a,0);pthread_join(b,0);
        CHECK((r.restore_rc==0||r.restore_rc==EldenRtcOwnerBusy)
              &&(r.ack_rc==0||r.ack_rc==EldenRtcOwnerBusy));
    }
    /* Every op ends in exactly one counter — a claim-losing op counts a Busy
     * rejection instead of a success, never a fabricated result. */
    CHECK(f.restores+f.acks+f.rejected==100);
    r.full=0;CHECK(elden_rtc_owner_restore(&f,r.ngen)==0&&rtc.persist_state==EldenRtcPersistVerified);
    CHECK(!f.op&&!f.busy);
    printf("PASS %d\n",checks);return 0;
}
