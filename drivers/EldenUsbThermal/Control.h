/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Policy.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t sequence;
    bool connector_valid[2],battery_valid;
    int connector_mc[2],battery_centi_c;
} EldenThermalInput;
typedef struct {int level[2];bool armed,fault_latched;} EldenThermalAction;
typedef struct {
    uint32_t previous_sequence;
    unsigned readiness,cool[2];
    bool armed,fault_latched[2];
} EldenThermalControl;
/* After two coherent valid cycles, follow OEM hysteresis. Once armed,
 * missing/invalid sensor data closes both inputs. Reopening after that fault
 * requires two fresh cool cycles. No inferred permission from elapsed time. */
void elden_thermal_control_step(EldenThermalControl *,const EldenThermalInput *,EldenThermalAction *);
#ifdef __cplusplus
}
#endif
