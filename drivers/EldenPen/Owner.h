/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_OWNER_H
#define ELDEN_PEN_OWNER_H
#include <stddef.h>
#include <stdint.h>
#include "PenReport.h"
#include "Delivery.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { EldenPenNoProvider=-110, EldenPenOwnerStale=-116, EldenPenOwnerForeign=-117,
       EldenPenOwnerDisabled=-125, EldenPenOwnerBusy=-16 };
/* The guarded AP501U sampling owner facade. This is a source-side contract the
 * real NT36536 sampling worker can bind when mainline is ready: it admits only
 * an explicitly named provider with a nonzero owner and every callback
 * populated, and it stays default-off until such a provider is bound. It
 * chooses nothing — no scan byte, pressure, tilt, battery or coordinate is
 * invented; every packet still goes through the existing elden_pen_decode and
 * EldenPenDelivery state machine, and the only output path is the caller-bound
 * EldenPenSend. */
typedef struct {
    void *context;
    /* Named provider identity; must equal `named` at bind, non-empty. */
    const char *(*name)(void *context);
    /* Serialized provider owner/epoch; rechecked before every facade operation
     * so a stale or foreign sampling worker is fenced, never trusted. */
    int (*owner_epoch)(void *context,uint64_t *owner,uint64_t *epoch);
    /* The finger/touch frame owner the packet was sampled under; rechecked for
     * every packet so foreign/stale frames are refused. This is the explicit
     * association with the existing touch frame owner: the same serialization
     * that protects finger frames also serializes simultaneous finger+pen
     * reports, and pen-only reports cannot be lost to a foreign stream. */
    int (*frame_owner)(void *context,uint64_t *owner,uint64_t *epoch);
    /* Serialized read of one raw EldenPenPacketBytes NT36536/AP501U packet
     * (packet[0] is the register byte, as in elden_pen_decode) plus the
     * worker's monotonic sampling timestamp (us) and strictly increasing
     * sequence. A nonzero return is a transport fault, not a packet. */
    int (*packet)(void *context,uint8_t out[EldenPenPacketBytes],
                  uint64_t *timestamp_us,uint64_t *sequence);
} EldenPenProvider;
typedef struct {
    EldenPenDelivery *delivery;      /* caller-owned, initialized; never owned */
    EldenPenSend send;void *send_context;
    EldenPenLimits limits;
    EldenPenProvider provider;
    uint64_t seq,gen,owner,epoch,frame_owner,frame_epoch;
    uint64_t last_ts,last_seq,last_now,stop_now;
    uint32_t bound,stopped,busy,stop_deferred,have_seq;
    uint32_t packets,decoded,metadata,faults,pauses,resumes,stops;
    uint32_t rejected,dropped,stale_ts,stale_seq,foreign_frames;
} EldenPenOwner;
/* Read-only status. transport_delivered counts frames the bound send() accepted;
 * pencil_accepted stays 0 forever at this layer — actual IOHIDEventDriver/
 * backboardd/UIKit Pencil acceptance is a separate downstream claim this facade
 * does not and cannot make. */
typedef struct {
    uint32_t bound,stopped,busy;
    uint64_t gen,owner,epoch,frame_owner,frame_epoch,last_ts,last_seq;
    uint32_t packets,decoded,metadata,faults,pauses,resumes,stops;
    uint32_t rejected,dropped,stale_ts,stale_seq,foreign_frames;
    uint32_t transport_delivered,transport_failures;
    int delivery_paused,delivery_released;
    uint32_t pencil_accepted;
} EldenPenOwnerStatus;
/* Bind the facade to the caller's initialized delivery state machine, decode
 * limits and send transport. The delivery's own lifecycle (stop/release ticks,
 * re-init for reuse) stays with the caller. */
int elden_pen_owner_init(EldenPenOwner *,EldenPenDelivery *,const EldenPenLimits *,
                         EldenPenSend,void *send_context);
/* Bind a named provider plus the explicit finger/touch frame-owner identity
 * (nonzero frame_owner) every packet must report. Requires every callback, a
 * non-empty name equal to `named`, nonzero provider owner, and the provider's
 * own frame_owner report already matching the declared identity. */
int elden_pen_owner_bind(EldenPenOwner *,const EldenPenProvider *,const char *named,
                         uint64_t frame_owner,uint64_t frame_epoch,uint64_t *generation);
/* One guarded sampling step: fences -> owner/epoch -> provider packet ->
 * frame-owner recheck -> monotonic timestamp/sequence -> decode -> delivery.
 * A refusal on the live generation (provider owner/epoch drift, foreign frame
 * owner, timestamp/sequence rollback) also drives the existing neutral
 * release so an already-delivered contact is lifted, never silently held;
 * a stale/foreign generation token mutates nothing. */
int elden_pen_owner_packet(EldenPenOwner *,uint64_t generation);
int elden_pen_owner_tick(EldenPenOwner *,uint64_t generation,uint64_t now_us);
/* Provider transport fault: drives the existing delivery fault/release path. */
int elden_pen_owner_fault(EldenPenOwner *,uint64_t generation,uint64_t now_us);
/* Pause/sleep flushes a real release through the existing delivery pause path;
 * resume refuses until the lift is acknowledged (delivery_released) and never
 * replays a pre-sleep contact. Service and clock are preserved by the delivery
 * layer itself. */
int elden_pen_owner_pause(EldenPenOwner *,uint64_t generation,uint64_t now_us);
int elden_pen_owner_resume(EldenPenOwner *,uint64_t generation,uint64_t now_us);
/* Facade-level stop: prevents all later provider I/O and drives the existing
 * delivery stop/release path. Callable from inside a provider callback — the
 * release then runs when the in-flight operation unwinds, so a stop during
 * delivery still releases the contact rather than reentering the provider. */
int elden_pen_owner_stop(EldenPenOwner *,uint64_t generation,uint64_t now_us);
/* Cleanup retry needing no provider callback or new sample: re-runs the
 * existing neutral release at the last accepted monotonic time through the
 * bound send(). Stop is terminal for new packets, not this release — a lift
 * pending after stop is completed here. Fences stale/foreign generations
 * before any mutation; a failed send keeps release_pending (never a false
 * released state). */
int elden_pen_owner_release(EldenPenOwner *,uint64_t generation);
/* Ordered stop -> retire fence: requires a prior stop and refuses while a
 * provider callback is active. Also refuses while the delivery still owes or
 * holds a contact — finish the lift through elden_pen_owner_release first.
 * On a confirmed release it re-arms the caller-owned delivery to its init
 * state (stale_us preserved), releases the binding, clears stopped and leaves
 * the facade reusable for a new qualified bind (fresh generation; prior
 * generations stay stale). */
int elden_pen_owner_retire(EldenPenOwner *,uint64_t generation);
/* True once the delivery has no delivered tip/range/button and owes no lift. */
int elden_pen_owner_released(const EldenPenOwner *);
/* Read-only status/acceptance surface; no side effects. */
int elden_pen_owner_status(const EldenPenOwner *,EldenPenOwnerStatus *);
#ifdef __cplusplus
}
#endif
#endif
