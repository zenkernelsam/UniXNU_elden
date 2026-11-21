/* SPDX-License-Identifier: GPL-2.0-only */
#include "Owner.h"
/* Named-identity equality without <string.h> (freestanding profile). */
static int name_eq(const char *a,const char *b){
    if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;
}
/* A generation is live iff it matches a currently-bound provider's armed gen. */
static int gen_live(EldenRtcOwner *s,uint64_t g){
    return g&&((s->nvmem_bound&&g==s->nvmem_gen)||(s->alarm_bound&&g==s->alarm_gen));
}
/* Bridged provider I/O: the core calls these; they serialize the provider
 * callback, re-fence owner/epoch right before the I/O, and report stop-during-
 * callback as an error so the existing core marks uncertainty instead of
 * publishing a committed result. busy fences any reentrant facade call. */
/* A stop landing inside owner_epoch must not let the bridge reach the
 * provider read/write: check stopped before forwarding the I/O. */
static int bridge_pr(void *c,uint8_t out[4]){
    EldenRtcOwner *s=c;
    if(!s->nvmem_bound||s->stopped||s->busy)return EldenRtcPersistError;
    s->busy=1;uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);
    if(s->stopped){s->busy=0;s->dropped++;return EldenRtcPersistError;}
    if(rc||o!=s->nvmem_owner||e!=s->nvmem_epoch)rc=EldenRtcPersistError;
    else rc=s->nvmem.read(s->nvmem.context,out)?EldenRtcPersistError:0;
    s->busy=0;
    if(s->stopped){s->dropped++;return EldenRtcPersistError;}
    return rc;
}
static int bridge_pw(void *c,const uint8_t data[4]){
    EldenRtcOwner *s=c;
    if(!s->nvmem_bound||s->stopped||s->busy)return EldenRtcPersistError;
    s->busy=1;uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);
    if(s->stopped){s->busy=0;s->dropped++;return EldenRtcPersistError;}
    if(rc||o!=s->nvmem_owner||e!=s->nvmem_epoch)rc=EldenRtcPersistError;
    else rc=s->nvmem.write(s->nvmem.context,data)?EldenRtcPersistError:0;
    s->busy=0;
    if(s->stopped){s->dropped++;return EldenRtcPersistError;}
    return rc;
}
static int bridge_ar(void *c,uint16_t a,uint8_t *out,size_t n){
    EldenRtcOwner *s=c;
    if(!s->alarm_bound||s->stopped||s->busy)return EldenRtcAlarmError;
    s->busy=1;uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(s->stopped){s->busy=0;s->dropped++;return EldenRtcAlarmError;}
    if(rc||o!=s->alarm_owner||e!=s->alarm_epoch)rc=EldenRtcAlarmError;
    else rc=s->alarm.read(s->alarm.context,a,out,n)?EldenRtcAlarmError:0;
    s->busy=0;
    if(s->stopped){s->dropped++;return EldenRtcAlarmError;}
    return rc;
}
static int bridge_aw(void *c,uint16_t a,const uint8_t *d,size_t n){
    EldenRtcOwner *s=c;
    if(!s->alarm_bound||s->stopped||s->busy)return EldenRtcAlarmError;
    s->busy=1;uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);
    if(s->stopped){s->busy=0;s->dropped++;return EldenRtcAlarmError;}
    if(rc||o!=s->alarm_owner||e!=s->alarm_epoch)rc=EldenRtcAlarmError;
    else rc=s->alarm.write(s->alarm.context,a,d,n)?EldenRtcAlarmError:0;
    s->busy=0;
    if(s->stopped){s->dropped++;return EldenRtcAlarmError;}
    return rc;
}
int elden_rtc_owner_init(EldenRtcOwner *s,EldenRtc *core){
    if(!s||!core||!core->initialized)return EldenRtcInvalid;
    EldenRtcOwner zero={0};*s=zero;s->core=core;
    s->persist_io=(EldenRtcPersistence){s,bridge_pr,bridge_pw};
    s->alarm_io=(EldenRtcAlarmIo){s,bridge_ar,bridge_aw};
    return 0;
}
/* Per-call fence before any core/provider work: stopped dominates, then the
 * binding must exist, the generation must be the armed one, no callback may be
 * in flight, and owner/epoch must still match. owner_epoch is itself a provider
 * callback, so it is serialized under busy and a stop landing inside it is
 * caught on the way out. */
