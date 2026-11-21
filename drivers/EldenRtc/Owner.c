/* SPDX-License-Identifier: GPL-2.0-only */
#include "Owner.h"
/* Named-identity equality without <string.h> (freestanding profile). */
static int name_eq(const char *a,const char *b){
    if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;
}
/* A generation is live iff it matches a currently-bound provider's armed gen. */
static int gen_live(EldenRtcOwner *s,uint64_t g){
    return g&&((atomic_load(&s->nvmem_bound)&&g==atomic_load(&s->nvmem_gen))||
               (atomic_load(&s->alarm_bound)&&g==atomic_load(&s->alarm_gen)));
}
/* Nonblocking serialized claim across the clock owner, the IRQ owner and
 * policy callers. Every provider callback — name, owner_epoch, read, write
 * for either provider — runs only under the claim, so a concurrent or
 * reentrant facade call is refused instead of racing provider I/O. */
static int claim(EldenRtcOwner *s){return atomic_exchange(&s->busy,1);}
/* The operation-level claim is distinct from busy: it covers one complete
 * high-level operation (fence, whole core call, counters) while the bridges
 * keep using busy inside it — a provider callback therefore never fails its
 * parent's operation claim, and two high-level calls can never overlap shared
 * core state. */
static int claim_op(EldenRtcOwner *s){return atomic_exchange(&s->op,1);}
/* unwind returns nonzero when a deferred stop landed: a stop posted while a
 * callback held the claim is applied here exactly once, so the stop is never
 * lost and the in-flight op can report the existing error instead of a
 * fabricated result. */
static int unwind(EldenRtcOwner *s){
    atomic_store(&s->busy,0);
    if(atomic_load(&s->stopping)){
        if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
        atomic_store(&s->stopping,0);
        return 1;
    }
    return 0;
}
/* Bridged provider I/O: the core calls these; they serialize the provider
 * callback under the claim, re-fence bound/stopped after claiming and before
 * any I/O, and re-fence owner/epoch right before the access. A stop landing
 * inside owner_epoch must not let the bridge reach the provider read/write:
 * the stopping arm is observed, unwind applies the stop, and the op returns
 * the existing uncertainty/error so the core marks the result uncertain
 * instead of publishing a committed one. The post-I/O unwind reports a
 * landed stop the same way — the result was computed but is dropped. */
