/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Owner.h"
/* Named-identity equality without <string.h> (freestanding profile). */
static int name_eq(const char *a,const char *b){
    if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;
}
static int gen_live(const EldenPenOwner *s,uint64_t g){return g&&s->bound&&g==s->gen;}
static void mark_now(EldenPenOwner *s,uint64_t now){if(now>s->last_now)s->last_now=now;}
/* A deferred stop runs the existing delivery stop/release path once the
 * in-flight operation has unwound out of the provider callback. The release
 * then goes through the caller's send() exactly like a top-level stop. */
static int unwind_stop(EldenPenOwner *s){
    if(!s->stop_deferred)return 0;
    s->stop_deferred=0;
    return elden_pen_delivery_stop(s->delivery,s->stop_now,s->send,s->send_context);
}
/* Per-operation fence: stopped dominates, then the binding must exist, the
 * generation must be the armed one, and no callback may be in flight. The
 * provider's owner/epoch is rechecked inside the held busy region, so every
 * facade operation — not just packet pulls — re-qualifies the worker identity
 * before touching it or the delivery. On success busy stays held; the caller
 * must run guard_exit, after which a deferred stop (if any) unwinds. */
static int guard_enter(EldenPenOwner *s,uint64_t g){
    if(s->stopped)return EldenPenStopped;
    if(!s->bound)return EldenPenNoProvider;
    if(g!=s->gen)return EldenPenOwnerStale;
    if(s->busy)return EldenPenOwnerBusy;
    s->busy=1;uint64_t o=0,e=0;
    int rc=s->provider.owner_epoch(s->provider.context,&o,&e);
    /* A stop landing inside owner_epoch is unwound here, before the operation
     * itself runs — the release must still reach the delivery/send path. */
    if(s->stopped){s->busy=0;unwind_stop(s);return EldenPenStopped;}
    if(rc||o!=s->owner||e!=s->epoch){
        /* Drift first observed by any op on the armed generation: refuse the
         * operation, issue no further provider I/O, and lift any contact the
         * session still holds through the existing neutral release at the
         * last accepted monotonic time. busy stays held around send() so a
         * stop inside it still defers; a failed lift remains pending for
         * elden_pen_owner_release and the op may surface the send error.
         * Reaching here means g==s->gen — a stale/foreign generation token
         * was already refused above without touching the session. */
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        s->busy=0;int src=unwind_stop(s);
        return src?src:(x?x:EldenPenOwnerStale);
    }
    return 0;
}
static void guard_exit(EldenPenOwner *s){s->busy=0;}
/* Refusal release on a live generation: a stale owner/epoch, foreign frame
 * owner or rolled-back worker timestamp/sequence refuses the packet, but any
 * contact the current session already delivered must not stay held down. The
 * lift reuses the existing neutral release at the last accepted monotonic
 * time; a failed emit leaves release_pending for elden_pen_owner_release to
 * retry — never a fabricated frame and never a false released state. busy is
 * (re)armed around send() so a stop landing inside it defers, exactly like the
 * decode/transport fault branches. */