static int fence_nvmem(EldenRtcOwner *s,uint64_t g){
    if(s->stopped)return EldenRtcStopped;
    if(!s->nvmem_bound)return EldenRtcNoProvider;
    if(g!=s->nvmem_gen)return EldenRtcOwnerStale;
    if(s->busy)return EldenRtcOwnerBusy;
    s->busy=1;uint64_t o=0,e=0;int rc=s->nvmem.owner_epoch(s->nvmem.context,&o,&e);s->busy=0;
    if(s->stopped)return EldenRtcStopped;
    if(rc||o!=s->nvmem_owner||e!=s->nvmem_epoch)return EldenRtcOwnerStale;
    return 0;
}
static int fence_alarm(EldenRtcOwner *s,uint64_t g){
    if(s->stopped)return EldenRtcStopped;
    if(!s->alarm_bound)return EldenRtcNoProvider;
    if(g!=s->alarm_gen)return EldenRtcOwnerStale;
    if(s->busy)return EldenRtcOwnerBusy;
    s->busy=1;uint64_t o=0,e=0;int rc=s->alarm.owner_epoch(s->alarm.context,&o,&e);s->busy=0;
    if(s->stopped)return EldenRtcStopped;
    if(rc||o!=s->alarm_owner||e!=s->alarm_epoch)return EldenRtcOwnerStale;
    return 0;
}
static int bind_nvmem_common(EldenRtcOwner *s,const EldenRtcNvmem *p,const char *named,uint64_t *generation){
    if(!s||!s->core||!p||!named||!*named||!generation)return EldenRtcInvalid;
    if(!p->name||!p->owner_epoch||!p->read||!p->write)return EldenRtcInvalid;
    if(s->stopped)return EldenRtcStopped;
    if(s->nvmem_bound)return EldenRtcOwnerBusy;
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named))return EldenRtcInvalid;
    uint64_t o=0,e=0;if(p->owner_epoch(p->context,&o,&e)||!o)return EldenRtcInvalid;
    if(!s->nvmem_registered){int rc=elden_rtc_core_bind_persistence(s->core,&s->persist_io);if(rc)return rc;s->nvmem_registered=1;}
    s->nvmem=*p;s->nvmem_owner=o;s->nvmem_epoch=e;s->nvmem_gen=++s->seq;s->nvmem_bound=1;
    s->core->persist_state=EldenRtcPersistUncertain;*generation=s->nvmem_gen;return 0;
}
static int bind_alarm_common(EldenRtcOwner *s,const EldenRtcAlarmOwner *p,const char *named,void *irq,uint64_t *generation){
    if(!s||!s->core||!p||!named||!*named||!irq||!generation)return EldenRtcInvalid;
    if(!p->name||!p->owner_epoch||!p->read||!p->write)return EldenRtcInvalid;
    if(s->stopped)return EldenRtcStopped;
    if(s->alarm_bound)return EldenRtcOwnerBusy;
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named))return EldenRtcInvalid;
    uint64_t o=0,e=0;if(p->owner_epoch(p->context,&o,&e)||!o)return EldenRtcInvalid;
    if(!s->alarm_registered){int rc=elden_rtc_core_bind_alarm(s->core,&s->alarm_io);if(rc)return rc;s->alarm_registered=1;}
    s->alarm=*p;s->alarm_owner=o;s->alarm_epoch=e;s->irq=irq;s->alarm_gen=++s->seq;s->alarm_bound=1;
    *generation=s->alarm_gen;return 0;
}
int elden_rtc_owner_bind_nvmem(EldenRtcOwner *s,const EldenRtcNvmem *p,const char *named,uint64_t *generation){
    return bind_nvmem_common(s,p,named,generation);
}
int elden_rtc_owner_bind_alarm(EldenRtcOwner *s,const EldenRtcAlarmOwner *p,const char *named,void *irq,uint64_t *generation){
    return bind_alarm_common(s,p,named,irq,generation);
}
int elden_rtc_owner_restore(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    int rc=fence_nvmem(s,g);if(rc){s->rejected++;return rc;}
    rc=elden_rtc_core_restore_offset(s->core);if(!rc)s->restores++;else s->rejected++;return rc;
}
int elden_rtc_owner_learn(EldenRtcOwner *s,uint64_t g,uint64_t seconds){
    if(!s||!s->core)return EldenRtcInvalid;
    int rc=fence_nvmem(s,g);if(rc){s->rejected++;return rc;}
    rc=elden_rtc_core_learn_persistent(s->core,seconds);if(!rc)s->learns++;else s->rejected++;return rc;
}
int elden_rtc_owner_alarm_set(EldenRtcOwner *s,uint64_t g,uint64_t seconds,int enabled){
    if(!s||!s->core)return EldenRtcInvalid;
    int rc=fence_alarm(s,g);if(rc){s->rejected++;return rc;}
    rc=elden_rtc_core_alarm_set(s->core,seconds,enabled);if(!rc)s->alarm_sets++;else s->rejected++;return rc;
}
int elden_rtc_owner_alarm_read(EldenRtcOwner *s,uint64_t g,uint64_t *seconds,int *enabled){
    if(!s||!s->core)return EldenRtcInvalid;
    int rc=fence_alarm(s,g);if(rc){s->rejected++;return rc;}
    rc=elden_rtc_core_alarm_read(s->core,seconds,enabled);if(!rc)s->alarm_reads++;else s->rejected++;return rc;
}
int elden_rtc_owner_alarm_ack(EldenRtcOwner *s,uint64_t g,void *irq){
    if(!s||!s->core)return EldenRtcInvalid;
    if(s->stopped){s->rejected++;return EldenRtcStopped;}
    if(!s->alarm_bound){s->rejected++;return EldenRtcNoProvider;}
    if(!irq||irq!=s->irq){s->rejected++;return EldenRtcInvalid;}  /* owner IRQ path only */
    if(g!=s->alarm_gen){s->rejected++;return EldenRtcOwnerStale;}
    if(s->busy){s->rejected++;return EldenRtcOwnerBusy;}
    s->busy=1;uint64_t o=0,e=0;int oerc=s->alarm.owner_epoch(s->alarm.context,&o,&e);s->busy=0;
    if(s->stopped){s->rejected++;return EldenRtcStopped;}
    if(oerc||o!=s->alarm_owner||e!=s->alarm_epoch){s->rejected++;return EldenRtcOwnerStale;}
    int rc=elden_rtc_core_alarm_ack(s->core);if(!rc)s->acks++;else s->rejected++;return rc;
}
/* Facade-level stop: gates every guarded facade op and (via the bridges'
 * stopped checks) every provider I/O, without terminating the live core. The
 * clock itself stops through the core owner's own elden_rtc_core_stop path;
 * keeping the core live is what lets an explicit retire + re-bind reuse the
 * facade for a new qualified provider. */