static int bridge_pr(void *c,uint8_t out[4]){
    EldenRtcOwner *s=c;
    if(!atomic_load(&s->nvmem_bound)||atomic_load(&s->stopped))return EldenRtcPersistError;
    if(claim(s))return EldenRtcPersistError;
    if(!atomic_load(&s->nvmem_bound)||atomic_load(&s->stopped)||atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);
    if(atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    if(rc||o!=atomic_load(&s->nvmem_owner)||e!=atomic_load(&s->nvmem_epoch))rc=EldenRtcPersistError;
    else rc=s->nvmem.read(s->nvmem.context,out)?EldenRtcPersistError:0;
    if(unwind(s)){atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    return rc;
}
static int bridge_pw(void *c,const uint8_t data[4]){
    EldenRtcOwner *s=c;
    if(!atomic_load(&s->nvmem_bound)||atomic_load(&s->stopped))return EldenRtcPersistError;
    if(claim(s))return EldenRtcPersistError;
    if(!atomic_load(&s->nvmem_bound)||atomic_load(&s->stopped)||atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);
    if(atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    if(rc||o!=atomic_load(&s->nvmem_owner)||e!=atomic_load(&s->nvmem_epoch))rc=EldenRtcPersistError;
    else rc=s->nvmem.write(s->nvmem.context,data)?EldenRtcPersistError:0;
    if(unwind(s)){atomic_fetch_add(&s->dropped,1);return EldenRtcPersistError;}
    return rc;
}
static int bridge_ar(void *c,uint16_t a,uint8_t *out,size_t n){
    EldenRtcOwner *s=c;
    if(!atomic_load(&s->alarm_bound)||atomic_load(&s->stopped))return EldenRtcAlarmError;
    if(claim(s))return EldenRtcAlarmError;
    if(!atomic_load(&s->alarm_bound)||atomic_load(&s->stopped)||atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    if(rc||o!=atomic_load(&s->alarm_owner)||e!=atomic_load(&s->alarm_epoch))rc=EldenRtcAlarmError;
    else rc=s->alarm.read(s->alarm.context,a,out,n)?EldenRtcAlarmError:0;
    if(unwind(s)){atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    return rc;
}
static int bridge_aw(void *c,uint16_t a,const uint8_t *d,size_t n){
    EldenRtcOwner *s=c;
    if(!atomic_load(&s->alarm_bound)||atomic_load(&s->stopped))return EldenRtcAlarmError;
    if(claim(s))return EldenRtcAlarmError;
    if(!atomic_load(&s->alarm_bound)||atomic_load(&s->stopped)||atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(atomic_load(&s->stopping)){
        unwind(s);atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    if(rc||o!=atomic_load(&s->alarm_owner)||e!=atomic_load(&s->alarm_epoch))rc=EldenRtcAlarmError;
    else rc=s->alarm.write(s->alarm.context,a,d,n)?EldenRtcAlarmError:0;
    if(unwind(s)){atomic_fetch_add(&s->dropped,1);return EldenRtcAlarmError;}
    return rc;
}
int elden_rtc_owner_init(EldenRtcOwner *s,EldenRtc *core){
    if(!s||!core||!core->initialized)return EldenRtcInvalid;
    s->core=core;s->nvmem=(EldenRtcNvmem){0};s->alarm=(EldenRtcAlarmOwner){0};
    s->persist_io=(EldenRtcPersistence){s,bridge_pr,bridge_pw};
    s->alarm_io=(EldenRtcAlarmIo){s,bridge_ar,bridge_aw};
    atomic_init(&s->irq,0);atomic_init(&s->seq,0);
    atomic_init(&s->nvmem_gen,0);atomic_init(&s->alarm_gen,0);
    atomic_init(&s->nvmem_owner,0);atomic_init(&s->nvmem_epoch,0);
    atomic_init(&s->alarm_owner,0);atomic_init(&s->alarm_epoch,0);
    atomic_init(&s->nvmem_bound,0);atomic_init(&s->alarm_bound,0);
    atomic_init(&s->nvmem_registered,0);atomic_init(&s->alarm_registered,0);
    atomic_init(&s->stopped,0);atomic_init(&s->stopping,0);atomic_init(&s->busy,0);
    atomic_init(&s->op,0);
    atomic_init(&s->restores,0);atomic_init(&s->learns,0);
    atomic_init(&s->alarm_sets,0);atomic_init(&s->alarm_reads,0);
    atomic_init(&s->acks,0);atomic_init(&s->stops,0);
    atomic_init(&s->rejected,0);atomic_init(&s->dropped,0);
    return 0;
}
/* Per-call fence before any core/provider work: stopped dominates, then the
 * binding must exist and the generation must be the armed one — all checked
 * once cheaply and again under the claim so a stop/retire racing in between
 * is re-fenced before the owner_epoch probe. The probe is itself a provider
 * callback, serialized under the claim; a stop landing inside it is applied
 * by unwind and reported Stopped. */
static int fence_nvmem(EldenRtcOwner *s,uint64_t g){
    if(atomic_load(&s->stopped))return EldenRtcStopped;
    if(!atomic_load(&s->nvmem_bound))return EldenRtcNoProvider;
    if(g!=atomic_load(&s->nvmem_gen))return EldenRtcOwnerStale;
    if(claim(s))return EldenRtcOwnerBusy;
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(!atomic_load(&s->nvmem_bound)){unwind(s);return EldenRtcNoProvider;}
    if(g!=atomic_load(&s->nvmem_gen)){unwind(s);return EldenRtcOwnerStale;}
    uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);
    if(rc||o!=atomic_load(&s->nvmem_owner)||e!=atomic_load(&s->nvmem_epoch))rc=EldenRtcOwnerStale;
    return unwind(s)?EldenRtcStopped:rc;
}
static int fence_alarm(EldenRtcOwner *s,uint64_t g){
    if(atomic_load(&s->stopped))return EldenRtcStopped;
    if(!atomic_load(&s->alarm_bound))return EldenRtcNoProvider;
    if(g!=atomic_load(&s->alarm_gen))return EldenRtcOwnerStale;
    if(claim(s))return EldenRtcOwnerBusy;
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(!atomic_load(&s->alarm_bound)){unwind(s);return EldenRtcNoProvider;}
    if(g!=atomic_load(&s->alarm_gen)){unwind(s);return EldenRtcOwnerStale;}
    uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(rc||o!=atomic_load(&s->alarm_owner)||e!=atomic_load(&s->alarm_epoch))rc=EldenRtcOwnerStale;
    return unwind(s)?EldenRtcStopped:rc;
}
/* op is held by the caller for the whole lifecycle operation — validation,
 * the busy-serialized provider probes, core registration, and publication —
 * so no high-level operation can start between the checks and the armed
 * binding or overlap the registration. busy is nested only around the
 * provider callbacks inside it. */
static int bind_nvmem_common(EldenRtcOwner *s,const EldenRtcNvmem *p,const char *named,uint64_t *generation){
    if(!s||!s->core||!p||!named||!*named||!generation)return EldenRtcInvalid;
    if(!p->name||!p->owner_epoch||!p->read||!p->write)return EldenRtcInvalid;
    if(atomic_load(&s->stopped))return EldenRtcStopped;
    if(atomic_load(&s->nvmem_bound))return EldenRtcOwnerBusy;
    if(claim(s))return EldenRtcOwnerBusy;
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(atomic_load(&s->nvmem_bound)){unwind(s);return EldenRtcOwnerBusy;}
    /* seq+1 must never wrap: a reused generation could resurrect a fenced
     * identity. Refuse before any provider callback and leave everything
     * untouched. */
    if(atomic_load(&s->seq)==UINT64_MAX){unwind(s);return EldenRtcInvalid;}
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named)){unwind(s);return EldenRtcInvalid;}
    /* A stop posted (stopping) or applied (stopped) while busy is held must
     * abort the bind: no further provider callback, no core registration, no
     * publication — the same stop-before-later-I/O rule the bridges follow. */
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    uint64_t o=0,e=0;
    if(p->owner_epoch(p->context,&o,&e)||!o){unwind(s);return EldenRtcInvalid;}
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(!atomic_load(&s->nvmem_registered)){
        int rc=elden_rtc_core_bind_persistence(s->core,&s->persist_io);
        if(rc){unwind(s);return rc;}
        atomic_store(&s->nvmem_registered,1);
        if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    }
    s->nvmem=*p;
    uint64_t g=atomic_load(&s->seq)+1;
    atomic_store(&s->nvmem_owner,o);atomic_store(&s->nvmem_epoch,e);
    atomic_store(&s->nvmem_gen,g);atomic_store(&s->seq,g);
    s->core->persist_state=EldenRtcPersistUncertain;
    atomic_store(&s->nvmem_bound,1);
    /* unwind applies a stop that was posted while busy was held — including
     * inside the publication window itself — so no new binding survives it. */
    if(unwind(s)){
        atomic_store(&s->nvmem_bound,0);atomic_store(&s->nvmem_gen,0);
        s->nvmem=(EldenRtcNvmem){0};
        return EldenRtcStopped;
    }
    *generation=g;return 0;
}
static int bind_alarm_common(EldenRtcOwner *s,const EldenRtcAlarmOwner *p,const char *named,void *irq,uint64_t *generation){
    if(!s||!s->core||!p||!named||!*named||!irq||!generation)return EldenRtcInvalid;
    if(!p->name||!p->owner_epoch||!p->read||!p->write)return EldenRtcInvalid;
    if(atomic_load(&s->stopped))return EldenRtcStopped;
    if(atomic_load(&s->alarm_bound))return EldenRtcOwnerBusy;
    if(claim(s))return EldenRtcOwnerBusy;
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(atomic_load(&s->alarm_bound)){unwind(s);return EldenRtcOwnerBusy;}
    if(atomic_load(&s->seq)==UINT64_MAX){unwind(s);return EldenRtcInvalid;}
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named)){unwind(s);return EldenRtcInvalid;}
    /* Same stop-abort rule as the NVMEM bind: once a stop is pending, no
     * further provider callback, core registration, or publication runs. */
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    uint64_t o=0,e=0;
    if(p->owner_epoch(p->context,&o,&e)||!o){unwind(s);return EldenRtcInvalid;}
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    if(!atomic_load(&s->alarm_registered)){
        int rc=elden_rtc_core_bind_alarm(s->core,&s->alarm_io);
        if(rc){unwind(s);return rc;}
        atomic_store(&s->alarm_registered,1);
        if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);return EldenRtcStopped;}
    }
    s->alarm=*p;
    uint64_t g=atomic_load(&s->seq)+1;
    atomic_store(&s->alarm_owner,o);atomic_store(&s->alarm_epoch,e);
    atomic_store(&s->irq,(uintptr_t)irq);
    atomic_store(&s->alarm_gen,g);atomic_store(&s->seq,g);
    atomic_store(&s->alarm_bound,1);
    if(unwind(s)){
        atomic_store(&s->alarm_bound,0);atomic_store(&s->alarm_gen,0);
        atomic_store(&s->irq,0);
        s->alarm=(EldenRtcAlarmOwner){0};
        return EldenRtcStopped;
    }
    *generation=g;return 0;
}
int elden_rtc_owner_bind_nvmem(EldenRtcOwner *s,const EldenRtcNvmem *p,const char *named,uint64_t *generation){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s))return EldenRtcOwnerBusy;
    int rc=bind_nvmem_common(s,p,named,generation);
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_bind_alarm(EldenRtcOwner *s,const EldenRtcAlarmOwner *p,const char *named,void *irq,uint64_t *generation){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s))return EldenRtcOwnerBusy;
    int rc=bind_alarm_common(s,p,named,irq,generation);
    atomic_store(&s->op,0);return rc;
}
/* Each high-level operation holds the op claim from argument/generation
 * revalidation through the entire core call and final counters, so shared
 * core state (raw, offset, alarm fields, persist_state) can never be mutated
 * by two operations at once. The op claim is distinct from busy: the core's
 * provider I/O still claims busy inside the operation. Reentry or a
 * concurrent operation is refused EldenRtcOwnerBusy — never a wait. */
