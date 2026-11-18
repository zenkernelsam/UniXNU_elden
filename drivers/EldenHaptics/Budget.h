/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_SHARED_BUDGET_H
#define ELDEN_HAPTICS_SHARED_BUDGET_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenSharedBudgetSlots=64};
typedef struct {
    uint32_t window_us,max_on_us[2],max_dual_on_us;
    /* gain^2 * signed8-sample^2 summed at24kHz, per moving window.
     * Accounting proxy only; not a motor coil/temperature certificate. */
    uint64_t max_squares[2];
    uint8_t mask;
} EldenSharedBudgetLimits;
typedef struct {
    uint64_t owner,epoch,id,expires_us;uint64_t squares[2];
    uint32_t on_us[2],dual_us;uint8_t used,active,held;
} EldenSharedBudgetEntry;
typedef struct {
    EldenSharedBudgetLimits limits;EldenSharedBudgetEntry slots[EldenSharedBudgetSlots];
    uint64_t clock_us;uint8_t initialized,has_clock,clock_failed;
} EldenSharedBudget;
/* Native worker serializes all methods. The same object persists across RAM/
 * RTP requests and client disconnects; init refuses a previously initialized
 * object. Active/unconfirmed entries NEVER expire from elapsed time alone. */
int elden_shared_budget_init(EldenSharedBudget *,const EldenSharedBudgetLimits *);
int elden_shared_budget_reserve(EldenSharedBudget *,uint64_t owner,uint64_t epoch,uint64_t id,
    uint64_t now_us,uint32_t lease_us,const uint64_t peak_squares[2],const uint32_t peak_on_us[2],uint32_t peak_dual_us);
int elden_shared_budget_ram(EldenSharedBudget *,uint64_t owner,uint64_t epoch,uint64_t id,
    uint64_t now_us,uint32_t duration_us,const uint8_t gain[2]);
/* confirmed only after actual stop/standby. A failed stop permanently pins its
 * reservation until physical reset/new boot, with no release success stub. */
int elden_shared_budget_finish(EldenSharedBudget *,uint64_t owner,uint64_t epoch,uint64_t id,uint64_t now_us,int confirmed);
int elden_shared_budget_active(const EldenSharedBudget *);
#ifdef __cplusplus
}
#endif
#endif
