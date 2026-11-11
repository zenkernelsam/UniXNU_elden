/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenThermalKeep=-1,EldenThermalLow=0,EldenThermalHigh=1};
/* Original Lenovo connector GPIO policy, input units explicit. This only
 * decides the desired level; it never changes a pin, rail or input source. */
int elden_usb_thermal_decision(unsigned port,bool connector_valid,int connector_mc,
    bool battery_valid,int battery_centi_c);
#ifdef __cplusplus
}
#endif
