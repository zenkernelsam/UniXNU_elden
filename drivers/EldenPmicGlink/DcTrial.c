/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcTrial.h"
#include <string.h>
static bool snapshot_ok(const EldenPowerPathRead *s){
    const uint8_t *v=s->values;
    if(!s->cycles||s->failed||v[0]!=0xdb||v[18]!=0xdb||v[1]!=1||v[2]!=0||v[3]!=10)return false;
    for(unsigned i=4;i<=11;i+=7)if(v[i]!=0x10||v[i+1]!=0x12||v[i+2]!=1||v[i+3]!=1||v[i+4]!=0||v[i+5]!=0x80||v[i+6]!=0x80)return false;
    return true;
}
void elden_dc_trial_init(EldenDcTrial *s){memset(s,0,sizeof(*s));}
void elden_dc_trial_fail(EldenDcTrial *s,unsigned error){s->phase=EldenDcTrialFailed;s->error=error;s->snapshot.pending=false;}
void elden_dc_trial_link(EldenDcTrial *s,bool up,uint32_t epoch){
    if(s->up==up&&s->epoch==epoch)return;s->up=up;s->epoch=epoch;
    if(s->started)elden_dc_trial_fail(s,EldenDcTrialEpoch);
}
int elden_dc_trial_begin(EldenDcTrial *s,uint64_t now){
    if(!s->up||s->started||s->phase!=EldenDcTrialOff)return 0;
    s->started=true;s->expires=now+8000;s->phase=EldenDcTrialPre;
    elden_power_reg_init(&s->snapshot);elden_power_reg_extend(&s->snapshot);
    elden_power_reg_link(&s->snapshot,true,s->epoch);return elden_power_reg_begin(&s->snapshot);
}
int elden_dc_trial_next_read(EldenDcTrial *s,uint8_t out[28]){
    return (s->phase==EldenDcTrialPre||s->phase==EldenDcTrialFinal)?elden_power_reg_next(&s->snapshot,out):0;
}
void elden_dc_trial_unsent(EldenDcTrial *s){elden_power_reg_unsent(&s->snapshot);}
int elden_dc_trial_receive(EldenDcTrial *s,const uint8_t *p,size_t n){
    if(s->phase!=EldenDcTrialPre&&s->phase!=EldenDcTrialFinal)return 0;
    int rc=elden_power_reg_receive(&s->snapshot,p,n);
    if(rc<0){elden_dc_trial_fail(s,EldenDcTrialProtocol);return -1;}
    if(rc==2){
        if(!snapshot_ok(&s->snapshot)){elden_dc_trial_fail(s,EldenDcTrialUnsafe);return -1;}
        if(s->phase==EldenDcTrialPre)s->phase=EldenDcTrialOpen;
        else{s->phase=EldenDcTrialDone;s->restored=true;}
    }
    return rc;
}
static void final_read(EldenDcTrial *s,uint64_t now){
    s->phase=EldenDcTrialFinal;s->expires=now+2000;elden_power_reg_begin(&s->snapshot);
}
void elden_dc_trial_step(EldenDcTrial *s,uint64_t now,bool qualified,bool stop,bool abort,const EldenDcTrialIo *io){
    if(s->phase==EldenDcTrialDone||s->phase==EldenDcTrialFailed)return;
    if(abort){elden_dc_trial_fail(s,EldenDcTrialStopped);return;}
    if(s->phase==EldenDcTrialOff){if(stop)elden_dc_trial_fail(s,EldenDcTrialStopped);return;}
    if(!s->up){elden_dc_trial_fail(s,EldenDcTrialEpoch);return;}
    if(now>=s->expires){elden_dc_trial_fail(s,EldenDcTrialTimeout);return;}
    if(stop&&s->phase<=EldenDcTrialLower){elden_dc_trial_fail(s,EldenDcTrialStopped);return;}
    if(!io||!io->open||!io->check||!io->write){elden_dc_trial_fail(s,EldenDcTrialIoError);return;}
    int rc;
    switch(s->phase){
    case EldenDcTrialOpen:
        if(!qualified)return;
        if(io->open(io->context)||io->check(io->context,10)){elden_dc_trial_fail(s,EldenDcTrialIoError);return;}
        s->phase=EldenDcTrialLower;break;
    case EldenDcTrialLower:
        if(!qualified)return;
        if(io->check(io->context,10)){elden_dc_trial_fail(s,EldenDcTrialUnsafe);return;}
        rc=io->write(io->context,9);if(rc==1)return;
        ++s->write_attempts;if(rc){elden_dc_trial_fail(s,EldenDcTrialIoError);return;}
        ++s->write_completions;s->phase=EldenDcTrialCheckLow;s->expires=now+12000;break;
    case EldenDcTrialCheckLow:
        rc=io->check(io->context,9);
        if(rc==1){s->overridden=true;final_read(s,now);return;}
        if(rc){elden_dc_trial_fail(s,EldenDcTrialIoError);return;}
        s->lower_seen=true;s->phase=EldenDcTrialHold;s->hold_until=now+5000;s->sample_at=now;break;
    case EldenDcTrialHold:
        /* Keep the existing five-second thermal sampling cadence. */
        if(stop||now>=s->hold_until){s->held_full=!stop;s->phase=EldenDcTrialRestore;s->expires=now+6000;return;}
        if(now<s->sample_at)return;s->sample_at=now+250;
        rc=io->check(io->context,9);
        if(rc==1){s->overridden=true;final_read(s,now);return;}
        if(rc)elden_dc_trial_fail(s,EldenDcTrialOverridden);
        break;
    case EldenDcTrialRestore:
        if(!qualified)return;
        rc=io->check(io->context,9);
        if(rc==1){s->overridden=true;final_read(s,now);return;}
        if(rc){elden_dc_trial_fail(s,EldenDcTrialOverridden);return;}
        rc=io->write(io->context,10);if(rc==1)return;
        ++s->write_attempts;if(rc){elden_dc_trial_fail(s,EldenDcTrialIoError);return;}
        ++s->write_completions;final_read(s,now);break;
    default:break;
    }
}
