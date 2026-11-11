/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only: feed actual captured GLINK words through the production decoder.
 * Each row: epoch cycle age_ms valid_mask(hex) then all raw uint32 words(hex).
 */
#include "BatterySnapshot.h"
#include <stdio.h>
#include <string.h>
int main(void){
    unsigned epoch,cycle,age;unsigned long long valid;
    EldenBatteryGate gate={0};
    while(scanf("%u %u %u %llx",&epoch,&cycle,&age,&valid)==4){
        EldenChargeRawSnapshot raw;memset(&raw,0,sizeof(raw));raw.epoch=epoch;raw.cycle=cycle;raw.valid=valid;
        for(unsigned i=0;i<EldenChargeFieldCount;i++)if(scanf("%x",&raw.value[i].raw)!=1)return 2;
        EldenBatteryContract contract={1,-1,10000};EldenBatterySnapshot s;
        int rc=elden_battery_snapshot(&raw,&contract,age,&s);
        EldenBatterySnapshot accepted;
        int admitted=elden_battery_gate_update(&gate,&raw,&contract,age,&accepted);
        printf("{\"epoch\":%u,\"cycle\":%u,\"result\":%d,\"publication\":%d",epoch,cycle,rc,admitted);
        if(!rc)printf(",\"percent\":%d,\"mv\":%d,\"ma\":%d,\"temp_centi_c\":%d,\"external\":%u,\"external_capable\":%u,\"charging\":%u,\"remaining_mah\":%d,\"full_mah\":%d,\"minutes\":%d,\"optional\":%u",
            s.percent,s.voltage_mv,s.current_ma,s.temperature_centi_c,(unsigned)s.external,
            (unsigned)s.external_charge_capable,(unsigned)s.charging,s.remaining_mah,s.full_mah,s.time_remaining_min,s.optional);
        puts("}");
    }
    return ferror(stdin)?3:0;
}