static int refuse_release(EldenPenOwner *s){
    s->busy=1;
    int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
    s->busy=0;int src=unwind_stop(s);return src?src:x;
}
int elden_pen_owner_init(EldenPenOwner *s,EldenPenDelivery *d,const EldenPenLimits *l,
                         EldenPenSend send,void *send_context){
    if(!s||!d||!l||!send||!d->stale_us)return EldenPenDeliveryArgument;
    /* Same admission the decoder applies to limits; reject an unusable range
     * here rather than faulting every later packet. */
    if(!l->max_x||!l->max_y||(l->max_pressure!=4095&&l->max_pressure!=8191)||
       !l->max_tilt||l->max_tilt>90)return EldenPenDeliveryArgument;
    EldenPenOwner z={0};*s=z;
    s->delivery=d;s->send=send;s->send_context=send_context;s->limits=*l;return 0;
}
int elden_pen_owner_bind(EldenPenOwner *s,const EldenPenProvider *p,const char *named,
                         uint64_t frame_owner,uint64_t frame_epoch,uint64_t *generation){
    if(!s||!s->delivery||!p||!named||!*named||!generation)return EldenPenDeliveryArgument;
    if(!p->name||!p->owner_epoch||!p->frame_owner||!p->packet)return EldenPenDeliveryArgument;
    if(s->stopped)return EldenPenStopped;
    if(s->bound)return EldenPenOwnerBusy;
    /* The explicit touch frame-owner association is mandatory and nonzero. */
    if(!frame_owner)return EldenPenDeliveryArgument;
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named))return EldenPenDeliveryArgument;
    uint64_t o=0,e=0;
    if(p->owner_epoch(p->context,&o,&e)||!o)return EldenPenDeliveryArgument;
    /* The provider's own frame-owner report must already equal the declared
     * association — a fabricated association is foreign, not bound. */
    uint64_t fo=0,fe=0;
    if(p->frame_owner(p->context,&fo,&fe)||fo!=frame_owner||fe!=frame_epoch)return EldenPenOwnerForeign;
    s->provider=*p;s->owner=o;s->epoch=e;s->frame_owner=frame_owner;s->frame_epoch=frame_epoch;
    s->gen=++s->seq;s->bound=1;s->have_seq=0;s->last_ts=0;s->last_seq=0;
    *generation=s->gen;return 0;
}
int elden_pen_owner_packet(EldenPenOwner *s,uint64_t g){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    /* guard_enter already issued the drift release for owner/epoch loss on
     * this live generation — no second lift is attempted here. */
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    /* A paused or terminally stopped delivery must not consume a provider
     * packet at all — the sample is refused before any provider I/O so no
     * contact bytes are pulled and silently lost. */
    if(s->delivery->stopped){guard_exit(s);return EldenPenStopped;}
    if(s->delivery->paused){guard_exit(s);return EldenPenPaused;}
    uint8_t raw[EldenPenPacketBytes];uint64_t ts=0,seq=0,fo=0,fe=0;
    int prc=s->provider.packet(s->provider.context,raw,&ts,&seq);
    if(s->stopped){guard_exit(s);s->dropped++;int x=unwind_stop(s);return x?x:EldenPenStopped;}
    if(prc){
        /* Transport fault: no packet was sampled. Drive the existing fault
         * path at the facade's last known clock — never a synthetic lift. busy
         * stays held so a stop inside send() defers instead of nesting send. */
        s->faults++;
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        guard_exit(s);int src=unwind_stop(s);return src?src:(x?x:prc);
    }
    int frc=s->provider.frame_owner(s->provider.context,&fo,&fe);
    if(s->stopped){guard_exit(s);s->dropped++;int x=unwind_stop(s);return x?x:EldenPenStopped;}
    if(frc||fo!=s->frame_owner||fe!=s->frame_epoch){
        /* Foreign frame owner on the live generation: the packet is refused,
         * but a held current contact must still be lifted. */
        s->foreign_frames++;s->rejected++;
        int x=refuse_release(s);return x?x:EldenPenOwnerForeign;
    }
    /* Monotonic worker timestamp/sequence fences: the packet is refused
     * (not faulted) — a rollback is a fencing violation, not pen data — and
     * any held contact is lifted through the same refusal release. */
    if(s->have_seq&&seq<=s->last_seq){s->stale_seq++;s->rejected++;int x=refuse_release(s);return x?x:EldenPenOwnerStale;}
    if(s->have_seq&&ts<s->last_ts){s->stale_ts++;s->rejected++;int x=refuse_release(s);return x?x:EldenPenOwnerStale;}
    s->have_seq=1;s->last_seq=seq;s->last_ts=ts;s->packets++;
    EldenPenFrame f={0};
    int drc=elden_pen_decode(raw,EldenPenPacketBytes,&s->limits,&f);
    if(drc){
        /* Malformed/out-of-range packet: the existing transport-fault path,
         * matching elden_pen_native_packet's decode-failure behavior. busy
         * stays held across send() for the same deferred-stop reason. */
        s->faults++;
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        guard_exit(s);int src=unwind_stop(s);return src?src:(x?x:drc);
    }
    s->decoded++;if(f.kind==EldenPenIdentity||f.kind==EldenPenInfo)s->metadata++;
    mark_now(s,ts);
    rc=elden_pen_delivery_feed(s->delivery,&f,ts,s->send,s->send_context);
    guard_exit(s);
    int src=unwind_stop(s);
    if(src)return src;
    if(s->stopped)return EldenPenStopped;  /* a stop landed inside send() */
    return rc;
}
int elden_pen_owner_tick(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_tick(s->delivery,now,s->send,s->send_context);
    guard_exit(s);
    int src=unwind_stop(s);return src?src:rc;
}
int elden_pen_owner_fault(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_fault(s->delivery,now,s->send,s->send_context);
    s->faults++;guard_exit(s);
    int src=unwind_stop(s);return src?src:rc;
}
int elden_pen_owner_pause(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_pause(s->delivery,now,s->send,s->send_context);
    if(!rc)s->pauses++;guard_exit(s);
    int src=unwind_stop(s);return src?src:rc;
}
int elden_pen_owner_resume(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_resume(s->delivery,now);
    if(!rc)s->resumes++;guard_exit(s);
    int src=unwind_stop(s);return src?src:rc;
}
/* Facade-level stop: requires a live generation, gates every later operation
 * and provider I/O, and drives the existing delivery stop/release path. A stop
 * issued from inside a provider/send callback only sets the flag — the release
 * is deferred to the in-flight operation's unwind so send() is never reentered
 * from within a provider callback. */
