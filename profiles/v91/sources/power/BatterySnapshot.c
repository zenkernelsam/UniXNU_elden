/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BatterySnapshot.h"
#include <string.h>

static bool has(const EldenChargeRawSnapshot *r,unsigned field) {
    return (r->valid & (UINT64_C(1)<<field))!=0;
}
static int64_t signed_raw(uint32_t v) {
    return v>=UINT32_C(0x80000000)?(int64_t)v-INT64_C(4294967296):(int64_t)v;
}
static bool boolean(uint32_t v) {return v<=1;}
static bool charge_known(const EldenChargeRawSnapshot *r,unsigned field,int *mah) {
    if(!has(r,field) || r->value[field].raw>50000000u)return false;
    *mah=(int)(r->value[field].raw/1000u);return true;
}
int elden_battery_snapshot(const EldenChargeRawSnapshot *r,
    const EldenBatteryContract *c,uint64_t age_ms,EldenBatterySnapshot *out) {
    if(!r || !c || !out)return EldenBatteryArgument;
    if(c->profile!=1 || (c->current_multiplier!=1 && c->current_multiplier!=-1) ||
        !c->max_age_ms || c->max_age_ms>10000)return EldenBatteryUnqualified;
    if(!r->epoch || !r->cycle || age_ms>c->max_age_ms)return EldenBatteryStale;
    static const unsigned required[]={EldenChargeStatus,EldenChargePresent,EldenChargeCapacity,
        EldenChargeVoltage,EldenChargeCurrent,EldenChargeTemperature,EldenChargeUsbOnline,
        EldenChargeWlsOnline,EldenChargeInputSuspended,EldenChargeEnabled};
    for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);++i)
        if(!has(r,required[i]))return EldenBatteryMissing;
    const uint32_t status=r->value[EldenChargeStatus].raw;
    const uint32_t soc=r->value[EldenChargeCapacity].raw;
    const uint32_t voltage=r->value[EldenChargeVoltage].raw;
    const int64_t current=signed_raw(r->value[EldenChargeCurrent].raw)*c->current_multiplier;
    const int64_t temp=signed_raw(r->value[EldenChargeTemperature].raw);
    if(status<1 || status>4 || soc>10000 || voltage<500000 || voltage>20000000 ||
        current<-50000000 || current>50000000 || temp<-4000 || temp>9000 ||
        !boolean(r->value[EldenChargePresent].raw) || !boolean(r->value[EldenChargeUsbOnline].raw) ||
        !boolean(r->value[EldenChargeWlsOnline].raw) ||
        !boolean(r->value[EldenChargeInputSuspended].raw) || !boolean(r->value[EldenChargeEnabled].raw))
        return EldenBatteryInvalid;
    EldenBatterySnapshot s;memset(&s,0,sizeof(s));
    s.epoch=r->epoch;s.cycle=r->cycle;s.percent=(int)((soc+50u)/100u);
    s.voltage_mv=(int)(voltage/1000u);s.current_ma=(int)(current/1000);
    s.temperature_centi_c=(int)temp;s.present=r->value[EldenChargePresent].raw!=0;
    s.external=r->value[EldenChargeUsbOnline].raw || r->value[EldenChargeWlsOnline].raw;
    s.input_suspended=r->value[EldenChargeInputSuspended].raw!=0;
    s.charging_enabled=r->value[EldenChargeEnabled].raw!=0;
    s.external_charge_capable=s.external && !s.input_suspended;
    /* An online cable or firmware charge state alone does not establish net charge. */
    s.charging=s.present && s.external && s.charging_enabled && !s.input_suspended &&
        status==1 && current>0;
    s.fully_charged=s.present && status==4;
    s.warn=s.present && current<0 && s.percent<=10;
    s.critical=s.present && current<0 && s.percent<=2;
    s.remaining_mah=s.full_mah=s.design_mah=s.cycles=s.time_remaining_min=s.health=-1;
    if(charge_known(r,EldenChargeCounter,&s.remaining_mah))s.optional|=EldenBatteryCounterKnown;
    if(charge_known(r,EldenChargeFull,&s.full_mah) && s.full_mah>0)s.optional|=EldenBatteryFullKnown;
    if(charge_known(r,EldenChargeDesign,&s.design_mah) && s.design_mah>0)s.optional|=EldenBatteryDesignKnown;
    if(has(r,EldenChargeCycles) && r->value[EldenChargeCycles].raw<100000) {
        s.cycles=(int)r->value[EldenChargeCycles].raw;s.optional|=EldenBatteryCyclesKnown;
    }
    if(has(r,EldenChargeHealth) && r->value[EldenChargeHealth].raw<=20) {
        s.health=(int)r->value[EldenChargeHealth].raw;s.optional|=EldenBatteryHealthKnown;
    }
    unsigned time=s.charging?EldenChargeTimeFull:EldenChargeTimeEmpty;
    /* Zero from an initializing/idle estimator is not a zero-minute claim. */
    if(has(r,time) && r->value[time].raw>0 && r->value[time].raw<360000) {
        s.time_remaining_min=(int)(r->value[time].raw/60u);s.optional|=EldenBatteryTimeKnown;
    }
    *out=s;return EldenBatteryOk;
}
static int64_t difference(int64_t a,int64_t b){return a>b?a-b:b-a;}
static bool within(int value,int a,int b,int margin){
    int low=a<b?a:b,high=a>b?a:b;return value>=low-margin&&value<=high+margin;
}
static bool gauges_agree(const EldenChargeRawSnapshot *r,const EldenBatterySnapshot *s){
    const unsigned fields[]={EldenChargeFg1Soc,EldenChargeFg2Soc,EldenChargeFg1Voltage,EldenChargeFg2Voltage,
        EldenChargeFg1Temperature,EldenChargeFg2Temperature,EldenChargeFg1Current,EldenChargeFg2Current};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)if(!has(r,fields[i]))return false;
    int soc1=(int)r->value[EldenChargeFg1Soc].raw,soc2=(int)r->value[EldenChargeFg2Soc].raw;
    uint32_t v1=r->value[EldenChargeFg1Voltage].raw,v2=r->value[EldenChargeFg2Voltage].raw;
    int64_t t1=signed_raw(r->value[EldenChargeFg1Temperature].raw),t2=signed_raw(r->value[EldenChargeFg2Temperature].raw);
    int64_t i1=signed_raw(r->value[EldenChargeFg1Current].raw),i2=signed_raw(r->value[EldenChargeFg2Current].raw);
    if(soc1<0||soc1>100||soc2<0||soc2>100||v1<2000000||v1>5000000||v2<2000000||v2>5000000||
       t1<-400||t1>900||t2<-400||t2>900||i1<-50000||i1>50000||i2<-50000||i2>50000)return false;
    /* Independent scalars use the original OEM units: SOC%, uV, 0.1C, mA.
     * An envelope allows unequal cells, rounding and sequential sampling. */
    if(!within(s->percent,soc1,soc2,1)||!within(s->voltage_mv,(int)(v1/1000),(int)(v2/1000),150)||
       !within(s->temperature_centi_c,(int)t1*10,(int)t2*10,500))return false;
    /* Both gauges use charge-positive current, opposite the aggregate raw
     * value. Never publish a strong net charge against two discharging cells. */
    if((s->current_ma>20&&i1<-5&&i2<-5)||(s->current_ma<-20&&i1>5&&i2>5))return false;
    return true;
}
void elden_battery_gate_reset(EldenBatteryGate *g){if(g)memset(g,0,sizeof(*g));}
int elden_battery_gate_update(EldenBatteryGate *g,const EldenChargeRawSnapshot *r,
    const EldenBatteryContract *c,uint64_t age,EldenBatterySnapshot *out){
    if(!g||!r||!out)return EldenBatteryArgument;
    if(g->epoch!=r->epoch){elden_battery_gate_reset(g);g->epoch=r->epoch;}
    if(!r->cycle||r->cycle<=g->last_cycle)return EldenBatteryStale;
    bool consecutive=!g->last_cycle||r->cycle==g->last_cycle+1;
    g->last_cycle=r->cycle;
    EldenBatterySnapshot s;int rc=elden_battery_snapshot(r,c,age,&s);
    if(rc){g->confirmations=0;return rc;}
    if(!gauges_agree(r,&s)){g->confirmations=0;return EldenBatterySettling;}
    bool coherent=g->confirmations&&consecutive&&difference(s.percent,g->previous.percent)<=2&&
        difference(s.voltage_mv,g->previous.voltage_mv)<=200&&
        difference(s.temperature_centi_c,g->previous.temperature_centi_c)<=500;
    g->confirmations=coherent?2:1;g->previous=s;
    if(g->confirmations<2)return EldenBatterySettling;
    *out=s;return 0;
}
