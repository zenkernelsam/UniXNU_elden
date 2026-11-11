/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PdBootstrap.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static EldenPdBootInput input(void){
    EldenPdBootInput i={0};i.epoch=1;i.cycle=4;i.deadline_seconds=240;i.icl_applied=i.icl_observed=500000;
    i.pulse=EldenPulseReady;i.link_up=i.qualified=i.raw_fresh=i.icl_owned=true;return i;
}
static EldenPdBootAction step(EldenPdBootstrap *s,EldenPdBootInput *i){EldenPdBootAction a;elden_pd_boot_step(s,i,&a);return a;}
static void to_pulse(EldenPdBootstrap *s,EldenPdBootInput *i,unsigned mode){
    *i=input();elden_pd_boot_init(s,mode,0);CHECK(!step(s,i).request_pulse);
    i->now_ms=3000;i->cycle++;CHECK(step(s,i).request_pulse);CHECK(s->phase==EldenPdBootPulse);
    CHECK(!step(s,i).request_pulse);
}
static void to_contract(EldenPdBootstrap *s,EldenPdBootInput *i,unsigned mode){
    to_pulse(s,i,mode);i->pulse=EldenPulseRestored;i->now_ms+=500;CHECK(!step(s,i).request_pulse);CHECK(s->phase==EldenPdBootContract);
    i->high_voltage=true;i->now_ms+=3000;i->cycle++;CHECK(!step(s,i).suspend_icl);
    i->now_ms+=3000;i->cycle++;step(s,i);
}
int main(void){
    EldenPdBootstrap s;EldenPdBootInput i;EldenPdBootAction a;
    EldenChargeRawSnapshot raw={0};EldenUcsiSnapshot u={0};raw.epoch=u.epoch=1;raw.cycle=4;
    raw.valid=(UINT64_C(1)<<EldenChargeFieldCount)-1;u.connectors=2;
    u.port[0].status_valid=u.port[0].connected=u.port[0].usb=true;u.port[0].power_mode=1;
    u.port[1].status_valid=u.port[1].connected=u.port[1].pdos_complete=u.port[1].pdos_consistent=true;
    u.port[1].power_mode=3;u.port[1].pdo_count=2;u.port[1].source_pdos[0]=0x0a01912c;u.port[1].source_pdos[1]=0x0002d0de;
    raw.value[EldenChargeUsbOnline].raw=raw.value[EldenChargeUsbAdapter].raw=raw.value[EldenChargeUsbRealType].raw=1;
    raw.value[EldenChargePresent].raw=raw.value[EldenChargeHealth].raw=raw.value[EldenChargeEnabled].raw=1;
    raw.value[EldenChargeUsbMax].raw=900000;raw.value[EldenChargeCapacity].raw=6000;
    raw.value[EldenChargeTemperature].signed_value=2500;
    raw.value[EldenChargeVoltage].raw=raw.value[EldenChargeFg1Voltage].raw=raw.value[EldenChargeFg2Voltage].raw=4000000;
    CHECK(elden_pd_boot_qualified(&raw,&u,true,true,true,500000));
    for(unsigned f=0;f<17;f++){
        EldenChargeRawSnapshot r=raw;EldenUcsiSnapshot v=u;bool fresh=true,ufresh=true,notify=true;uint32_t budget=500000;
        switch(f){case 0:fresh=false;break;case 1:ufresh=false;break;case 2:notify=false;break;case 3:budget=100000;break;
        case 4:r.cycle=3;break;case 5:r.epoch=2;break;case 6:r.valid=0;break;case 7:v.port[0].source=true;break;
        case 8:v.port[1].pdos_consistent=false;break;case 9:v.port[1].source_pdos[1]=0;break;case 10:v.port[1].pdo_count=8;break;
        case 11:r.value[EldenChargeUsbMax].raw=100000;break;case 12:r.value[EldenChargeCapacity].raw=3999;break;
        case 13:r.value[EldenChargeTemperature].signed_value=4000;break;case 14:r.value[EldenChargeEnabled].raw=0;break;
        case 15:r.value[EldenChargeInputSuspended].raw=1;break;case 16:r.value[EldenChargeFg2Voltage].raw=3500000;break;}
        CHECK(!elden_pd_boot_qualified(&r,&v,fresh,ufresh,notify,budget));
    }
    for(unsigned f=0;f<9;f++){
        i=input();elden_pd_boot_init(&s,2,0);
        switch(f){case 0:i.qualified=false;break;case 1:i.raw_fresh=false;break;case 2:i.deadline_seconds=119;break;
        case 3:i.icl_owned=false;break;case 4:i.icl_pending=true;break;case 5:i.icl_quarantined=true;break;
        case 6:i.icl_applied=100000;break;case 7:i.icl_observed=100000;break;case 8:i.pulse=EldenPulseOff;break;}
        for(unsigned n=0;n<10;n++){i.cycle++;i.now_ms+=3000;a=step(&s,&i);CHECK(!a.request_pulse&&!a.suspend_icl);}
    }
    i=input();elden_pd_boot_init(&s,2,0);step(&s,&i);i.epoch=2;i.cycle=5;CHECK(!step(&s,&i).request_pulse);
    i.cycle=6;CHECK(step(&s,&i).request_pulse);
    i=input();elden_pd_boot_init(&s,2,0);step(&s,&i);i.cycle=3;CHECK(!step(&s,&i).request_pulse);
    to_contract(&s,&i,1);CHECK(s.phase==EldenPdBootDone&&s.restored);CHECK(!step(&s,&i).suspend_icl);
    for(unsigned n=0;n<50;n++){i.now_ms+=1000;i.cycle++;a=step(&s,&i);CHECK(!a.request_pulse&&!a.suspend_icl);}
    to_contract(&s,&i,2);CHECK(s.phase==EldenPdBootSuspend);uint64_t expiry=s.expires;
    i.icl_applied=i.icl_observed=0;i.cycle++;i.now_ms+=1000;CHECK(step(&s,&i).suspend_icl&&s.saw_zero);
    i.now_ms=expiry-1;CHECK(step(&s,&i).suspend_icl&&s.expires==expiry);
    i.now_ms=expiry;CHECK(!step(&s,&i).suspend_icl&&s.phase==EldenPdBootRestore);
    i.icl_applied=i.icl_observed=500000;CHECK(s.phase==EldenPdBootRestore);step(&s,&i);CHECK(!s.restored);
    i.cycle++;CHECK(step(&s,&i).stop_ready&&s.phase==EldenPdBootDone&&s.restored);
    /* A firmware limit change after a confirmed zero must restore promptly,
     * including while a SET is in flight and at the nominal hold boundary. */
    for(unsigned pending=0;pending<2;pending++)for(unsigned boundary=0;boundary<2;boundary++){
        to_contract(&s,&i,2);i.icl_applied=i.icl_observed=0;i.cycle++;i.now_ms+=1000;
        CHECK(step(&s,&i).suspend_icl&&s.saw_zero);uint32_t zero_cycle=i.cycle;
        i.icl_observed=100000;i.icl_pending=pending;
        CHECK(step(&s,&i).suspend_icl&&s.last_cycle==zero_cycle); /* cached cycle */
        i.cycle--;CHECK(step(&s,&i).suspend_icl); /* reordered old cycle */
        i.cycle=zero_cycle+1;i.now_ms=boundary?s.expires:i.now_ms+1;
        a=step(&s,&i);CHECK(!a.suspend_icl&&a.cancel_pulse&&s.phase==EldenPdBootRestore);
        CHECK(s.error==EldenPdBootInterrupted&&!s.restored);
        i.icl_pending=false;i.icl_applied=i.icl_observed=500000;
        step(&s,&i);CHECK(!s.restored);i.cycle++;a=step(&s,&i);
        CHECK(s.restored&&s.phase==EldenPdBootFailed&&s.error==EldenPdBootInterrupted&&a.stop_ready);
        i.cycle++;CHECK(!step(&s,&i).suspend_icl); /* no second attempt */
    }
    to_contract(&s,&i,2);i.icl_owned=false;
    CHECK(!step(&s,&i).suspend_icl&&s.phase==EldenPdBootRestore&&s.error==EldenPdBootUnsafe);
    to_contract(&s,&i,2);i.stop=true;a=step(&s,&i);CHECK(!a.suspend_icl&&a.cancel_pulse&&s.phase==EldenPdBootRestore);
    i.cycle++;step(&s,&i);CHECK(s.phase==EldenPdBootFailed&&s.error==EldenPdBootStopped&&s.restored);
    to_contract(&s,&i,2);i.icl_quarantined=true;a=step(&s,&i);CHECK(!a.suspend_icl&&!a.stop_ready&&s.error==EldenPdBootIclUnknown);
    to_contract(&s,&i,2);i.link_up=false;a=step(&s,&i);CHECK(!a.suspend_icl&&s.error==EldenPdBootLinkLost);
    to_contract(&s,&i,2);i.epoch++;CHECK(!step(&s,&i).suspend_icl&&s.error==EldenPdBootLinkLost);
    to_contract(&s,&i,2);i.high_voltage=false;CHECK(!step(&s,&i).suspend_icl&&s.phase==EldenPdBootRestore);
    to_pulse(&s,&i,2);i.pulse=EldenPulseHigh;i.stop=true;a=step(&s,&i);CHECK(a.cancel_pulse&&!a.stop_ready);
    i.pulse=EldenPulseRestored;CHECK(step(&s,&i).stop_ready);
    to_pulse(&s,&i,2);i.pulse=EldenPulseUncertain;a=step(&s,&i);CHECK(!a.stop_ready&&s.phase==EldenPdBootFailed);
    to_pulse(&s,&i,2);i.now_ms=s.expires;a=step(&s,&i);CHECK(a.cancel_pulse&&s.error==EldenPdBootExpired);
    to_contract(&s,&i,2);i.now_ms=s.expires;step(&s,&i);CHECK(s.error==EldenPdBootNoZero&&!s.restored);
    i.now_ms=s.expires;step(&s,&i);CHECK(s.phase==EldenPdBootFailed&&!s.restored);
    puts("PASS: one-shot pulse, fresh epochs/cycles, PD wait, bounded ICL hold, restore, stop and unknown completion");
    printf("%u checks\n",checks);
}
