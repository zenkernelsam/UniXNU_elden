/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BatterySnapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put(EldenChargeRawSnapshot *r,unsigned field,int value){r->value[field].raw=(uint32_t)value;r->valid|=UINT64_C(1)<<field;}
static EldenChargeRawSnapshot sample(void){
    EldenChargeRawSnapshot r={0};r.epoch=1;r.cycle=1;
    put(&r,EldenChargeStatus,1);put(&r,EldenChargePresent,1);put(&r,EldenChargeCapacity,5800);
    put(&r,EldenChargeVoltage,3973000);put(&r,EldenChargeCurrent,577000);put(&r,EldenChargeTemperature,3250);
    put(&r,EldenChargeUsbOnline,1);put(&r,EldenChargeWlsOnline,0);put(&r,EldenChargeInputSuspended,0);put(&r,EldenChargeEnabled,1);
    put(&r,EldenChargeFg1Soc,58);put(&r,EldenChargeFg2Soc,57);
    put(&r,EldenChargeFg1Voltage,3973000);put(&r,EldenChargeFg2Voltage,3964000);
    put(&r,EldenChargeFg1Temperature,325);put(&r,EldenChargeFg2Temperature,321);
    put(&r,EldenChargeFg1Current,-323);put(&r,EldenChargeFg2Current,-254);return r;
}
int main(void){
    EldenBatteryContract c={1,-1,10000};EldenBatteryGate gate={0};EldenBatterySnapshot out,before;
    memset(&before,0xa5,sizeof(before));out=before;
    EldenChargeRawSnapshot r=sample();
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    CHECK(!memcmp(&before,&out,sizeof(out)));
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatteryStale&&gate.confirmations==1);
    r.cycle++;CHECK(!elden_battery_gate_update(&gate,&r,&c,40,&out));CHECK(out.percent==58&&!out.charging&&out.current_ma==-577);
    /* Actual initialization tuple disagrees with the independently read cells. */
    r=sample();r.epoch=2;put(&r,EldenChargeCapacity,5000);put(&r,EldenChargeVoltage,4000000);
    put(&r,EldenChargeCurrent,-4000000);put(&r,EldenChargeTemperature,2500);
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling&&gate.confirmations==0);
    r.cycle++;CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    /* Even at a real 50% SOC, current direction can expose the placeholder. */
    put(&r,EldenChargeFg1Soc,50);put(&r,EldenChargeFg2Soc,50);r.cycle++;
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    /* Unequal cells stay unequal: only the aggregate is published. */
    r=sample();r.epoch=3;put(&r,EldenChargeCapacity,6500);put(&r,EldenChargeFg1Soc,20);put(&r,EldenChargeFg2Soc,90);
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    r.cycle++;CHECK(!elden_battery_gate_update(&gate,&r,&c,40,&out)&&out.percent==65);
    /* A protection hold preserves the supply without needing a new warmup. */
    r.cycle++;put(&r,EldenChargeEnabled,0);put(&r,EldenChargeCurrent,0);put(&r,EldenChargeFg1Current,0);put(&r,EldenChargeFg2Current,0);
    CHECK(!elden_battery_gate_update(&gate,&r,&c,40,&out)&&out.external_charge_capable&&!out.charging);
    r.cycle++;CHECK(elden_battery_gate_update(&gate,&r,&c,10001,&out)==EldenBatteryStale&&gate.confirmations==0);
    r.cycle++;CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    r.cycle++;CHECK(!elden_battery_gate_update(&gate,&r,&c,40,&out));
    r.cycle++;r.valid&=~(UINT64_C(1)<<EldenChargeFg1Voltage);
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling&&gate.confirmations==0);
    r=sample();r.epoch=4;CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    r.cycle+=2;CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    r.cycle++;CHECK(!elden_battery_gate_update(&gate,&r,&c,40,&out));
    elden_battery_gate_reset(&gate);CHECK(!gate.epoch&&!gate.last_cycle&&!gate.confirmations);
    r=sample();put(&r,EldenChargeFg2Voltage,0);
    CHECK(elden_battery_gate_update(&gate,&r,&c,40,&out)==EldenBatterySettling);
    CHECK(elden_battery_gate_update(NULL,&r,&c,40,&out)==EldenBatteryArgument);
    printf("Battery publication readiness: %u checks passed\n",checks);
}
