/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "ChargePoll.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Profile 1 is the stock Lenovo scalar interface: capacity in 0.01%,
 * aggregate voltage/current/charge in uV/uA/uAh, temperature in 0.01 C.
 * It must be qualified against OEM Android before publishing to powerd.
 * The vendor FG1/FG2 scalars are deliberately not converted by this profile.
 */
typedef struct {
    uint32_t profile;
    int current_multiplier; /* +1 raw charging-positive; -1 raw discharge-positive */
    uint32_t max_age_ms;
} EldenBatteryContract;

enum EldenBatteryOptional {
    EldenBatteryCounterKnown=1u<<0, EldenBatteryFullKnown=1u<<1,
    EldenBatteryDesignKnown=1u<<2, EldenBatteryCyclesKnown=1u<<3,
    EldenBatteryTimeKnown=1u<<4, EldenBatteryHealthKnown=1u<<5
};
typedef struct {
    uint32_t epoch, cycle, optional;
    int percent, voltage_mv, current_ma, temperature_centi_c;
    int remaining_mah, full_mah, design_mah, cycles, time_remaining_min, health;
    bool present, external, charging_enabled, input_suspended;
    /* Input remains usable while the battery is held at a protection limit
     * or bypass is selected. Battery permission is a separate decision. */
    bool external_charge_capable;
    bool charging, fully_charged, warn, critical;
} EldenBatterySnapshot;

enum {
    EldenBatteryOk=0, EldenBatteryArgument=-1, EldenBatteryUnqualified=-2,
    EldenBatteryStale=-3, EldenBatteryMissing=-4, EldenBatteryInvalid=-5,
    EldenBatterySettling=-6
};
/* Single complete poll cycle, no averaging of the two differently sized cells.
 * Failure leaves out unchanged, so callers cannot publish an invented zero SOC.
 */
int elden_battery_snapshot(const EldenChargeRawSnapshot *raw,
    const EldenBatteryContract *contract, uint64_t age_ms, EldenBatterySnapshot *out);
typedef struct {
    uint32_t epoch,last_cycle;
    unsigned confirmations;
    EldenBatterySnapshot previous;
} EldenBatteryGate;
void elden_battery_gate_reset(EldenBatteryGate *gate);
/* TB322FC's first complete firmware replies can still contain the aggregate
 * defaults 50%/4V/4A/25C. Require independent gauge agreement and two fresh
 * coherent cycles before publishing. Gauges are never averaged/substituted. */
int elden_battery_gate_update(EldenBatteryGate *gate,const EldenChargeRawSnapshot *raw,
    const EldenBatteryContract *contract,uint64_t age_ms,EldenBatterySnapshot *out);
#ifdef __cplusplus
}
#endif