int elden_pen_owner_stop(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    if(!gen_live(s,g))return EldenPenOwnerStale;
    if(s->stopped)return EldenPenStopped;
    s->stopped=1;s->stops++;s->stop_now=now;mark_now(s,now);
    if(s->busy){s->stop_deferred=1;return 0;}
    return elden_pen_delivery_stop(s->delivery,now,s->send,s->send_context);
}
/* Cleanup retry that needs no provider callback and no new sample: it only
 * re-runs the existing neutral release at the last accepted monotonic time
 * through the bound send(). Stop is terminal for new packet delivery, not for
 * this release path — a pending lift after stop, or a lift that lost a send
 * race inside a refused packet, is completed here. A stale/foreign generation
 * is fenced before anything mutates, so an old session can never release or
 * disturb the current contact. A failed send keeps release_pending; the
 * facade never reports released until the delivery itself confirms. */
int elden_pen_owner_release(EldenPenOwner *s,uint64_t g){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    if(!gen_live(s,g))return EldenPenOwnerStale;
    if(s->busy)return EldenPenOwnerBusy;
    s->busy=1;
    int rc=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
    s->busy=0;int src=unwind_stop(s);return src?src:rc;
}
int elden_pen_owner_retire(EldenPenOwner *s,uint64_t g){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    if(!gen_live(s,g))return EldenPenOwnerStale;
    if(s->busy)return EldenPenOwnerBusy;             /* never retire inside a callback */
    if(!s->stopped)return EldenPenOwnerDisabled;   /* ordered fence: stop before retire */
    /* Confirmed-release fence: a pending or undelivered lift must be finished
     * through elden_pen_owner_release first — retire never drops a contact the
     * peer still believes is down. */
    if(!elden_pen_delivery_released(s->delivery))return EldenPenOwnerDisabled;
    /* Re-arm the caller-owned delivery to its init state (stale_us kept) so a
     * later bind starts clean without the caller reinitializing it. */
    int irc=elden_pen_delivery_init(s->delivery,s->delivery->stale_us);if(irc)return irc;
    s->bound=0;s->gen=0;s->stopped=0;s->stop_deferred=0;s->have_seq=0;
    s->last_ts=0;s->last_seq=0;s->last_now=0;
    s->provider=(EldenPenProvider){0};s->owner=0;s->epoch=0;
    s->frame_owner=0;s->frame_epoch=0;return 0;
}
int elden_pen_owner_released(const EldenPenOwner *s){
    return s&&s->delivery&&elden_pen_delivery_released(s->delivery);
}
int elden_pen_owner_status(const EldenPenOwner *s,EldenPenOwnerStatus *out){
    if(!s||!out||!s->delivery)return EldenPenDeliveryArgument;
    *out=(EldenPenOwnerStatus){s->bound,s->stopped,s->busy,
        s->gen,s->owner,s->epoch,s->frame_owner,s->frame_epoch,s->last_ts,s->last_seq,
        s->packets,s->decoded,s->metadata,s->faults,s->pauses,s->resumes,s->stops,
        s->rejected,s->dropped,s->stale_ts,s->stale_seq,s->foreign_frames,
        s->delivery->sent,s->delivery->failures,
        s->delivery->paused,elden_pen_delivery_released(s->delivery),0};
    return 0;
}
