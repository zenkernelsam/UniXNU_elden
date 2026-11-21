/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PowerOwner.h"
/* Named-identity equality without <string.h> (freestanding profile). */
static int name_eq(const char *a,const char *b){
    if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;
}
/* Serialized owner/epoch probe under the busy claim: failure or drift is a
 * Stale fence before any provider callback result is trusted. */
static int probe(EldenWakePowerOwner *s){
    uint64_t o=0,e=0;int rc=s->provider.owner_epoch(s->provider.context,&o,&e);
    return rc||o!=atomic_load(&s->owner)||e!=atomic_load(&s->epoch)?EldenWakePowerStale:0;
}
/* Non-blocking serialization: one callback or lifecycle op at a time. The
 * exchange is the fence — a same-thread reentry (synchronous self-ack) and a
 * foreign-thread caller get the identical Busy refusal; nothing ever waits. */
static int claim(EldenWakePowerOwner *s){return atomic_exchange(&s->busy,1)?EldenWakePowerBusy:0;}
/* Release the claim, then apply a deferred stop: busy=0 is stored first so a
 * stop whose exchange saw it held set `stopping`; loads after the store see
 * it. If a stop instead claimed the freed claim itself it already applied
 * stopped — either way the stop lands, never lost. */
static int unwind(EldenWakePowerOwner *s){
    atomic_store(&s->busy,0);
    if(atomic_load(&s->stopping)){
        if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
        atomic_store(&s->stopping,0);return EldenWakePowerDisabled;
    }
    return 0;
}
/* Installed EldenTouchWakePower bridges: the original touch worker calls these
 * through the coordinator. They fence the bound owner/epoch arguments, claim
 * the non-blocking serialization, re-fence bound/stopped inside it (a retire
 * or stop may have landed between the first check and the claim), run the
 * serialized provider probe and callback, and propagate the provider's actual
 * return code — no admission or queue result is fabricated. */
