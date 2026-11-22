/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativeBridge.h"
/* The bridge is a gate, not a second authority: data operations forward to
 * EldenPenOwner, which holds the atomic whole-operation claim across provider
 * probes, send callbacks and delivery mutation. `started` is a generation
 * tag — the gate admits only the generation it names — and `lc` is the
 * nonblocking lifecycle claim that serializes bind/start/stop/retire so no
 * transition can publish or clear the gate across a successor. */
static int lc_claim(EldenPenNativeBridge *b){return atomic_exchange(&b->lc,1);}
/* Release the lifecycle claim, first consuming a stop posted while it was
 * held: the tag is applied through the owner only if its generation is still
 * live (a retired tag is moot — the same drop the owner's unwind makes for a
 * dead deferral). The gate tag is cleared whenever the applied or already-dead
 * generation was the armed one, so no stale gate can survive the transition.
 * A real apply/send error surfaces to the claim holder like the owner's
 * unwind; a dead tag is consumed silently. Must run before `lc` drops so the
 * deferred stop linearizes inside the holder's transition window. */
static int lc_release(EldenPenNativeBridge *b){
    int x=0;
    uint64_t tag=atomic_exchange(&b->stop_pending,0);
    if(tag){
        int rc=elden_pen_owner_stop(&b->owner,tag,atomic_load(&b->stop_now));
        if(!rc||atomic_load(&b->started)==tag)atomic_store(&b->started,0);
        if(rc&&rc!=EldenPenOwnerStale&&rc!=EldenPenStopped)x=rc;
    }
    atomic_store(&b->lc,0);return x;
}
int elden_pen_bridge_init(EldenPenNativeBridge *b,const EldenPenLimits *limits,
                          EldenPenSend send,void *send_context){
    if(!b||!limits||!send)return EldenPenDeliveryArgument;
    atomic_store(&b->started,0);atomic_store(&b->lc,0);
    atomic_store(&b->stop_pending,0);atomic_store(&b->stop_now,0);
    int rc=elden_pen_delivery_init(&b->delivery,100000);
    if(rc)return rc;
    return elden_pen_owner_init(&b->owner,&b->delivery,limits,send,send_context);
}
int elden_pen_bridge_bind(EldenPenNativeBridge *b,const EldenPenProvider *p,
                          const char *named,uint64_t frame_owner,
                          uint64_t frame_epoch,uint64_t *generation){
    if(!b)return EldenPenDeliveryArgument;
    if(lc_claim(b))return EldenPenOwnerBusy;
    int rc;
    if(atomic_load(&b->started))rc=EldenPenOwnerBusy;   /* armed run refuses */
    else rc=elden_pen_owner_bind(&b->owner,p,named,frame_owner,frame_epoch,generation);
    int x=lc_release(b);return x?x:rc;
}
int elden_pen_bridge_start(EldenPenNativeBridge *b,uint64_t generation){
    if(!b)return EldenPenDeliveryArgument;
    if(lc_claim(b))return EldenPenOwnerBusy;
    EldenPenOwnerStatus st;
    int rc=elden_pen_owner_status(&b->owner,&st);   /* coherent or Busy */
    if(!rc&&!st.bound)rc=EldenPenNoProvider;         /* no real named provider */
    if(!rc&&st.gen!=generation)rc=EldenPenOwnerStale;
    if(!rc&&st.stopped)rc=EldenPenOwnerDisabled;
    if(!rc)atomic_store(&b->started,generation);     /* publish under the claim */
    int x=lc_release(b);
    return x?x:rc;
}
int elden_pen_bridge_packet(EldenPenNativeBridge *b,uint64_t generation){
    if(!b)return EldenPenDeliveryArgument;
    if(atomic_load(&b->started)!=generation)return EldenPenOwnerDisabled;
    return elden_pen_owner_packet(&b->owner,generation);
}
int elden_pen_bridge_tick(EldenPenNativeBridge *b,uint64_t generation,uint64_t now_us){
    if(!b)return EldenPenDeliveryArgument;
    if(atomic_load(&b->started)!=generation)return EldenPenOwnerDisabled;
    return elden_pen_owner_tick(&b->owner,generation,now_us);
}
int elden_pen_bridge_fault(EldenPenNativeBridge *b,uint64_t generation,uint64_t now_us){
    if(!b)return EldenPenDeliveryArgument;
    if(atomic_load(&b->started)!=generation)return EldenPenOwnerDisabled;
    return elden_pen_owner_fault(&b->owner,generation,now_us);
}
int elden_pen_bridge_pause(EldenPenNativeBridge *b,uint64_t generation,uint64_t now_us){
    if(!b)return EldenPenDeliveryArgument;
    if(atomic_load(&b->started)!=generation)return EldenPenOwnerDisabled;
    return elden_pen_owner_pause(&b->owner,generation,now_us);
}
int elden_pen_bridge_resume(EldenPenNativeBridge *b,uint64_t generation,uint64_t now_us){
    if(!b)return EldenPenDeliveryArgument;
    if(atomic_load(&b->started)!=generation)return EldenPenOwnerDisabled;
    return elden_pen_owner_resume(&b->owner,generation,now_us);
}
/* The gate disarm is synchronous and unconditional when the tag names this
 * generation — the same "arm before attempting" order the owner uses for
 * `stopped` — so a gate can never outlive a stop that targets it, even while
 * the apply itself defers behind a held lifecycle claim. */
int elden_pen_bridge_stop(EldenPenNativeBridge *b,uint64_t generation,uint64_t now_us){
    if(!b)return EldenPenDeliveryArgument;
    {uint64_t exp=generation;atomic_compare_exchange_strong(&b->started,&exp,0);}
    if(!lc_claim(b)){
        int rc=elden_pen_owner_stop(&b->owner,generation,now_us);
        if(!rc)atomic_store(&b->started,0);
        int x=lc_release(b);return x?x:rc;
    }
    /* A lifecycle transition holds the claim: post the deferred tag (callback
     * inside the holder's operation is exactly the owner's case) and retry the
     * claim once — if it freed meanwhile we drain our own post at release. */
    atomic_store(&b->stop_now,now_us);
    (void)atomic_exchange(&b->stop_pending,generation);
    if(!lc_claim(b)){
        int rc=elden_pen_owner_stop(&b->owner,generation,now_us);
        if(!rc)atomic_store(&b->started,0);
        int x=lc_release(b);return x?x:rc;
    }
    return 0;   /* posted — the claim holder applies it at release */
}
int elden_pen_bridge_release(EldenPenNativeBridge *b,uint64_t generation){
    if(!b)return EldenPenDeliveryArgument;
    return elden_pen_owner_release(&b->owner,generation);
}
int elden_pen_bridge_retire(EldenPenNativeBridge *b,uint64_t generation){
    if(!b)return EldenPenDeliveryArgument;
    if(lc_claim(b))return EldenPenOwnerBusy;
    int rc=elden_pen_owner_retire(&b->owner,generation);
    if(!rc)atomic_store(&b->started,0);   /* delivery re-armed for a fresh bind */
    int x=lc_release(b);return x?x:rc;
}
int elden_pen_bridge_status(EldenPenNativeBridge *b,EldenPenOwnerStatus *out){
    if(!b)return EldenPenDeliveryArgument;
    return elden_pen_owner_status(&b->owner,out);
}
uint32_t elden_pen_bridge_started(const EldenPenNativeBridge *b){return b&&atomic_load(&b->started)?1:0;}
uint32_t elden_pen_bridge_bound(const EldenPenNativeBridge *b){return b?b->owner.bound:0;}