int elden_rtc_owner_restore(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s)){atomic_fetch_add(&s->rejected,1);return EldenRtcOwnerBusy;}
    int rc=fence_nvmem(s,g);if(!rc)rc=elden_rtc_core_restore_offset(s->core);
    if(!rc)atomic_fetch_add(&s->restores,1);else atomic_fetch_add(&s->rejected,1);
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_learn(EldenRtcOwner *s,uint64_t g,uint64_t seconds){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s)){atomic_fetch_add(&s->rejected,1);return EldenRtcOwnerBusy;}
    int rc=fence_nvmem(s,g);if(!rc)rc=elden_rtc_core_learn_persistent(s->core,seconds);
    if(!rc)atomic_fetch_add(&s->learns,1);else atomic_fetch_add(&s->rejected,1);
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_alarm_set(EldenRtcOwner *s,uint64_t g,uint64_t seconds,int enabled){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s)){atomic_fetch_add(&s->rejected,1);return EldenRtcOwnerBusy;}
    int rc=fence_alarm(s,g);if(!rc)rc=elden_rtc_core_alarm_set(s->core,seconds,enabled);
    if(!rc)atomic_fetch_add(&s->alarm_sets,1);else atomic_fetch_add(&s->rejected,1);
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_alarm_read(EldenRtcOwner *s,uint64_t g,uint64_t *seconds,int *enabled){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s)){atomic_fetch_add(&s->rejected,1);return EldenRtcOwnerBusy;}
    int rc=fence_alarm(s,g);if(!rc)rc=elden_rtc_core_alarm_read(s->core,seconds,enabled);
    if(!rc)atomic_fetch_add(&s->alarm_reads,1);else atomic_fetch_add(&s->rejected,1);
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_alarm_ack(EldenRtcOwner *s,uint64_t g,void *irq){
    if(!s||!s->core)return EldenRtcInvalid;
    if(claim_op(s)){atomic_fetch_add(&s->rejected,1);return EldenRtcOwnerBusy;}
    if(atomic_load(&s->stopped)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcStopped;}
    if(!atomic_load(&s->alarm_bound)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcNoProvider;}
    if(!irq||(uintptr_t)irq!=atomic_load(&s->irq)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcInvalid;}  /* owner IRQ path only */
    if(g!=atomic_load(&s->alarm_gen)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcOwnerStale;}
    if(claim(s)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcOwnerBusy;}
    if(atomic_load(&s->stopped)||atomic_load(&s->stopping)){unwind(s);atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcStopped;}
    if(!atomic_load(&s->alarm_bound)){unwind(s);atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcNoProvider;}
    if(g!=atomic_load(&s->alarm_gen)){unwind(s);atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcOwnerStale;}
    uint64_t o=0,e=0;int oerc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(oerc||o!=atomic_load(&s->alarm_owner)||e!=atomic_load(&s->alarm_epoch))oerc=EldenRtcOwnerStale;
    if(unwind(s)){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return EldenRtcStopped;}
    if(oerc){atomic_fetch_add(&s->rejected,1);atomic_store(&s->op,0);return oerc;}
    int rc=elden_rtc_core_alarm_ack(s->core);
    if(!rc)atomic_fetch_add(&s->acks,1);else atomic_fetch_add(&s->rejected,1);
    atomic_store(&s->op,0);return rc;
}
/* Facade-level stop: gates every guarded facade op and (via the bridges'
 * stopped/stopping fences) every provider I/O, without terminating the live
 * core. The clock itself stops through the core owner's own
 * elden_rtc_core_stop path; keeping the core live is what lets an explicit
 * retire + re-bind reuse the facade for a new qualified provider. A stop
 * posted while a provider callback holds the claim cannot take the claim: it
 * arms stopping and retries once — either the retry applies it or the
 * holder's unwind does, so a deferred stop is never lost. */
