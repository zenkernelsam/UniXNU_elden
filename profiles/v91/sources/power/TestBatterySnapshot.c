/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BatterySnapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static void put(EldenChargeRawSnapshot *r,unsigned field,int64_t value) {
    r->valid|=UINT64_C(1)<<field;r->value[field].raw=(uint32_t)value;
    r->value[field].signed_value=value;
}
int main(void) {
    EldenChargeRawSnapshot r;memset(&r,0,sizeof(r));r.epoch=1;r.cycle=1;
    put(&r,EldenChargeStatus,1);put(&r,EldenChargePresent,1);put(&r,EldenChargeCapacity,6549);
    put(&r,EldenChargeVoltage,3910000);put(&r,EldenChargeCurrent,-470000);
    put(&r,EldenChargeTemperature,2750);put(&r,EldenChargeUsbOnline,0);
    put(&r,EldenChargeWlsOnline,1);put(&r,EldenChargeInputSuspended,0);put(&r,EldenChargeEnabled,1);
    put(&r,EldenChargeCounter,5100000);put(&r,EldenChargeFull,7600000);
    put(&r,EldenChargeDesign,7600000);put(&r,EldenChargeCycles,0);put(&r,EldenChargeTimeFull,7200);
    /* Cells differ: aggregate SOC must not be replaced by their average. */
    put(&r,EldenChargeFg1Soc,20);put(&r,EldenChargeFg2Soc,90);
    EldenBatteryContract c={1,-1,10000};EldenBatterySnapshot s;
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0);
    CHECK(s.percent==65 && s.voltage_mv==3910 && s.current_ma==470 && s.temperature_centi_c==2750);
    CHECK(s.charging && s.external && !s.fully_charged && s.cycles==0 && s.time_remaining_min==120);
    CHECK(s.remaining_mah==5100 && s.full_mah==7600 && s.design_mah==7600);
    CHECK(s.external_charge_capable);
    put(&r,EldenChargeEnabled,0);put(&r,EldenChargeCurrent,0);put(&r,EldenChargeStatus,3);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && s.external && s.external_charge_capable && !s.charging);
    put(&r,EldenChargeInputSuspended,1);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && s.external && !s.external_charge_capable);
    put(&r,EldenChargeInputSuspended,0);put(&r,EldenChargeEnabled,1);
    put(&r,EldenChargeCurrent,-470000);put(&r,EldenChargeStatus,1);
    put(&r,EldenChargeTimeFull,0);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && !(s.optional&EldenBatteryTimeKnown));
    put(&r,EldenChargeTimeFull,7200);
    c.current_multiplier=1;CHECK(elden_battery_snapshot(&r,&c,0,&s)==0);
    CHECK(s.current_ma==-470 && !s.charging); /* online with net battery drain */
    c.current_multiplier=-1;put(&r,EldenChargeCurrent,470000);
    put(&r,EldenChargeCapacity,200);put(&r,EldenChargeStatus,2);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && s.warn && s.critical && !s.charging);
    put(&r,EldenChargeTemperature,-501);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && s.temperature_centi_c==-501);
    put(&r,EldenChargeStatus,4);put(&r,EldenChargeCapacity,10000);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && s.fully_charged && !s.charging && s.percent==100);
    r.valid&=~(UINT64_C(1)<<EldenChargeFull);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && !(s.optional&EldenBatteryFullKnown));
    put(&r,EldenChargeTimeEmpty,UINT32_MAX);
    CHECK(elden_battery_snapshot(&r,&c,0,&s)==0 && !(s.optional&EldenBatteryTimeKnown));
    for(unsigned field=0;field<EldenChargeFieldCount;++field) {
        EldenChargeRawSnapshot bad=r;EldenBatterySnapshot before,after;
        memset(&before,0xa5,sizeof(before));after=before;bad.valid&=~(UINT64_C(1)<<field);
        int rc=elden_battery_snapshot(&bad,&c,0,&after);
        if(rc)CHECK(memcmp(&before,&after,sizeof(before))==0);
    }
    const struct {unsigned field;int64_t value;} invalid[]={
        {EldenChargeStatus,0},{EldenChargeStatus,5},{EldenChargeCapacity,10001},
        {EldenChargeVoltage,0},{EldenChargeVoltage,UINT32_MAX},{EldenChargeCurrent,INT32_MIN},
        {EldenChargeCurrent,INT32_MAX},{EldenChargeTemperature,-4001},{EldenChargeTemperature,9001},
        {EldenChargeUsbOnline,2},{EldenChargeWlsOnline,2},{EldenChargeInputSuspended,2},
        {EldenChargeEnabled,2},{EldenChargePresent,2}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        EldenChargeRawSnapshot bad=r;put(&bad,invalid[i].field,invalid[i].value);
        EldenBatterySnapshot before,after;memset(&before,0xa5,sizeof(before));after=before;
        CHECK(elden_battery_snapshot(&bad,&c,0,&after)==EldenBatteryInvalid);
        CHECK(memcmp(&before,&after,sizeof(before))==0);
    }
    c.current_multiplier=0;CHECK(elden_battery_snapshot(&r,&c,0,&s)==EldenBatteryUnqualified);
    c.current_multiplier=2;CHECK(elden_battery_snapshot(&r,&c,0,&s)==EldenBatteryUnqualified);
    c.current_multiplier=-1;c.profile=0;CHECK(elden_battery_snapshot(&r,&c,0,&s)==EldenBatteryUnqualified);
    c.profile=1;CHECK(elden_battery_snapshot(&r,&c,10001,&s)==EldenBatteryStale);
    CHECK(elden_battery_snapshot(&r,&c,10000,&s)==0);
    r.epoch=0;CHECK(elden_battery_snapshot(&r,&c,0,&s)==EldenBatteryStale);
    r.epoch=1;r.cycle=0;CHECK(elden_battery_snapshot(&r,&c,0,&s)==EldenBatteryStale);
    CHECK(elden_battery_snapshot(NULL,&c,0,&s)==EldenBatteryArgument);
    printf("PASS Elden battery snapshot: %u checks, two-cell and online/net-current cases\n",checks);
    return 0;
}
