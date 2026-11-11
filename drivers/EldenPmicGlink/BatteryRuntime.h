/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "BatterySnapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Called only by the existing PMIC worker: no second GLINK channel/owner. */
void elden_battery_runtime_configure(void);
void elden_battery_runtime_update(const EldenChargeRawSnapshot *snapshot,uint64_t age_ms);
void elden_battery_runtime_unavailable(uint32_t epoch,int reason);
/* Qualified telemetry, also available with the UI publisher disabled.
 * Returns false on initialization defaults, link loss or expired readings. */
bool elden_battery_temperature_get(int *centi_c,uint32_t *epoch,uint32_t *cycle);
#ifdef __cplusplus
}
#endif