static int bridge_admit(void *v,uint64_t owner,uint64_t epoch,unsigned phase){
    EldenWakePowerOwner *s=v;
    if(!atomic_load(&s->bound))return EldenWakePowerDisabled;
    if(owner!=atomic_load(&s->owner)||epoch!=atomic_load(&s->epoch))return EldenWakePowerStale;
    if(claim(s))return EldenWakePowerBusy;
    if(!atomic_load(&s->bound)||atomic_load(&s->stopped)){unwind(s);return EldenWakePowerDisabled;}
    int rc=probe(s);atomic_fetch_add(&s->admits,1);
    if(!rc)rc=s->provider.admit(s->provider.context,owner,epoch,phase);
    int end=unwind(s);return end?end:rc;
}
static int bridge_request(void *v,uint64_t owner,uint64_t epoch,unsigned gesture){
    EldenWakePowerOwner *s=v;
    if(!atomic_load(&s->bound))return EldenWakePowerDisabled;
    if(owner!=atomic_load(&s->owner)||epoch!=atomic_load(&s->epoch))return EldenWakePowerStale;
    if(claim(s))return EldenWakePowerBusy;
    if(!atomic_load(&s->bound)||atomic_load(&s->stopped)){unwind(s);return EldenWakePowerDisabled;}
    int rc=probe(s);
    if(!rc&&atomic_load(&s->pending))rc=EldenWakePowerBusy; /* single outstanding request */
    if(!rc){rc=s->provider.request_wake(s->provider.context,owner,epoch,gesture);
        if(!rc){atomic_fetch_add(&s->requests,1);atomic_store(&s->pending,1);}}
    int end=unwind(s);return end?end:rc;
}
int elden_wake_power_bind(EldenWakePowerOwner *s,const EldenWakePowerProvider *p,
    const char *named,int (*resume)(void *,uint64_t,uint64_t),void *resume_context,
    uint64_t *generation){
    if(!s||!p||!named||!*named||!resume||!generation)return EldenWakePowerInvalid;
    if(!p->name||!p->owner_epoch||!p->admit||!p->request_wake)return EldenWakePowerInvalid;
    if(atomic_load(&s->bound))return EldenWakePowerInvalid;
    if(claim(s))return EldenWakePowerBusy;
    /* Provider callbacks (name probe included) run serialized under the claim:
     * a concurrent bind can never reach provider I/O while one is active.
     * seq exhaustion refuses before any provider call — nothing is armed and
     * generation is left untouched rather than wrapping to a reused value. */
    int rc;
    if(atomic_load(&s->bound)||atomic_load(&s->seq)==UINT64_MAX)rc=EldenWakePowerInvalid;
    else if(!name_eq(p->name(p->context),named))rc=EldenWakePowerInvalid;
    else{
        uint64_t o=0,e=0;rc=p->owner_epoch(p->context,&o,&e);
        if(!rc&&(!o||!e))rc=EldenWakePowerInvalid;
        if(!rc){
            s->provider=*p;s->resume=resume;s->resume_context=resume_context;
            atomic_store(&s->owner,o);atomic_store(&s->epoch,e);
            atomic_store(&s->seq,atomic_load(&s->seq)+1);
            atomic_store(&s->generation,atomic_load(&s->seq));
            atomic_store(&s->pending,0);atomic_store(&s->stopped,0);atomic_store(&s->stopping,0);
            s->installed.context=s;s->installed.owner=o;s->installed.epoch=e;
            s->installed.admit=bridge_admit;s->installed.request_wake=bridge_request;
            atomic_store(&s->bound,1); /* last: readers see a fully armed provider */
            atomic_store(&s->busy,0);*generation=atomic_load(&s->generation);return 0;
        }
    }
    atomic_fetch_add(&s->rejected,1);unwind(s);return rc;
}
int elden_wake_power_ready(EldenWakePowerOwner *s,uint64_t generation,int ready){
    if(!s||(ready!=0&&ready!=1))return EldenWakePowerInvalid;
    if(!atomic_load(&s->bound)||generation!=atomic_load(&s->generation)){
        atomic_fetch_add(&s->rejected,1);return EldenWakePowerStale;}
    if(claim(s)){atomic_fetch_add(&s->rejected,1);return EldenWakePowerBusy;}
    if(!atomic_load(&s->bound)||generation!=atomic_load(&s->generation)){
        atomic_fetch_add(&s->rejected,1);unwind(s);return EldenWakePowerStale;}
    if(atomic_load(&s->stopped)){
        atomic_fetch_add(&s->rejected,1);unwind(s);return EldenWakePowerDisabled;}
    int rc=probe(s);
    if(!rc&&!atomic_load(&s->pending))rc=EldenWakePowerBusy;
    if(rc)atomic_fetch_add(&s->rejected,1);
    else{
        atomic_store(&s->pending,0);atomic_fetch_add(&s->readies,1);
        if(!ready)atomic_fetch_add(&s->not_ready,1);
        else{atomic_fetch_add(&s->resumes,1);
            rc=s->resume(s->resume_context,atomic_load(&s->owner),atomic_load(&s->epoch));}
    }
    int end=unwind(s);return end?end:rc;
}
int elden_wake_power_stop(EldenWakePowerOwner *s,uint64_t generation){
    if(!s)return EldenWakePowerInvalid;
    if(!atomic_load(&s->bound)||generation!=atomic_load(&s->generation)){
        atomic_fetch_add(&s->rejected,1);return EldenWakePowerStale;}
    if(atomic_load(&s->stopped))return EldenWakePowerDisabled;
    if(claim(s)){
        /* A callback or lifecycle op holds the claim: arm the deferred stop,
         * then retry once — the holder may have unwound between our exchange
         * and the store, in which case nobody else will apply it. */
        atomic_store(&s->stopping,1);
        if(claim(s))return 0;
        if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
        atomic_store(&s->stopping,0);atomic_store(&s->busy,0);return 0;
    }
    if(!atomic_exchange(&s->stopped,1))atomic_fetch_add(&s->stops,1);
    atomic_store(&s->busy,0);return 0;
}
int elden_wake_power_retire(EldenWakePowerOwner *s,uint64_t generation){
    if(!s)return EldenWakePowerInvalid;
    if(!atomic_load(&s->bound)||generation!=atomic_load(&s->generation)){
        atomic_fetch_add(&s->rejected,1);return EldenWakePowerStale;}
    if(claim(s)){atomic_fetch_add(&s->rejected,1);return EldenWakePowerBusy;}
    if(!atomic_load(&s->bound)||generation!=atomic_load(&s->generation)){
        atomic_fetch_add(&s->rejected,1);unwind(s);return EldenWakePowerStale;}
    if(!atomic_load(&s->stopped)){
        atomic_fetch_add(&s->rejected,1);unwind(s);return EldenWakePowerBusy;}
    EldenTouchWakePower none={0};s->installed=none;
    s->provider.context=NULL;s->provider.name=NULL;s->provider.owner_epoch=NULL;
    s->provider.admit=NULL;s->provider.request_wake=NULL;
    s->resume=NULL;s->resume_context=NULL;
    atomic_store(&s->owner,0);atomic_store(&s->epoch,0);atomic_store(&s->generation,0);
    atomic_store(&s->pending,0);atomic_store(&s->stopping,0);atomic_store(&s->stopped,0);
    atomic_store(&s->bound,0);atomic_store(&s->busy,0);return 0;
}
int elden_wake_power_status(const EldenWakePowerOwner *s,EldenWakePowerStatus *out){
    if(!s||!out)return EldenWakePowerInvalid;
    out->bound=atomic_load(&s->bound);out->stopped=atomic_load(&s->stopped);
    out->pending=atomic_load(&s->pending);
    out->generation=atomic_load(&s->generation);out->owner=atomic_load(&s->owner);
    out->epoch=atomic_load(&s->epoch);
    out->admits=atomic_load(&s->admits);out->requests=atomic_load(&s->requests);
    out->readies=atomic_load(&s->readies);out->resumes=atomic_load(&s->resumes);
    out->not_ready=atomic_load(&s->not_ready);out->rejected=atomic_load(&s->rejected);
    out->stops=atomic_load(&s->stops);return 0;
}