int elden_rtc_owner_stop(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    if(!gen_live(s,g))return EldenRtcOwnerStale;
    if(s->stopped)return EldenRtcStopped;
    s->stopped=1;s->stops++;return 0;
}
/* Ordered reset/retire fence: requires a live generation and a prior stop,
 * and refuses while a provider callback is active so the binding cannot be
 * released out from under an in-flight I/O. A successful retire releases the
 * retained registrations, clears the stopped state and leaves the facade
 * reusable: a later qualified bind re-arms a fresh generation through the
 * already-registered bridge (the live core's one-shot binds are consumed once
 * and stay bound to the indirection, so the stale-generation fence is kept). */
int elden_rtc_owner_retire(EldenRtcOwner *s,uint64_t g){
    if(!s||!s->core)return EldenRtcInvalid;
    if(!gen_live(s,g))return EldenRtcOwnerStale;
    if(s->busy)return EldenRtcOwnerBusy;             /* never retire inside a provider callback */
    if(!s->stopped)return EldenRtcOwnerDisabled;   /* ordered fence: stop before retire */
    s->nvmem_bound=0;s->alarm_bound=0;s->nvmem_gen=0;s->alarm_gen=0;
    s->nvmem=(EldenRtcNvmem){0};s->alarm=(EldenRtcAlarmOwner){0};s->irq=0;s->stopped=0;return 0;
}
int elden_rtc_owner_status(const EldenRtcOwner *s,EldenRtcOwnerStatus *out){
    if(!s||!out)return EldenRtcInvalid;
    *out=(EldenRtcOwnerStatus){s->nvmem_bound,s->alarm_bound,s->stopped,
        s->nvmem_gen,s->alarm_gen,s->nvmem_owner,s->alarm_owner,
        s->restores,s->learns,s->alarm_sets,s->alarm_reads,s->acks,s->rejected,s->dropped,
        s->core?s->core->persist_state:-1};
    return 0;
}
