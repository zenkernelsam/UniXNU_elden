/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Owner.h"
/* Named-identity equality without <string.h> (freestanding profile). */
static int name_eq(const char *a,const char *b){
    if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;
}
static int gen_live(const EldenPenOwner *s,uint64_t g){
    return g&&s->bound&&g==s->gen;
}
/* A non-zero armed generation is "stopped"; 0 never matches (a cleared or
 * not-yet-armed facade). The tag means a stop that races a retire into the
 * clear window can never poison the successor binding. */
static int is_stopped(const EldenPenOwner *s){
    uint64_t g=s->gen;return g&&g==s->stopped;
}
static void mark_now(EldenPenOwner *s,uint64_t now){
    uint64_t cur=s->last_now;   /* monotonic max under concurrent stop() */
    while(cur<now&&!atomic_compare_exchange_weak(&s->last_now,&cur,now)){}
}
/* busy is the one nonblocking claim — an atomic_exchange held for the whole
 * facade operation (provider probe, provider/send callbacks, deferred-stop
 * unwind). There is no waiting anywhere: a contender gets EldenPenOwnerBusy. */
static int claim(EldenPenOwner *s){return atomic_exchange(&s->busy,1);}
/* Release the claim, first consuming a deferred stop whose armed generation is
 * still live: the existing delivery stop/release path runs under the claim so
 * its send() serializes exactly like a top-level stop. A deferral armed for a
 * generation retire already cleared is consumed and dropped — that binding is
 * gone, so the posted stop is moot. Returns the deferred release's rc (the
 * in-flight op may surface the send error). */
static int unwind(EldenPenOwner *s){
    int x=0;
    uint64_t dg=atomic_exchange(&s->stop_deferred,0);
    if(dg&&dg==s->gen)x=elden_pen_delivery_stop(s->delivery,s->stop_now,s->send,s->send_context);
    s->busy=0;return x;
}
/* Per-operation fence: stopped dominates, then the binding must exist and the
 * generation must be the armed one, then the claim is taken — everything
 * re-fenced under it so a stop/retire landing between the pre-checks and the
 * claim is still caught before the provider is touched. The provider's
 * owner/epoch is rechecked inside the held claim, so every facade operation —
 * not just packet pulls — re-qualifies the worker identity before touching it
 * or the delivery. On success the claim stays held; the caller must run
 * unwind, which applies any deferred stop before releasing busy. */
static int guard_enter(EldenPenOwner *s,uint64_t g){
    if(is_stopped(s))return EldenPenStopped;
    if(!s->bound)return EldenPenNoProvider;
    if(!g||g!=s->gen)return EldenPenOwnerStale;
    if(claim(s))return EldenPenOwnerBusy;
    if(is_stopped(s)){unwind(s);return EldenPenStopped;}
    if(!s->bound){unwind(s);return EldenPenNoProvider;}
    if(g!=s->gen){unwind(s);return EldenPenOwnerStale;}
    uint64_t o=0,e=0;
    int rc=s->provider.owner_epoch(s->provider.context,&o,&e);
    /* A stop landing inside owner_epoch is consumed by unwind here, before the
     * operation itself runs — the deferred release still reaches the
     * delivery/send path. */
    if(is_stopped(s)){unwind(s);return EldenPenStopped;}
    if(rc||o!=s->owner||e!=s->epoch){
        /* Drift first observed by any op on the armed generation: refuse the
         * operation, issue no further provider I/O, and lift any contact the
         * session still holds through the existing neutral release at the
         * last accepted monotonic time. The claim stays held around send() so
         * a stop inside it still defers; a failed lift remains pending for
         * elden_pen_owner_release and the op may surface the send error.
         * Reaching here means g==s->gen — a stale/foreign generation token
         * was already refused above without touching the session. */
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        int src=unwind(s);
        return src?src:(x?x:EldenPenOwnerStale);
    }
    return 0;
}
/* Refusal release on a live generation: a stale owner/epoch, foreign frame
 * owner or rolled-back worker timestamp/sequence refuses the packet, but any
 * contact the current session already delivered must not stay held down. The
 * lift reuses the existing neutral release at the last accepted monotonic
 * time; a failed emit leaves release_pending for elden_pen_owner_release to
 * retry — never a fabricated frame and never a false released state. The claim
 * is still held around send() so a stop landing inside it defers, exactly like
 * the decode/transport fault branches. */
