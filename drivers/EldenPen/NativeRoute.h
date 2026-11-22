/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_NATIVE_ROUTE_H
#define ELDEN_PEN_NATIVE_ROUTE_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "NativeBridge.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The real push-model edge between the serialized NT36536 sampling worker
 * and the guarded AP501U owner path. The worker already holds one complete
 * 109-byte packet from its single SPI read; this route stages exactly those
 * bytes with the caller's own sampling-owner identity, the worker's sampling
 * timestamp and a real increasing sequence, then lets the bound owner pull
 * them through the existing provider contract. It is the named provider the
 * owner binds — there is exactly one route object, owned by the native
 * entry, so no second SPI read and no invented packet, coordinate, pressure,
 * tilt, timestamp, sequence or frame owner exists anywhere.
 *
 * Caller serialization is the existing elden_pen_native_* contract: packet,
 * stop and release run on the original touch sampling worker only; pause and
 * resume run on the inherited sleep coordinator under the same generation.
 * The staged packet is written only by the worker push — the owner facade's
 * claim then serializes every pull, probe and send callback, and the bridge
 * lifecycle claim serializes begin/stop/retire transitions exactly as before. */
typedef struct {
    uint8_t bytes[EldenPenPacketBytes];
    uint64_t ts,seq,owner,epoch,live;
} EldenPenStaged;
typedef struct {
    EldenPenNativeBridge bridge;
    const char *name;                  /* the native entry's own provider name */
    _Atomic uint64_t gen;              /* armed generation; 0 = no live binding */
    _Atomic uint32_t armed;            /* a begin currently owns a generation */
    uint64_t frame_owner,frame_epoch;  /* declared worker identity at begin */
    uint64_t epoch;                    /* provider epoch; bumps every begin */
    uint64_t seq;                      /* pushed-packet sequence (worker only) */
    EldenPenStaged staged;             /* the pushed packet (worker only) */
    uint32_t init_done;
} EldenPenNativeRoute;
/* Arm one session: initializes the bridge once, then binds the route as the
 * named provider and starts its generation under the existing lifecycle
 * claim. frame_owner/frame_epoch must be the caller's real serialized
 * sampling-owner identity (nonzero); every later packet carries its own
 * caller identity and is rechecked against this declaration per packet.
 * Refuses while a session is already armed; a failed start unbinds rather
 * than leaving an owner attached to nothing. */
int elden_pen_route_begin(EldenPenNativeRoute *,const EldenPenLimits *,
                          EldenPenSend,void *send_context,const char *named,
                          uint64_t frame_owner,uint64_t frame_epoch);
/* One pushed worker packet: bytes must be exactly EldenPenPacketBytes (a
 * wrong size drives the existing fault path and refuses, mirroring the old
 * decode-failure behavior). The same bytes the worker's single SPI read
 * produced are staged with its caller identity, the worker sampling
 * timestamp and the next real sequence, then admitted through the armed
 * generation gate into owner packet handling. Refusals — unbound, disabled
 * gate, foreign frame owner, rolled-back time — propagate unchanged. */
int elden_pen_route_packet(EldenPenNativeRoute *,const uint8_t *,size_t,
                           uint64_t frame_owner,uint64_t timestamp_us);
/* Forwarded owner operations under the armed generation; gate/stale/foreign
 * refusals propagate unchanged. */
int elden_pen_route_tick(EldenPenNativeRoute *,uint64_t now_us);
int elden_pen_route_fault(EldenPenNativeRoute *,uint64_t now_us);
int elden_pen_route_pause(EldenPenNativeRoute *,uint64_t now_us);
int elden_pen_route_resume(EldenPenNativeRoute *,uint64_t now_us);
/* Stop the armed generation (nonblocking; defers behind a held claim),
 * provider-free release retry for the drain, then ordered retire which
 * re-arms the facade for a later session's rebind. */
int elden_pen_route_stop(EldenPenNativeRoute *,uint64_t now_us);
int elden_pen_route_release(EldenPenNativeRoute *);
int elden_pen_route_retire(EldenPenNativeRoute *);
/* Coherent owner status under the claim, or EldenPenOwnerBusy. */
int elden_pen_route_status(EldenPenNativeRoute *,EldenPenOwnerStatus *);
#ifdef __cplusplus
}
#endif
#endif
