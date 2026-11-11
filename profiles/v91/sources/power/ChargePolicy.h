/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenChargeMode {EldenChargeObserve=0,EldenChargeStock=1,EldenChargeBypass=2,
    EldenChargeLimit80=3,EldenChargeWindow4060=4};
enum EldenChargePolicyPhase {EldenPolicyOff,EldenPolicyReadWindow,EldenPolicyReadEu,
    EldenPolicyReady,EldenPolicySetWindow,EldenPolicyVerifyWindow,EldenPolicySetEnable,
    EldenPolicyWaitWindow,EldenPolicyWaitEnable,EldenPolicyFailed};
typedef struct {
    bool up,pending,original_saved,firmware_held;
    enum EldenChargePolicyPhase phase;
    unsigned requested_mode,active_mode,applied_mode;
    uint32_t epoch,request_sequence,active_sequence,applied_sequence;
    uint32_t original_window,window,eu_window,target_window,target_enable;
    uint32_t observed_cycle,observed_soc,observed_enable,verify_after;
    uint32_t last_opcode,last_property,last_value,errors,timeouts,firmware_error;
} EldenChargePolicy;
void elden_charge_policy_init(EldenChargePolicy *s);
void elden_charge_policy_link(EldenChargePolicy *s,bool up);
int elden_charge_policy_select(EldenChargePolicy *s,unsigned mode);
/* Only the existing BMS poll reads property28, avoiding two indistinguishable
 * GET replies on one endpoint. Verification waits for a full later cycle. */
void elden_charge_policy_observe(EldenChargePolicy *s,uint32_t cycle,uint32_t soc_centi,uint32_t enabled);
int elden_charge_policy_next(EldenChargePolicy *s,bool allow_change,uint8_t out[24]);
void elden_charge_policy_unsent(EldenChargePolicy *s);
void elden_charge_policy_timeout(EldenChargePolicy *s);
int elden_charge_policy_receive(EldenChargePolicy *s,const uint8_t *data,size_t bytes);
bool elden_charge_window_valid(uint32_t value);
#ifdef __cplusplus
}
#endif