static int refuse_release(EldenPenOwner *s){
    int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
    int src=unwind(s);return src?src:x;
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
    if(is_stopped(s))return EldenPenStopped;
    if(s->bound)return EldenPenOwnerBusy;
    /* The explicit touch frame-owner association is mandatory and nonzero. */
    if(!frame_owner)return EldenPenDeliveryArgument;
    /* The whole lifecycle — the name/owner/frame probes plus the publication —
     * runs under the claim so no operation can overlap a half-armed binding. */
    if(claim(s))return EldenPenOwnerBusy;
    if(is_stopped(s)){unwind(s);return EldenPenStopped;}
    if(s->bound){unwind(s);return EldenPenOwnerBusy;}
    /* seq+1 must never wrap: a reused generation could resurrect a fenced
     * identity. Refuse before any provider callback and leave everything
     * untouched. */
    if(s->seq==UINT64_MAX){unwind(s);return EldenPenDeliveryArgument;}
    const char *have=p->name(p->context);
    if(!have||!name_eq(have,named)){unwind(s);return EldenPenDeliveryArgument;}
    uint64_t o=0,e=0;
    if(p->owner_epoch(p->context,&o,&e)||!o){unwind(s);return EldenPenDeliveryArgument;}
    /* The provider's own frame-owner report must already equal the declared
     * association — a fabricated association is foreign, not bound. */
    uint64_t fo=0,fe=0;
    if(p->frame_owner(p->context,&fo,&fe)||fo!=frame_owner||fe!=frame_epoch){unwind(s);return EldenPenOwnerForeign;}
    s->provider=*p;s->owner=o;s->epoch=e;s->frame_owner=frame_owner;s->frame_epoch=frame_epoch;
    s->gen=s->seq+1;s->seq=s->gen;s->bound=1;s->have_seq=0;s->last_ts=0;s->last_seq=0;
    *generation=s->gen;s->busy=0;return 0;
}
int elden_pen_owner_packet(EldenPenOwner *s,uint64_t g){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    /* guard_enter already issued the drift release for owner/epoch loss on
     * this live generation — no second lift is attempted here. */
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    /* A paused or terminally stopped delivery must not consume a provider
     * packet at all — the sample is refused before any provider I/O so no
     * contact bytes are pulled and silently lost. */
    if(s->delivery->stopped){unwind(s);return EldenPenStopped;}
    if(s->delivery->paused){unwind(s);return EldenPenPaused;}
    uint8_t raw[EldenPenPacketBytes];uint64_t ts=0,seq=0,fo=0,fe=0;
    int prc=s->provider.packet(s->provider.context,raw,&ts,&seq);
    if(is_stopped(s)){s->dropped++;int x=unwind(s);return x?x:EldenPenStopped;}
    if(prc){
        /* Transport fault: no packet was sampled. Drive the existing fault
         * path at the facade's last known clock — never a synthetic lift. The
         * claim stays held so a stop inside send() defers instead of nesting
         * send. */
        s->faults++;
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        int src=unwind(s);return src?src:(x?x:prc);
    }
    int frc=s->provider.frame_owner(s->provider.context,&fo,&fe);
    if(is_stopped(s)){s->dropped++;int x=unwind(s);return x?x:EldenPenStopped;}
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
         * matching elden_pen_native_packet's decode-failure behavior. The
         * claim stays held across send() for the same deferred-stop reason. */
        s->faults++;
        int x=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
        int src=unwind(s);return src?src:(x?x:drc);
    }
    s->decoded++;if(f.kind==EldenPenIdentity||f.kind==EldenPenInfo)s->metadata++;
    mark_now(s,ts);
    rc=elden_pen_delivery_feed(s->delivery,&f,ts,s->send,s->send_context);
    int src=unwind(s);
    if(src)return src;
    if(is_stopped(s))return EldenPenStopped;  /* a stop landed inside send() */
    return rc;
}
int elden_pen_owner_tick(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_tick(s->delivery,now,s->send,s->send_context);
    int src=unwind(s);return src?src:rc;
}
int elden_pen_owner_fault(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_fault(s->delivery,now,s->send,s->send_context);
    s->faults++;
    int src=unwind(s);return src?src:rc;
}
int elden_pen_owner_pause(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_pause(s->delivery,now,s->send,s->send_context);
    if(!rc)s->pauses++;
    int src=unwind(s);return src?src:rc;
}
int elden_pen_owner_resume(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    int rc=guard_enter(s,g);if(rc){s->rejected++;return rc;}
    mark_now(s,now);
    rc=elden_pen_delivery_resume(s->delivery,now);
    if(!rc)s->resumes++;
    int src=unwind(s);return src?src:rc;
}
/* Facade-level stop: requires a live generation, gates every later operation
 * and provider I/O, and drives the existing delivery stop/release path. The
 * stopped GATE is armed synchronously by CAS on the armed generation — later
 * ops fence Stopped at once — while the release WORK never waits: a stop
 * issued from inside a provider/send callback only posts the deferred flag,
 * and the release runs when the in-flight operation unwinds, so send() is
 * never reentered from within a provider callback. A stop racing a retire is
 * either consumed by the retire's own unwind (its generation is still live
 * there) or becomes moot on the cleared facade — never a poisoned successor. */
