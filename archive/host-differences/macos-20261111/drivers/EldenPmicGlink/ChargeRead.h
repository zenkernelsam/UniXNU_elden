// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only board codec for Qualcomm/Lenovo PMIC GLINK. Owner/type/layout
// follow the stock TB322FC qti_battery_charger.ko and fixed Linux references.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {ELDEN_CHARGE_OWNER=32778,ELDEN_CHARGE_BATTERY_GET=0x30,
      ELDEN_CHARGE_USB_GET=0x32,ELDEN_CHARGE_WLS_GET=0x34,ELDEN_CHARGE_LENOVO_GET=0x60};
enum {ELDEN_LENOVO_INPUT_SUSPEND=27,ELDEN_LENOVO_CHARGING_ENABLE=28,
      ELDEN_LENOVO_USB1_ONLINE=49,ELDEN_LENOVO_USB2_ONLINE=50,ELDEN_LENOVO_PPS_STATUS=52};
typedef struct {uint32_t opcode,property;} EldenChargeQuery;
typedef struct {uint32_t raw;int64_t signed_value;} EldenChargeValue;
enum {EldenChargeOk=0,EldenChargeArgument=-1,EldenChargeLength=-2,
      EldenChargeMismatch=-3,EldenChargeFirmwareError=-4};
// Only allowlisted GET opcodes are emitted. Invalid requests leave out untouched.
int elden_charge_read_request(EldenChargeQuery query,uint8_t out[24]);
// Exactly one scalar reply for the current query. Failures never alter value.
int elden_charge_read_reply(EldenChargeQuery query,const uint8_t *data,size_t size,EldenChargeValue *value);
#ifdef __cplusplus
}
#endif
