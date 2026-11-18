/* SPDX-License-Identifier: GPL-2.0-only */
#include "AwRtp.h"
static const uint8_t aw_rtp_regs[]={0x06,0x07,0x08,0x46,0x2f,0x30,0x31};
static int aw_rtp_fail(EldenAwRtp *r,int rc){r->held=1;r->chip->held=1;r->chip->ready=0;return rc;}
static int aw_rtp_write_read(EldenAwRtp *r,unsigned reg,unsigned value){
    int rc=aw_reg_write(r->chip,reg,value);if(rc)return rc;unsigned actual;
    rc=aw_reg_read(r->chip,reg,&actual);return rc?rc:actual==value?0:EldenAwIntegrity;
}
static int aw_rtp_clock(EldenAwRtp *r){
    uint64_t now=r->chip->io.now_us(r->chip->io.context);
    if(now<r->last_clock||now>=r->deadline)return EldenAwTimeout;r->last_clock=now;return 0;
}
int elden_aw_rtp_prepare(EldenAwRtp *r,EldenAwChip *c,uint32_t hz,uint8_t gain,uint32_t duration){
    if(!r||!c||r->owned||r->held||!c->ready||c->held||c->busy||!gain||gain>c->max_gain||
       !duration||duration>61000000||(hz!=12000&&hz!=24000&&hz!=48000))return EldenAwArgument;
    // The proved SRAM base also defines this chip's RTP FIFO capacity.
    if(c->base!=0x800)return EldenAwIntegrity;
    uint64_t now=c->io.now_us(c->io.context);if(now>UINT64_MAX-250000)return EldenAwArgument;
    int rc=c->io.admit(c->io.context,c->address,EldenAwPlay);if(rc)return rc;
    rc=elden_aw_stop(c);if(rc)return rc;
    EldenAwRtp value={0};value.chip=c;value.rate_hz=hz;value.duration_us=duration;value.last_clock=now;value.deadline=now+250000;*r=value;
    unsigned current;
    for(unsigned i=0;i<7;i++){rc=aw_reg_read(c,aw_rtp_regs[i],&current);if(rc)return rc;r->saved[i]=(uint8_t)current;}
    unsigned base_hi,base_lo,state;
    if((rc=aw_reg_read(c,0x2d,&base_hi))||(rc=aw_reg_read(c,0x2e,&base_lo))||(rc=aw_reg_read(c,0x3f,&state)))return rc;
    if((((base_hi&31)<<8)|base_lo)!=c->base||(state&15))return EldenAwIntegrity;
    r->owned=1;
    unsigned ae=c->base/2,af=c->base-c->base/4;
    const uint8_t values[]={
        (uint8_t)((r->saved[0]&~0x20u)|0x20u),gain,
        (uint8_t)((r->saved[2]&~7u)|1u),
        (uint8_t)((r->saved[3]&~0x60u)|(hz==12000?0x40u:hz==48000?0x20u:0)),
        (uint8_t)(((ae>>8)<<4)|(af>>8)),(uint8_t)ae,(uint8_t)af};
    for(unsigned i=0;i<7;i++){
        r->changed|=(uint8_t)(1u<<i);
        rc=aw_rtp_write_read(r,aw_rtp_regs[i],values[i]);if(rc)goto failed;
    }
    if((rc=aw_rtp_clock(r)))goto failed;
    c->busy=1;return 0; // reserve chip; no GO before both motor preparations
failed:
    (void)elden_aw_stop(c);return aw_rtp_fail(r,rc);
}
int elden_aw_rtp_go(EldenAwRtp *r){
    if(!r||!r->owned||r->held||r->started)return EldenAwArgument;
    int rc=aw_rtp_clock(r);if(rc)return rc;EldenAwChip *c=r->chip;
    // SYSINT is read-clear. Discard only the previous stopped run's DONE;
    // reject a real fault rather than carrying a stale completion into GO.
    unsigned irq;rc=aw_reg_read(c,2,&irq);if(rc)return aw_rtp_fail(r,rc);
    if(irq&0xe6)return aw_rtp_fail(r,EldenAwCancelled);r->done_seen=0;
    uint64_t now=c->io.now_us(c->io.context);if(now>UINT64_MAX-r->duration_us)return EldenAwArgument;
    r->last_clock=now;r->deadline=now+r->duration_us;
    r->started=1;c->busy=1;c->deadline=r->deadline;c->last_clock=r->last_clock;
    rc=aw_reg_write(c,9,1);return rc?aw_rtp_fail(r,rc):0;
}
int elden_aw_rtp_start(EldenAwRtp *r,EldenAwChip *c,uint32_t hz,uint8_t gain,uint32_t duration){
    int rc=elden_aw_rtp_prepare(r,c,hz,gain,duration);return rc?rc:elden_aw_rtp_go(r);
}
int elden_aw_rtp_status(EldenAwRtp *r,EldenAwRtpStatus *out){
    if(!r||!out||!r->owned||r->held)return EldenAwCancelled;
    int rc=aw_rtp_clock(r);if(rc)return rc;unsigned sysst,irq,state;
    if((rc=aw_reg_read(r->chip,1,&sysst))||(rc=aw_reg_read(r->chip,2,&irq))||(rc=aw_reg_read(r->chip,0x3f,&state)))return aw_rtp_fail(r,rc);
    if(irq&1)r->done_seen=1;
    EldenAwRtpStatus value={(uint8_t)((sysst>>3)&1),(uint8_t)((sysst>>4)&1),(uint8_t)(state&15),(uint8_t)(irq&0xe6),r->done_seen};
    if(value.faults)return aw_rtp_fail(r,EldenAwCancelled);*out=value;return 0;
}
int elden_aw_rtp_feed(EldenAwRtp *r,const int8_t *data,size_t n){
    if(!data||!n||n>64||!r||!r->owned||!r->started||r->held)return EldenAwArgument;
    EldenAwRtpStatus s;int rc=elden_aw_rtp_status(r,&s);if(rc)return rc;
    if(s.state!=8)return EldenAwCancelled;if(s.almost_full)return EldenAwBusy;
    rc=aw_admitted(r->chip);if(!rc)rc=r->chip->io.write(r->chip->io.context,r->chip->address,0x32,(const uint8_t *)data,n);
    return rc?aw_rtp_fail(r,rc):0;
}
int elden_aw_rtp_stop(EldenAwRtp *r){
    if(!r||!r->chip)return EldenAwArgument;
    EldenAwChip *c=r->chip;int rc=elden_aw_stop(c);if(rc)return aw_rtp_fail(r,rc);
    if(r->held)return EldenAwCancelled; // no config overwrite after uncertain IO
    c->cleanup=1;
    for(unsigned i=0;i<7;i++)if(r->changed&(1u<<i)){
        rc=aw_rtp_write_read(r,aw_rtp_regs[i],r->saved[i]);if(rc){c->cleanup=0;return aw_rtp_fail(r,rc);}
    }
    c->cleanup=0;r->owned=0;r->started=0;r->changed=0;return 0;
}
