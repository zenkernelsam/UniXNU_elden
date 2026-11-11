/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    uint32_t (*tlmm_read)(void *,uint32_t offset);
    int (*tlmm_write)(void *,uint32_t offset,uint32_t value);
    int (*pmic_read)(void *,uint8_t offset,uint8_t *value);
    int (*pmic_write)(void *,uint8_t offset,uint8_t value);
} EldenThermalPinsIo;
typedef struct {
    uint32_t tlmm_config,tlmm_io;
    uint8_t type,subtype,mode,vin,pull,input,source,drive,enable,atest,status;
    int level[2];
} EldenThermalPinView;
typedef struct {
    EldenThermalPinsIo io;EldenThermalPinView expected;
    bool ready,failed[2];unsigned writes[2];int error[2];
} EldenThermalPins;
/* Inherited normal GPIO configuration only. Change TLMM3 OUT, or preload
 * PM8550 GPIO1's level then switch its LV/MV direction to output. No change
 * to VIN, bias, drive, mux, master enable or any unrelated bit. */
int elden_thermal_pins_probe(EldenThermalPins *,EldenThermalPinView *);
int elden_thermal_pins_refresh(EldenThermalPins *,EldenThermalPinView *);
int elden_thermal_pin_set(EldenThermalPins *,unsigned port,int level);
#ifdef __cplusplus
}
#endif
