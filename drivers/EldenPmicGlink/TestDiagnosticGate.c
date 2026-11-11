/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DiagnosticGate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static EldenPdBootstrap state(void){EldenPdBootstrap s;memset(&s,0,sizeof(s));s.phase=EldenPdBootFailed;s.error=EldenPdBootExpired;s.epoch=7;return s;}
static EldenUsbIcl limit(void){EldenUsbIcl i;memset(&i,0,sizeof(i));i.up=i.owns_limit=true;i.applied=500000;return i;}
int main(void){
    EldenPdBootstrap s=state();EldenUsbIcl i=limit();
    CHECK(elden_diagnostic_terminal(&s,&i,EldenPulseRestored,7,500000,true,true));
    CHECK(!elden_diagnostic_terminal(&s,&i,EldenPulseRestored,7,500000,true,false));
    for(unsigned phase=0;phase<=EldenPdBootFailed;phase++)for(unsigned pulse=0;pulse<=EldenPulseUncertain;pulse++){
        s=state();s.phase=(enum EldenPdBootPhase)phase;
        bool expected=phase==EldenPdBootFailed&&(pulse==EldenPulseReady||pulse==EldenPulseRestored||pulse==EldenPulseRefused);
        CHECK(elden_diagnostic_terminal(&s,&i,pulse,7,500000,true,true)==expected);
    }
    for(unsigned fault=0;fault<12;fault++){
        s=state();i=limit();unsigned epoch=7,observed=500000;bool fresh=true;
        switch(fault){case 0:i.up=false;break;case 1:i.owns_limit=false;break;case 2:i.pending=true;break;
        case 3:i.quarantined=true;break;case 4:i.suspend_requested=true;break;case 5:i.applied=0;break;
        case 6:i.applied=900000;break;case 7:epoch=8;break;case 8:observed=0;break;case 9:fresh=false;break;
        case 10:s.saw_zero=true;break;case 11:s.restore_cycle=3;break;}
        CHECK(!elden_diagnostic_terminal(&s,&i,EldenPulseRestored,epoch,observed,fresh,true));
    }
    s=state();i=limit();s.saw_zero=true;s.restore_cycle=3;s.restored=true;
    CHECK(elden_diagnostic_terminal(&s,&i,EldenPulseRestored,7,500000,true,true));
    s.phase=EldenPdBootDone;s.error=EldenPdBootOk;s.restored=true;
    CHECK(elden_diagnostic_terminal(&s,&i,EldenPulseRestored,7,500000,true,false));
    printf("DiagnosticGate: %u checks; settled GET-only failures, active/unknown/recovery states refused\n",checks);
}
