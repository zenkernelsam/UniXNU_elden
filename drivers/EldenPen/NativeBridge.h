/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_NATIVE_BRIDGE_H
#define ELDEN_PEN_NATIVE_BRIDGE_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "Owner.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Default-off source-level bridge between the NT36536/AP501U native entry
 * surface and EldenPenOwner. A future mainline sampling worker calls these
 * hooks instead of touching the delivery layer directly: every data-path call
 * routes through the owner facade and inherits its named-provider, owner/epoch,
 * frame-owner, monotonic timestamp/sequence, deferred-stop, release and
 * retire/rebind fencing unchanged.
 *
 * The bridge invents NOTHING: no packets, coordinates, pressure, tilt,
 * timestamps, frame owners, scan modes or Pencil identities are synthesized
 * here or anywhere downstream; every byte still comes from the bound
 * provider through elden_pen_decode/EldenPenDelivery and the caller's
 * EldenPenSend. It is strictly default-off — `started` stays 0 until an
 * explicitly named, qualified provider is bound AND start is called, and
 * start refuses outright when no real provider exists (`pencil_accepted`,
 * hardware and provider-bound status remain false). The existing
 * EldenPenHid publication path and the original touch worker are untouched;
 * no global initializer is added.
 *
 * Lifecycle linearization: `started` is a generation TAG — the data gate
 * admits only the generation it names. `lc` is a nonblocking claim held by
 * bind/start/stop/retire across the whole transition, so a start can never
 * publish the gate across a stop→retire→rebind: a late start either
 * linearizes before the rebind (stale generation refusal) or sees the new
 * generation and refuses. `stop` never waits on `lc`: it disarms its
 * generation's gate tag synchronously, then applies through the owner when
 * it wins `lc`, else posts a generation-tagged `stop_pending` the claim
 * holder consumes at release — preserving the owner's deferred-stop,
 * callback-safe semantics with no deadlock. */
typedef struct {
    EldenPenOwner owner;            /* the authoritative guarded facade */
    EldenPenDelivery delivery;      /* bridge-owned delivery state machine */
    _Atomic uint64_t started;       /* armed generation tag; 0 = gate off */
    _Atomic uint32_t lc;            /* lifecycle claim: bind/start/stop/retire */
    _Atomic uint64_t stop_pending;  /* deferred stop generation while lc held */
    _Atomic uint64_t stop_now;      /* its timestamp */
} EldenPenNativeBridge;
/* Initialize the bridge-owned delivery (same 100 ms stale window the existing
 * native service uses) and the owner facade over the caller's decode limits
 * and send transport. Does not bind anything — the bridge stays disabled. */
int elden_pen_bridge_init(EldenPenNativeBridge *,const EldenPenLimits *,
                          EldenPenSend,void *send_context);
/* Bind an explicitly named provider through elden_pen_owner_bind: every
 * callback populated, name equal to `named`, nonzero provider owner/epoch and
 * an already matching nonzero touch-frame owner. Takes the `lc` claim;
 * refuses EldenPenOwnerBusy on contention or while a start gate is armed.
 * Returns the armed generation on success. */
int elden_pen_bridge_bind(EldenPenNativeBridge *,const EldenPenProvider *,
                          const char *named,uint64_t frame_owner,
                          uint64_t frame_epoch,uint64_t *generation);
/* Native startup under the `lc` claim: arms the generation-tagged gate only
 * after the owner reports a coherent bound, unstopped, matching-generation
 * binding — EldenPenNoProvider when no real named provider exists,
 * EldenPenOwnerStale on a wrong generation, EldenPenOwnerDisabled on a
 * stopped one, Busy on a contended claim. Because validation and publication
 * are serialized by `lc`, a delayed old-generation start can never open a
 * successor's gate. A successful start leaves provider-bound status true
 * only because a real provider was bound first; it is never set here. */
int elden_pen_bridge_start(EldenPenNativeBridge *,uint64_t generation);
/* Data path — each requires the gate tag to name the caller's generation
 * (generation-gated admission: a tag armed for a successor refuses all older
 * tokens) and forwards to the matching owner operation with full fencing. */
int elden_pen_bridge_packet(EldenPenNativeBridge *,uint64_t generation);
int elden_pen_bridge_tick(EldenPenNativeBridge *,uint64_t generation,uint64_t now_us);
int elden_pen_bridge_fault(EldenPenNativeBridge *,uint64_t generation,uint64_t now_us);
int elden_pen_bridge_pause(EldenPenNativeBridge *,uint64_t generation,uint64_t now_us);
int elden_pen_bridge_resume(EldenPenNativeBridge *,uint64_t generation,uint64_t now_us);
/* Policy path — disarms the gate tag synchronously when it names this
 * generation (callback-safe, never waits), then applies through
 * elden_pen_owner_stop under `lc`; when `lc` is contended it posts a
 * generation-tagged deferred stop consumed at the holder's release — same
 * nonblocking semantics as the owner's own deferred path, no deadlock.
 * Returns 0 once the stop is posted or applied. */
int elden_pen_bridge_stop(EldenPenNativeBridge *,uint64_t generation,uint64_t now_us);
/* Provider-free release retry through the bound send — unchanged owner
 * semantics (works after stop; keeps release_pending on send failure). */
int elden_pen_bridge_release(EldenPenNativeBridge *,uint64_t generation);
/* Ordered stop -> retire through the owner under the `lc` claim: requires
 * prior stop and a confirmed release, then re-arms the bridge-owned delivery,
 * clears the binding and drops the gate tag for a fresh qualified rebind.
 * Serialized so no concurrent start can re-publish across the clear. */
int elden_pen_bridge_retire(EldenPenNativeBridge *,uint64_t generation);
/* Coherent owner status under the claim, or EldenPenOwnerBusy. */
int elden_pen_bridge_status(EldenPenNativeBridge *,EldenPenOwnerStatus *);
/* Coarse atomic surface flags — the native entry gate and the owner binding.
 * Neither is claimed by reading; a false bound stays false until a real named
 * provider is bound. */
uint32_t elden_pen_bridge_started(const EldenPenNativeBridge *);
uint32_t elden_pen_bridge_bound(const EldenPenNativeBridge *);
#ifdef __cplusplus
}
#endif
#endif
