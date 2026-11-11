/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PdBootstrap.h"
#include <string.h>
bool elden_pd_boot_qualified(const EldenChargeRawSnapshot *raw,const EldenUcsiSnapshot *u,bool fresh,bool ucsiFresh,bool notified,uint32_t budget){
    if(!raw||!u||!fresh||!ucsiFresh||!notified||budget!=500000||raw->cycle<4||!raw->epoch||raw->epoch!=u->epoch||u->connectors!=2||
       raw->valid!=((UINT64_C(1)<<EldenChargeFieldCount)-1))return false;
    const EldenUcsiPort *usb=&u->port[0],*pd=&u->port[1];
    if(!usb->status_valid||!usb->connected||usb->source||!usb->usb||usb->power_mode!=1||
       !pd->status_valid||!pd->connected||pd->source||pd->power_mode!=3||!pd->pdo_count||pd->pdo_count>7||!pd->pdos_complete||!pd->pdos_consistent||pd->pdos_error)return false;
    bool nine=false;
    for(unsigned i=0;i<pd->pdo_count&&i<7;i++)if(!(pd->source_pdos[i]>>30)&&((pd->source_pdos[i]>>10)&1023)*50==9000&&
        (pd->source_pdos[i]&1023)*10>=500)nine=true;
    if(!nine||raw->value[EldenChargeUsbOnline].raw!=1||raw->value[EldenChargeUsbAdapter].raw!=1||
       raw->value[EldenChargeUsbRealType].raw!=1||raw->value[EldenChargePresent].raw!=1||raw->value[EldenChargeHealth].raw!=1||
       raw->value[EldenChargeEnabled].raw!=1||raw->value[EldenChargeInputSuspended].raw||raw->value[EldenChargeUsbMax].raw<500000||
       raw->value[EldenChargeCapacity].raw<4000||raw->value[EldenChargeCapacity].raw>10000||
       raw->value[EldenChargeTemperature].signed_value<1000||raw->value[EldenChargeTemperature].signed_value>=4000)return false;
    uint32_t battery=raw->value[EldenChargeVoltage].raw;if(battery<3800000||battery>4500000)return false;
    for(unsigned i=EldenChargeFg1Voltage;i<=EldenChargeFg2Voltage;++i){uint32_t v=raw->value[i].raw;
        if(v<3800000||v>4500000||(v>battery?v-battery:battery-v)>200000)return false;}
    return true;
}
void elden_pd_boot_init(EldenPdBootstrap *s,unsigned mode,uint64_t now){
    memset(s,0,sizeof(*s));s->mode=mode;s->last_ms=now;
    s->phase=(mode==1||mode==2)?EldenPdBootWait:EldenPdBootOff;s->expires=now+120000;
}
static void fail(EldenPdBootstrap *s,enum EldenPdBootError e){s->phase=EldenPdBootFailed;s->error=e;}
static void restore(EldenPdBootstrap *s,const EldenPdBootInput *in,enum EldenPdBootError e){
    s->phase=EldenPdBootRestore;s->expires=in->now_ms+10000;s->restore_cycle=in->cycle;s->error=e;
}
void elden_pd_boot_step(EldenPdBootstrap *s,const EldenPdBootInput *in,EldenPdBootAction *out){
    if(!out)return;
    memset(out,0,sizeof(*out));if(!s||!in)return;
    if(s->phase==EldenPdBootOff){out->stop_ready=true;return;}
    if(in->now_ms<s->last_ms)fail(s,EldenPdBootExpired);
    s->last_ms=in->now_ms;
    bool active=s->phase>=EldenPdBootPulse&&s->phase<=EldenPdBootRestore;
    if(active&&(!in->link_up||in->epoch!=s->epoch))fail(s,EldenPdBootLinkLost);
    if(active&&in->icl_quarantined)fail(s,EldenPdBootIclUnknown);
    if(in->stop&&s->phase!=EldenPdBootDone&&s->phase!=EldenPdBootFailed){
        if(s->phase==EldenPdBootSuspend)restore(s,in,EldenPdBootStopped);
        else if(s->phase!=EldenPdBootRestore)fail(s,EldenPdBootStopped);
    }
    switch(s->phase){
    case EldenPdBootWait:
        if(s->epoch!=in->epoch){s->epoch=in->epoch;s->last_cycle=0;s->stable=0;}
        if(in->now_ms>=s->expires){fail(s,EldenPdBootExpired);break;}
        if(!in->qualified||!in->raw_fresh||!in->link_up||in->deadline_seconds<120||!in->icl_owned||in->icl_pending||
           in->icl_quarantined||in->icl_applied!=500000||in->icl_observed!=500000||in->pulse!=EldenPulseReady){s->stable=0;break;}
        if(in->cycle>s->last_cycle){s->last_cycle=in->cycle;++s->stable;}
        else if(in->cycle<s->last_cycle){s->stable=0;break;}
        if(s->stable<2)break;
        s->epoch=in->epoch;s->stable=0;s->last_cycle=in->cycle;
        s->expires=in->now_ms+(in->high_voltage?45000:10000);
        if(in->high_voltage)s->phase=EldenPdBootContract;
        else{s->phase=EldenPdBootPulse;out->request_pulse=true;}
        break;
    case EldenPdBootPulse:
        if(in->pulse==EldenPulseRestored){s->phase=EldenPdBootContract;s->expires=in->now_ms+45000;s->last_cycle=in->cycle;s->stable=0;}
        else if(in->pulse==EldenPulseRefused||in->pulse==EldenPulseUncertain)fail(s,EldenPdBootPulseFailed);
        else if(in->now_ms>=s->expires)fail(s,EldenPdBootExpired);
        break;
    case EldenPdBootContract:
        if(in->now_ms>=s->expires||in->deadline_seconds<45){fail(s,EldenPdBootExpired);break;}
        if(!in->qualified||!in->raw_fresh||!in->high_voltage||in->icl_pending||in->icl_applied!=500000||in->icl_observed!=500000){s->stable=0;break;}
        if(in->cycle>s->last_cycle){s->last_cycle=in->cycle;++s->stable;}
        else if(in->cycle<s->last_cycle){s->stable=0;break;}
        if(s->stable<2)break;
        if(s->mode==1){s->phase=EldenPdBootDone;s->restored=true;}
        else{s->phase=EldenPdBootSuspend;s->expires=in->now_ms+20000;s->saw_zero=false;}
        break;
    case EldenPdBootSuspend:
        /* Each GET cycle is consumed once. Once zero has been confirmed, a
         * newer nonzero GET ends the experiment even if another SET is pending.
         * Do not reassert zero or report a continuous hold after interruption. */
        if(in->raw_fresh&&in->cycle>s->last_cycle){
            s->last_cycle=in->cycle;
            if(s->saw_zero&&in->icl_observed!=0){restore(s,in,EldenPdBootInterrupted);break;}
            if(in->icl_applied==0&&in->icl_observed==0&&!in->icl_pending)s->saw_zero=true;
        }
        if(!in->qualified||!in->raw_fresh||!in->high_voltage||!in->icl_owned||in->deadline_seconds<30)restore(s,in,EldenPdBootUnsafe);
        else if(in->now_ms>=s->expires)restore(s,in,s->saw_zero?EldenPdBootOk:EldenPdBootNoZero);
        break;
    case EldenPdBootRestore:
        if(in->raw_fresh&&in->cycle>s->restore_cycle&&!in->icl_pending&&in->icl_applied!=0&&in->icl_observed==in->icl_applied){
            s->restored=true;s->phase=s->error==EldenPdBootOk?EldenPdBootDone:EldenPdBootFailed;
        }else if(in->now_ms>=s->expires)fail(s,EldenPdBootExpired);
        break;
    default:break;
    }
    out->suspend_icl=s->phase==EldenPdBootSuspend;
    out->cancel_pulse=in->stop||s->phase==EldenPdBootFailed||s->phase==EldenPdBootRestore;
    out->stop_ready=!out->request_pulse&&s->phase!=EldenPdBootPulse&&!out->suspend_icl&&in->pulse!=EldenPulseQueued&&in->pulse!=EldenPulseHigh&&in->pulse!=EldenPulseUncertain&&
        in->link_up&&!in->icl_pending&&!in->icl_quarantined&&in->raw_fresh&&in->icl_applied!=0&&in->icl_observed==in->icl_applied;
}
