/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_WAKE_TOUCH_POWER_H
#define ELDEN_WAKE_TOUCH_POWER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Actual board display/system power owner; same phase enum as Flow.h.
     * Paused means touch supply/transport remains physically usable. Resume
     * only after real panel/rail initialization, never GOP logical PM alone. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned phase);
    /* Queue the actual power request asynchronously. Never invoke the
     * synchronous touch-request API from this original worker callback. */
    int (*request_wake)(void *,uint64_t owner,uint64_t epoch,unsigned gesture);
} EldenTouchWakePower;
typedef struct {uint32_t configured,ready,flow,paused,requested,acknowledged,closed,packets,gestures;int error;} EldenTouchWakeStatus;
/* Kernel power provider installs once, retained to reset. No service/startup
 * or successful placeholder provider. Worker feature remains default-off and
 * requires elden-touch-wake=1 plus real installed power callbacks. */
int elden_touch_wake_install_power(const EldenTouchWakePower *);
/* Original worker executes actual pause/release/gesture/deep/restore and
 * acknowledges. asleep=0 requests actual power-ready resume;asleep=1 pauses.
 * request timeout withdraws command and closes the coordinator; no false ack. */
int elden_touch_wake_request(uint64_t owner,uint64_t epoch,int asleep,int double_tap);
int elden_touch_wake_status(uint64_t owner,uint64_t epoch,EldenTouchWakeStatus *);
#ifdef __cplusplus
}
#endif
#endif