int elden_rtc_owner_stop(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    if(!gen_live(s,g))return EldenRtcOwnerStale;
    if(atomic_load(&s->stopped))return EldenRtcStopped;
    if(claim(s)){
        atomic_store(&s->stopping,1);
        if(claim(s))return 0;                       /* deferred: holder's unwind applies it */
        if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
        atomic_store(&s->stopping,0);atomic_store(&s->busy,0);return 0;
    }
    if(!gen_live(s,g)){atomic_store(&s->busy,0);return EldenRtcOwnerStale;}
    if(atomic_load(&s->stopped)){atomic_store(&s->busy,0);return EldenRtcStopped;}
    if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
    atomic_store(&s->busy,0);return 0;
}
/* Ordered reset/retire fence: requires a live generation and a prior stop,
 * and refuses while a provider callback is active so the binding cannot be
 * released out from under an in-flight I/O. A deferred stop still pending on
 * the claim is applied by unwind rather than dropped. A successful retire
 * releases the retained registrations, clears the stopped state and leaves
 * the facade reusable: a later qualified bind re-arms a fresh generation
 * through the already-registered bridge (the live core's one-shot binds are
 * consumed once and stay bound to the indirection, so the stale-generation
 * fence is kept). */
int elden_rtc_owner_retire(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    /* op is held for the whole retire so no high-level operation can start
     * between the generation checks and the clearing — never retire while an
     * operation or a provider callback is in flight. */
    if(claim_op(s))return EldenRtcOwnerBusy;
    int rc;
    if(!gen_live(s,g))rc=EldenRtcOwnerStale;
    else if(claim(s))rc=EldenRtcOwnerBusy;
    else{
        if(!gen_live(s,g))rc=EldenRtcOwnerStale;
        else if(!atomic_load(&s->stopped))rc=EldenRtcOwnerDisabled;   /* ordered fence: stop before retire */
        else{
            s->nvmem=(EldenRtcNvmem){0};s->alarm=(EldenRtcAlarmOwner){0};
            atomic_store(&s->irq,0);
            atomic_store(&s->nvmem_bound,0);atomic_store(&s->alarm_bound,0);
            atomic_store(&s->nvmem_gen,0);atomic_store(&s->alarm_gen,0);
            atomic_store(&s->stopped,0);atomic_store(&s->stopping,0);
            rc=0;
        }
        unwind(s);
    }
    atomic_store(&s->op,0);return rc;
}
int elden_rtc_owner_status(EldenRtcOwner *s,EldenRtcOwnerStatus *out){
    if(!s||!out)return EldenRtcInvalid;
    /* op+busy together mean no operation can be mutating core state and no
     * provider callback can be mid-flight — either the snapshot is coherent
     * or the call is refused Busy without reading the core. */
    if(claim_op(s))return EldenRtcOwnerBusy;
    if(claim(s)){atomic_store(&s->op,0);return EldenRtcOwnerBusy;}
    *out=(EldenRtcOwnerStatus){atomic_load(&s->nvmem_bound),atomic_load(&s->alarm_bound),atomic_load(&s->stopped),
        atomic_load(&s->nvmem_gen),atomic_load(&s->alarm_gen),atomic_load(&s->nvmem_owner),atomic_load(&s->alarm_owner),
        atomic_load(&s->restores),atomic_load(&s->learns),atomic_load(&s->alarm_sets),atomic_load(&s->alarm_reads),
        atomic_load(&s->acks),atomic_load(&s->rejected),atomic_load(&s->dropped),
        s->core?s->core->persist_state:-1};
    unwind(s);atomic_store(&s->op,0);return 0;
}
