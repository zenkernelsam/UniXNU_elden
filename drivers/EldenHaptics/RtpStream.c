/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "RtpStream.h"
static int limits(const EldenRtpLimits *l){
    if(!l||!l->mask||l->mask>3||!l->max_duration_us||l->max_duration_us>60000000||
       !l->max_service_gap_us||l->max_service_gap_us>10000||!l->measured_min_payload_bps||
       !l->window_frames||l->window_frames>240000||!l->max_sum_gain||l->max_sum_gain>256)return 0;
    for(unsigned m=0;m<2;m++)if(l->mask&(1u<<m)){
        if(!l->max_gain[m]||l->max_gain[m]>128||!l->max_sample[m]||l->max_sample[m]>127||
           !l->max_window_squares[m]||!l->max_window_on_frames[m]||l->max_window_on_frames[m]>l->window_frames)return 0;
    }
    return 1;
}
static int preflight(const EldenRtpLimits *l,const EldenRtpClip *c,uint64_t squares[2],uint32_t on[2]){
    if(!c||!c->id||!c->mask||c->mask&~l->mask||c->rate_hz!=24000||!c->frames||c->frames>1440000)return -22;
    uint64_t duration=((uint64_t)c->frames*1000000+23999)/24000;
    if(duration>l->max_duration_us||(unsigned)c->gain[0]+c->gain[1]>l->max_sum_gain)return -13;
    unsigned motors=0;
    for(unsigned m=0;m<2;m++)if(c->mask&(1u<<m)){
        motors++;if(!c->pcm[m]||!c->gain[m]||c->gain[m]>l->max_gain[m])return -13;
        uint64_t rolling=0,peak=0;
        for(size_t i=0;i<c->frames;i++){
            int value=c->pcm[m][i];unsigned magnitude=(unsigned)(value<0?-value:value);
            if(magnitude>l->max_sample[m])return -13;
            rolling+=(uint64_t)magnitude*magnitude*c->gain[m]*c->gain[m];
            if(i>=l->window_frames){int old=c->pcm[m][i-l->window_frames];unsigned oldmag=(unsigned)(old<0?-old:old);
                rolling-=(uint64_t)oldmag*oldmag*c->gain[m]*c->gain[m];}
            if(rolling>peak)peak=rolling;
        }
        // A zero PCM crossing does not turn a running motor off. Reserve its
        // entire streaming window, separately from the squared sample proxy.
        uint32_t motor_on=(uint32_t)(c->frames<l->window_frames?c->frames:l->window_frames);
        if(peak>l->max_window_squares[m]||motor_on>l->max_window_on_frames[m])return -13;
        squares[m]=peak;on[m]=motor_on;
    }
    // Measured complete status+payload path, not raw I2C clock frequency.
    if(l->measured_min_payload_bps<24000u*motors*5u/4u)return -13;
    return 0;
}
static int stop_all(EldenRtpStream *s,int error){
    int failure=0;uint8_t retained=0;
    for(unsigned m=0;m<2;m++)if(s->motor[m].chip){
        int rc=elden_aw_rtp_stop(&s->motor[m]);if(rc){retained|=(uint8_t)(1u<<m);if(!failure)failure=rc;}
    }
    s->active=retained;s->error=failure?failure:error;s->state=failure?EldenRtpHeld:error?EldenRtpFailed:EldenRtpCancelled;
    return failure?failure:error;
}
int elden_rtp_stream_start(EldenRtpStream *s,EldenAwChip *chips[2],const EldenRtpIo *io,const EldenRtpLimits *l,const EldenRtpClip *clip){
    if(!s||!chips||!io||!io->admit||!io->reserve||!io->now_us||!limits(l)||s->state!=EldenRtpIdle)return -22;
    uint64_t squares[2]={0};uint32_t on[2]={0};int rc=preflight(l,clip,squares,on);if(rc)return rc;
    for(unsigned m=0;m<2;m++)if(clip->mask&(1u<<m)){
        if(!chips[m]||!chips[m]->ready||chips[m]->held||chips[m]->busy||clip->gain[m]>chips[m]->max_gain)return -13;
    }
    rc=io->admit(io->context,clip->id,0);if(rc)return rc;
    uint32_t duration=(uint32_t)(((uint64_t)clip->frames*1000000+23999)/24000);
    rc=io->reserve(io->context,clip->id,duration+500000,squares,on);if(rc)return rc;
    uint64_t now=io->now_us(io->context);if(now>UINT64_MAX-500000)return -22;
    EldenRtpStream value={0};value.io=*io;value.limits=*l;value.clip=*clip;value.start_us=value.last_us=now;
    value.startup_deadline=now+500000;value.state=EldenRtpStarting;value.active=clip->mask;*s=value;
    // Retain every selected chip before even the first preparation STOP: a
    // failing STOP may still leave physical motion; its callback must retry.
    for(unsigned m=0;m<2;m++)if(s->active&(1u<<m))s->motor[m].chip=chips[m];
    // Prepare both chips before either GO; preserve simultaneous effect timing.
    for(unsigned m=0;m<2;m++)if(s->active&(1u<<m)){
        rc=elden_aw_rtp_prepare(&s->motor[m],chips[m],24000,clip->gain[m],duration+500000);
        if(rc)return stop_all(s,rc);
    }
    for(unsigned m=0;m<2;m++)if(s->active&(1u<<m)){
        rc=elden_aw_rtp_go(&s->motor[m]);if(rc)return stop_all(s,rc);
    }
    s->last_us=s->io.now_us(s->io.context);s->startup_deadline=s->last_us+250000;return 0;
}
int elden_rtp_stream_step(EldenRtpStream *s){
    if(!s||s->state==EldenRtpIdle||s->state==EldenRtpHeld)return -22;
    if(s->state==EldenRtpComplete)return 1;
    if(s->state==EldenRtpFailed||s->state==EldenRtpCancelled)return s->error?s->error:-125;
    int rc=s->io.admit(s->io.context,s->clip.id,0);if(rc)return stop_all(s,rc);
    uint64_t now=s->io.now_us(s->io.context);
    if(now<s->last_us||now-s->last_us>s->limits.max_service_gap_us)return stop_all(s,-110);s->last_us=now;
    unsigned wrote=0,blocked=0;
    // At most4096 payload bytes per call; alternate chips after every burst.
    while(s->active&&wrote<4096&&blocked<2){
        unsigned m=s->turn++&1u;if(!(s->active&(1u<<m))){blocked++;continue;}
        EldenAwRtpStatus status;rc=elden_aw_rtp_status(&s->motor[m],&status);if(rc)return stop_all(s,rc);
        if(status.state==8)s->seen|=(uint8_t)(1u<<m);
        else if(status.state==0){
            if(s->sent[m]==s->clip.frames&&status.done){rc=elden_aw_rtp_stop(&s->motor[m]);if(rc)return stop_all(s,rc);s->active&=(uint8_t)~(1u<<m);blocked=0;continue;}
            if(s->seen&(1u<<m))return stop_all(s,-61); // real early standby: underrun
            if(now>=s->startup_deadline)return stop_all(s,-110);blocked++;continue;
        }else{if(now>=s->startup_deadline)return stop_all(s,-110);blocked++;continue;}
        if(status.almost_full||s->sent[m]==s->clip.frames){blocked++;continue;}
        size_t bytes=s->clip.frames-s->sent[m];if(bytes>64)bytes=64;
        rc=elden_aw_rtp_feed(&s->motor[m],s->clip.pcm[m]+s->sent[m],bytes);
        if(rc==EldenAwBusy){blocked++;continue;}if(rc)return stop_all(s,rc);
        s->sent[m]+=bytes;wrote+=(unsigned)bytes;blocked=0;
        now=s->io.now_us(s->io.context);
        if(now<s->last_us||now-s->last_us>s->limits.max_service_gap_us)return stop_all(s,-110);
        s->last_us=now;
        rc=s->io.admit(s->io.context,s->clip.id,0);if(rc)return stop_all(s,rc);
    }
    if(!s->active){s->state=EldenRtpComplete;return 1;}
    if(s->seen==s->clip.mask)s->state=EldenRtpPlaying;return 0;
}
int elden_rtp_stream_stop(EldenRtpStream *s){
    if(!s||s->state==EldenRtpIdle)return -22;
    // The retained chip IO admission supplies the original bounded cleanup.
    int rc=s->io.admit(s->io.context,s->clip.id,1);if(rc){s->state=EldenRtpHeld;s->error=rc;return rc;}
    return stop_all(s,0);
}
