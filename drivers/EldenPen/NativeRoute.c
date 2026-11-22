/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativeRoute.h"
/* The route is the named provider EldenPenOwner binds: the worker pushes each
 * packet it already holds into `staged`, then the facade's pull model reads
 * exactly that packet back inside the held owner claim. Every report the
 * facade makes — owner/epoch, frame owner, timestamp, sequence — is a real
 * fact of this object and the actual caller, never a synthesized identity. */
static const char *rt_name(void *ctx){return ((EldenPenNativeRoute *)ctx)->name;}
static int rt_owner(void *ctx,uint64_t *owner,uint64_t *epoch){
    EldenPenNativeRoute *r=ctx;
    /* The provider's identity is the single route object itself; its epoch is
     * the real begin count, so a rebound session can never masquerade as the
     * previous binding's still-armed generation. */
    *owner=(uint64_t)(uintptr_t)r;*epoch=r->epoch;return 0;
}
static int rt_frame(void *ctx,uint64_t *owner,uint64_t *epoch){
    EldenPenNativeRoute *r=ctx;
    /* Before any packet the declared begin identity answers the bind probe;
     * once a packet is staged, the identity of the caller that actually
     * pushed it is reported — a packet sampled under a foreign owner is
     * refused by the facade's per-packet recheck. */
    if(r->staged.seq){*owner=r->staged.owner;*epoch=r->staged.epoch;}
    else{*owner=r->frame_owner;*epoch=r->frame_epoch;}
    return 0;
}
static int rt_packet(void *ctx,uint8_t out[EldenPenPacketBytes],uint64_t *ts,uint64_t *seq){
    EldenPenNativeRoute *r=ctx;
    /* A facade pull with nothing pushed is a transport violation, not a
     * packet — a nonzero return drives the existing fault path. */
    if(!r->staged.live)return -1;
    for(unsigned i=0;i<EldenPenPacketBytes;i++)out[i]=r->staged.bytes[i];
    *ts=r->staged.ts;*seq=r->staged.seq;r->staged.live=0;return 0;
}
int elden_pen_route_begin(EldenPenNativeRoute *r,const EldenPenLimits *limits,
                          EldenPenSend send,void *send_context,const char *named,
                          uint64_t frame_owner,uint64_t frame_epoch){
    if(!r||!limits||!send||!named||!*named||!frame_owner)return EldenPenDeliveryArgument;
    if(atomic_load(&r->armed))return EldenPenOwnerBusy;
    if(!r->init_done){
        int rc=elden_pen_bridge_init(&r->bridge,limits,send,send_context);
        if(rc)return rc;
        r->init_done=1;
    }
    r->name=named;r->frame_owner=frame_owner;r->frame_epoch=frame_epoch;
    r->staged=(EldenPenStaged){0};r->seq=0;++r->epoch;
    EldenPenProvider p={r,rt_name,rt_owner,rt_frame,rt_packet};
    uint64_t g=0;
    int rc=elden_pen_bridge_bind(&r->bridge,&p,named,frame_owner,frame_epoch,&g);
    if(rc)return rc;
    rc=elden_pen_bridge_start(&r->bridge,g);
    if(rc){
        /* Never leave a bound-but-unstarted owner behind: stop and retire the
         * generation so the facade is reusable and no stale binding lives. */
        elden_pen_bridge_stop(&r->bridge,g,0);
        elden_pen_bridge_retire(&r->bridge,g);
        return rc;
    }
    atomic_store(&r->gen,g);atomic_store(&r->armed,1);return 0;
}
int elden_pen_route_packet(EldenPenNativeRoute *r,const uint8_t *packet,size_t bytes,
                           uint64_t frame_owner,uint64_t timestamp_us){
    if(!r||!packet||!frame_owner)return EldenPenDeliveryArgument;
    /* No armed session: the push is refused before staging — a packet with no
     * live binding must never linger as a stale staged sample. */
    if(!atomic_load(&r->armed))return EldenPenNoProvider;
    if(bytes!=EldenPenPacketBytes){
        int fr=elden_pen_bridge_fault(&r->bridge,atomic_load(&r->gen),timestamp_us);
        return fr?fr:EldenPenDeliveryArgument;
    }
    for(unsigned i=0;i<EldenPenPacketBytes;i++)r->staged.bytes[i]=packet[i];
    r->staged.ts=timestamp_us;r->staged.seq=++r->seq;
    /* The pushed owner is the caller's own live identity; the epoch is the
     * one this session declared for it at begin — the facade's per-packet
     * frame-owner probe compares both against the declared association. */
    r->staged.owner=frame_owner;r->staged.epoch=r->frame_epoch;r->staged.live=1;
    return elden_pen_bridge_packet(&r->bridge,atomic_load(&r->gen));
}
int elden_pen_route_tick(EldenPenNativeRoute *r,uint64_t now_us){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return EldenPenNoProvider;
    return elden_pen_bridge_tick(&r->bridge,atomic_load(&r->gen),now_us);
}
int elden_pen_route_fault(EldenPenNativeRoute *r,uint64_t now_us){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return EldenPenNoProvider;
    return elden_pen_bridge_fault(&r->bridge,atomic_load(&r->gen),now_us);
}
int elden_pen_route_pause(EldenPenNativeRoute *r,uint64_t now_us){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return EldenPenNoProvider;
    return elden_pen_bridge_pause(&r->bridge,atomic_load(&r->gen),now_us);
}
int elden_pen_route_resume(EldenPenNativeRoute *r,uint64_t now_us){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return EldenPenNoProvider;
    return elden_pen_bridge_resume(&r->bridge,atomic_load(&r->gen),now_us);
}
int elden_pen_route_stop(EldenPenNativeRoute *r,uint64_t now_us){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return 0;
    return elden_pen_bridge_stop(&r->bridge,atomic_load(&r->gen),now_us);
}
int elden_pen_route_release(EldenPenNativeRoute *r){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return 0;
    return elden_pen_bridge_release(&r->bridge,atomic_load(&r->gen));
}
int elden_pen_route_retire(EldenPenNativeRoute *r){
    if(!r)return EldenPenDeliveryArgument;
    if(!atomic_load(&r->armed))return 0;
    int rc=elden_pen_bridge_retire(&r->bridge,atomic_load(&r->gen));
    if(!rc){atomic_store(&r->armed,0);atomic_store(&r->gen,0);}
    return rc;
}
int elden_pen_route_status(EldenPenNativeRoute *r,EldenPenOwnerStatus *out){
    if(!r||!out)return EldenPenDeliveryArgument;
    return elden_pen_bridge_status(&r->bridge,out);
}
