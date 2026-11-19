/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_DELIVERY_H
#define ELDEN_PEN_DELIVERY_H
#include "PenReport.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*EldenPenSend)(void *,const EldenPenFrame *);
enum { EldenPenClock=-20, EldenPenStopped=-21, EldenPenDeliveryArgument=-22,EldenPenPaused=-23 };
/* Serialized by the owning input worker. send() must return the actual HID
 * delivery result, must not reenter, and must not claim success without sending.
 * No accepted-state update on failure. Separate from finger history/locks. */
typedef struct {
    EldenPenFrame desired,accepted;
    uint64_t last_clock,last_coordinate,stale_us;
    uint32_t sent,failures;
    uint8_t has_clock,has_coordinate,has_accepted,release_pending,dirty,stopped,clock_fault,paused;
} EldenPenDelivery;
int elden_pen_delivery_init(EldenPenDelivery *,uint64_t stale_us);
int elden_pen_delivery_feed(EldenPenDelivery *,const EldenPenFrame *,uint64_t now_us,EldenPenSend,void *);
int elden_pen_delivery_tick(EldenPenDelivery *,uint64_t now_us,EldenPenSend,void *);
int elden_pen_delivery_fault(EldenPenDelivery *,uint64_t now_us,EldenPenSend,void *);
int elden_pen_delivery_stop(EldenPenDelivery *,uint64_t now_us,EldenPenSend,void *);
/* Reversible pause uses the same actual release acknowledgment as terminal
 * stop but retains the service/delivery clock. While paused no coordinate or
 * metadata can become a new contact. Resume refuses pending release/clock
 * fault/terminal stop, and never replays a saved pre-sleep desired contact. */
int elden_pen_delivery_pause(EldenPenDelivery *,uint64_t now_us,EldenPenSend,void *);
int elden_pen_delivery_resume(EldenPenDelivery *,uint64_t now_us);
/* True only once no delivered tip/range/button remains and no release is owed.
 * A failed release remains pending; the caller must keep the sender available. */
int elden_pen_delivery_released(const EldenPenDelivery *);
#ifdef __cplusplus
}
#endif
#endif