int elden_pen_owner_stop(EldenPenOwner *s,uint64_t g,uint64_t now){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    if(!gen_live(s,g))return EldenPenOwnerStale;
    uint64_t was=s->stopped;
    if(was==g)return EldenPenStopped;
    if(!atomic_compare_exchange_strong(&s->stopped,&was,g))
        return was==g?EldenPenStopped:EldenPenOwnerStale;   /* lost the post race */
    s->stops++;s->stop_now=now;mark_now(s,now);
    if(claim(s)){
        /* An op/callback holds the claim — defer the release to its unwind.
         * If the holder unwound between the exchange and the arm, nobody will
         * consume the deferral, so retry the claim once and run it here. */
        s->stop_deferred=g;
        if(!claim(s)){
            int rc=0;
            if(atomic_exchange(&s->stop_deferred,0)==g)
                rc=elden_pen_delivery_stop(s->delivery,now,s->send,s->send_context);
            s->busy=0;return rc;
        }
        return 0;   /* still held — the holder's unwind runs it */
    }
    /* Under the claim: a retire may have cleared the generation between the
     * gate and the claim — the post is then moot (the armed tag is inert once
     * gen is gone). */
    if(!gen_live(s,g)){s->busy=0;return EldenPenOwnerStale;}
    int rc=elden_pen_delivery_stop(s->delivery,now,s->send,s->send_context);
    s->busy=0;return rc;
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
    if(claim(s))return EldenPenOwnerBusy;
    /* Re-fence under the claim — a retire may have cleared the generation
     * between the pre-check and the claim. */
    if(!gen_live(s,g)){unwind(s);return EldenPenOwnerStale;}
    int rc=elden_pen_delivery_fault(s->delivery,s->last_now,s->send,s->send_context);
    int src=unwind(s);return src?src:rc;
}
int elden_pen_owner_retire(EldenPenOwner *s,uint64_t g){
    if(!s||!s->delivery)return EldenPenDeliveryArgument;
    /* The claim serializes the whole retire — generation revalidation, the
     * deferred-stop consumption, the release-confirmation fence, the delivery
     * re-init and the clearing — against new operations. */
    if(claim(s))return EldenPenOwnerBusy;
    if(!gen_live(s,g)){unwind(s);return EldenPenOwnerStale;}
    if(!is_stopped(s)){unwind(s);return EldenPenOwnerDisabled;}   /* ordered fence: stop before retire */
    /* Consume a stop deferred for this generation before deciding — its
     * pending release is exactly what the confirmation below is waiting on. */
    {uint64_t dg=atomic_exchange(&s->stop_deferred,0);
     if(dg&&dg==s->gen)elden_pen_delivery_stop(s->delivery,s->stop_now,s->send,s->send_context);}
    /* Confirmed-release fence: a pending or undelivered lift must be finished
     * through elden_pen_owner_release first — retire never drops a contact the
     * peer still believes is down. */
    if(!elden_pen_delivery_released(s->delivery)){s->busy=0;return EldenPenOwnerDisabled;}
    /* Re-arm the caller-owned delivery to its init state (stale_us kept) so a
     * later bind starts clean without the caller reinitializing it. */
    int irc=elden_pen_delivery_init(s->delivery,s->delivery->stale_us);
    if(irc){s->busy=0;return irc;}
    s->provider=(EldenPenProvider){0};s->owner=0;s->epoch=0;
    s->frame_owner=0;s->frame_epoch=0;
    s->have_seq=0;s->last_ts=0;s->last_seq=0;s->last_now=0;
    s->bound=0;s->gen=0;s->stopped=0;s->stop_deferred=0;s->stop_now=0;
    s->busy=0;return 0;
}
int elden_pen_owner_released(EldenPenOwner *s){
    if(!s||!s->delivery)return 0;
    /* Never a false "released" during an in-flight mutation: report the
     * conservative 0 while the claim is contended. */
    if(claim(s))return 0;
    int r=elden_pen_delivery_released(s->delivery);
    s->busy=0;return r;
}
int elden_pen_owner_status(EldenPenOwner *s,EldenPenOwnerStatus *out){
    if(!s||!out||!s->delivery)return EldenPenDeliveryArgument;
    /* Under the claim the snapshot is coherent — the facade fields and the
     * delivery counters are only mutated while the claim is held. */
    if(claim(s))return EldenPenOwnerBusy;
    uint64_t g=s->gen;
    *out=(EldenPenOwnerStatus){s->bound,(uint32_t)(g&&g==s->stopped),0,
        g,s->owner,s->epoch,s->frame_owner,s->frame_epoch,s->last_ts,s->last_seq,
        s->packets,s->decoded,s->metadata,s->faults,s->pauses,s->resumes,s->stops,
        s->rejected,s->dropped,s->stale_ts,s->stale_seq,s->foreign_frames,
        s->delivery->sent,s->delivery->failures,
        s->delivery->paused,elden_pen_delivery_released(s->delivery),0};
    s->busy=0;return 0;
}
