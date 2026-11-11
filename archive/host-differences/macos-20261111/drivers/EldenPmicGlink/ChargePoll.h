/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "ChargeRead.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenChargeField {
    EldenChargeStatus, EldenChargeHealth, EldenChargePresent, EldenChargeType,
    EldenChargeCapacity, EldenChargeVoltage, EldenChargeCurrent, EldenChargeTemperature,
    EldenChargeCounter, EldenChargeCycles, EldenChargeDesign, EldenChargeFull,
    EldenChargeTimeFull, EldenChargeTimeEmpty, EldenChargeUsbOnline, EldenChargeUsbVoltage,
    EldenChargeUsbCurrent, EldenChargeUsbMax, EldenChargeUsbLimit, EldenChargeUsbType,
    EldenChargeUsbAdapter, EldenChargeFg1Temperature, EldenChargeFg2Temperature,
    EldenChargeFg1Current, EldenChargeFg2Current, EldenChargeFg1Voltage, EldenChargeFg2Voltage,
    EldenChargeInputSuspended, EldenChargeEnabled, EldenChargeFg1Soc, EldenChargeFg2Soc,
    EldenChargeMainIbus, EldenChargeSlaveIbus, EldenChargeTotalIbus,
    EldenChargeUsb1Online, EldenChargeUsb2Online, EldenChargePpsStatus, EldenChargeFieldCount
};
typedef struct {
    EldenChargeValue value[EldenChargeFieldCount];
    uint64_t valid;
    uint32_t epoch, cycle;
} EldenChargeRawSnapshot;
typedef struct {
    EldenChargeRawSnapshot working, committed;
    uint32_t epoch, cycles, responses, ignored, errors, timeouts, notifications;
    unsigned cursor, fields;
    bool up, active, pending, refresh_wanted, quarantined;
    int failed_field;
} EldenChargePoll;
enum {EldenChargePollIgnored=0, EldenChargePollAnswer=1, EldenChargePollCycle=2,
      EldenChargePollNotified=3, EldenChargePollStopped=-1};

/* Single executor. A timeout poisons the channel until a real fresh link;
 * there is no protocol nonce with which to distinguish a delayed same-query reply.
 * Only allowlisted GET requests are emitted, never charge/PD control commands.
 */
void elden_charge_poll_init(EldenChargePoll *poll, bool extended);
void elden_charge_poll_link(EldenChargePoll *poll, bool up);
void elden_charge_poll_refresh(EldenChargePoll *poll);
int elden_charge_poll_next(EldenChargePoll *poll, uint8_t out[24]);
/* Only when local queue refused the message before enqueueing any bytes. */
void elden_charge_poll_unsent(EldenChargePoll *poll);
void elden_charge_poll_timeout(EldenChargePoll *poll);
int elden_charge_poll_receive(EldenChargePoll *poll, const uint8_t *data, size_t bytes);
EldenChargeQuery elden_charge_poll_query(unsigned field);
#ifdef __cplusplus
}
#endif
