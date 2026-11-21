/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_WAKE_POWER_OWNER_H
#define ELDEN_WAKE_POWER_OWNER_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "Flow.h"
#include "TouchPower.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenWakePowerInvalid=-22,EldenWakePowerStale=-116,EldenWakePowerDisabled=-125,
      EldenWakePowerBusy=-16};
/* The guarded display/system-PM owner facade for the existing touch wake flow.
 * This is a source-side contract mainline can later bind to the real panel/
 * rail/system power owner. It chooses nothing: no panel register, rail value,
 * epoch or readiness comes from here — every provider must declare a name,
 * expose nonzero owner/epoch, queue its wake work off the original touch
 * worker and submit an explicit readiness result before the existing
 * elden_touch_wake_request(owner,epoch,0,..) resume path is invoked.
 * Default-off: a missing named provider or missing callbacks refuse.
 *
 * Thread model: admit/request_wake run on the original touch worker through
 * the installed bridges; elden_wake_power_ready runs on the provider's own
 * thread; stop/retire/status may be posted from policy threads. All shared
 * lifecycle/callback fields are atomic. Serialization is a non-blocking busy
 * claim — any facade call finding a callback or lifecycle op in flight is
 * refused (reentrant synchronous self-ack and cross-thread concurrency get the
 * same -16), never queued on a lock, so no wait/deadlock is introduced. A stop
 * posted while a callback holds the claim is deferred and applied when that
 * callback unwinds. */
typedef struct {
    void *context;
    /* Named-provider identity; must equal the name the caller binds against,
     * non-empty. */
    const char *(*name)(void *context);
    /* Serialized provider owner/epoch probe; re-checked under the claim before
     * every provider callback result is trusted, so a stale or foreign owner
     * is fenced, not trusted. */
    int (*owner_epoch)(void *context,uint64_t *owner,uint64_t *epoch);
    /* Same phase enum as Flow.h; Resume requires actual panel/rail
     * initialization, never GOP logical scanout power management alone. */
    int (*admit)(void *context,uint64_t owner,uint64_t epoch,unsigned phase);
    /* Queue the actual display/system wake work asynchronously. This runs on
     * the original touch worker through the installed EldenTouchWakePower, so
     * it must never invoke elden_wake_power_ready from inside the callback —
     * synchronous self-ack is refused here and the coordinator request API
     * also refuses its own worker thread. Returning 0 means queued, not that
     * panel/rails are ready. At most one request is outstanding: a call while
     * a queued request awaits readiness is refused before reaching the
     * provider, matching the coordinator's single serialized request contract,
     * so no provider-queued work and no legitimate readiness result is lost. */
    int (*request_wake)(void *context,uint64_t owner,uint64_t epoch,unsigned gesture);
} EldenWakePowerProvider;
typedef struct {
    /* Immutable EldenTouchWakePower the caller installs through the existing
     * elden_touch_wake_install_power once bound; zeroed while unbound. */
    EldenTouchWakePower installed;
    EldenWakePowerProvider provider;
    /* The existing touch wake resume path — on the kernel side this wraps
     * elden_touch_wake_request(owner,epoch,0,0) with its original 2.5-second
     * bounded acknowledgment. Injected so this TU stays host-testable; the
     * facade never fabricates the call or its result. */
    int (*resume)(void *context,uint64_t owner,uint64_t epoch);
    void *resume_context;
    _Atomic uint64_t seq,generation,owner,epoch;
    _Atomic uint32_t bound,stopped,busy,stopping,pending;
    _Atomic uint32_t admits,requests,readies,resumes,not_ready,rejected,stops;
} EldenWakePowerOwner;
typedef struct {
    uint32_t bound,stopped,pending;
    uint64_t generation,owner,epoch;
    uint32_t admits,requests,readies,resumes,not_ready,rejected,stops;
} EldenWakePowerStatus;
/* Bind the named display/system-PM provider. Requires every provider
 * callback, an explicit non-empty name equal to `named`, a live nonzero
 * owner/epoch probe and the injected resume path; only then is `installed`
 * armed for the existing install API. The name callback and comparison run
 * serialized under the claim like every other provider callback, so a
 * concurrent bind cannot invoke provider I/O while a callback is active.
 * Returns a generation via *generation; generations are claimed under the
 * busy fence, never reused, and bind refuses once seq would wrap — nothing
 * is armed and generation is not modified at exhaustion. */
int elden_wake_power_bind(EldenWakePowerOwner *,const EldenWakePowerProvider *,
    const char *named,int (*resume)(void *,uint64_t,uint64_t),void *resume_context,
    uint64_t *generation);
/* Provider-side readiness submission, called from the provider's own thread
 * after it computed an explicit panel/rail readiness result: ready=1 calls the
 * bound resume path (its actual return code is propagated); ready=0 consumes
 * the pending request honestly so the coordinator's existing timeout closes it
 * — no fabricated resume and no fabricated error. Requires a live generation,
 * a pending request and a live owner/epoch; duplicate or out-of-order
 * readiness, stale identity, reentry or any concurrent callback (-16), and
 * calls after stop (-125) are refused. */
int elden_wake_power_ready(EldenWakePowerOwner *,uint64_t generation,int ready);
/* Facade-level stop: fences all later provider callbacks and readiness while
 * the bound registration is retained. A stop posted while a callback holds the
 * claim is deferred and applied when that callback unwinds; if the holder has
 * already unwound the stop applies it directly — a deferred stop is never
 * lost and no caller spins. */
int elden_wake_power_stop(EldenWakePowerOwner *,uint64_t generation);
/* Explicit stop -> retire fence: requires a prior stop and refuses while a
 * callback is active. A successful retire releases the retained provider
 * registration (the installed contract is cleared), leaving the facade
 * reusable for a new qualified bind; prior generations stay stale. */
int elden_wake_power_retire(EldenWakePowerOwner *,uint64_t generation);
/* Read-only atomic status snapshot for tests/status surfaces; no side effects
 * and safe to call concurrently with any facade operation. */
int elden_wake_power_status(const EldenWakePowerOwner *,EldenWakePowerStatus *);
#ifdef __cplusplus
}
#endif
#endif
