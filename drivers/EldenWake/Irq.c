/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SM8750/PDC policy from exact6f97250e pinctrl-sm8750/pinctrl-msm/qcom-pdc.
 * The bounded ownership/readback/refusal adapter is Elden-specific. */
#include "Irq.h"
enum {TlmmCtl=0,TlmmCfg=8,PdcVersion=0x1000,PdcCfg=0x110+4*109,PdcBank=0x10+4*(109/32),PdcBit=1u<<(109%32),
    PdcTypeMask=7,PdcFalling=2,PdcEnable=8,TlmmEnable=1,TlmmWakePresent=64,TlmmWakeEnable=128};
static int held(EldenWakeIrq *s,int rc){
    s->error=rc;__atomic_store_n(&s->state,EldenWakeRouteHeld,__ATOMIC_RELEASE);
    // Best-effort isolation may mask only our exclusive parent. A failed
    // cleanup is never converted to release/readiness; maps stay retained.
    if(s->attached)(void)s->io.mask(s->io.context,EldenWakeIntid,1);return rc;
}
static int read(EldenWakeIrq *s,unsigned space,unsigned reg,uint32_t *out){return s->io.read(s->io.context,space,reg,out);}
static int write_check(EldenWakeIrq *s,unsigned space,unsigned reg,uint32_t value,uint32_t mask){
    int rc=s->io.write(s->io.context,space,reg,value);if(rc)return rc;uint32_t seen;
    rc=read(s,space,reg,&seen);return rc?rc:(seen&mask)!=(value&mask)?-71:0;
}
static int version_valid(uint32_t v){return v!=UINT32_MAX&&!(v&0xff000000u)&&(v&0xff0000u);}
static int old_enable(const EldenWakeIrq *s){return s->version<0x30200;}
static int gate(EldenWakeIrq *s,uint64_t owner,uint64_t epoch,int cleanup){
    if(!s||!s->owner||s->owner!=owner||s->epoch!=epoch)return -116;
    uint32_t state=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE);
    if(state==EldenWakeRouteHeld)return s->error?s->error:-125;
    if(state==EldenWakeRouteEmpty||state==EldenWakeRouteStopped)return -125;
    return s->io.admit(s->io.context,owner,epoch,cleanup);
}
static int identity(EldenWakeIrq *s){
    uint32_t v,ctl,tlmm,cfg,bank=0;int rc=read(s,EldenWakePdc,PdcVersion,&v);if(rc)return rc;
    if(v!=s->version)return -116;
    if((rc=read(s,EldenWakeTlmm,TlmmCtl,&ctl))||(rc=read(s,EldenWakeTlmm,TlmmCfg,&tlmm))||
       (rc=read(s,EldenWakePdc,PdcCfg,&cfg)))return rc;
    // Another consumer's bit/target/trigger/mux change refuses. Only our
    // conditional wake-forward bit and PDC type/enable may differ.
    uint32_t wake=s->tlmm_cfg&TlmmWakePresent?TlmmWakeEnable:0;
    if(ctl!=s->pin_ctl||(tlmm&~wake)!=(s->tlmm_cfg&~wake)||
       (cfg&~(PdcTypeMask|(old_enable(s)?0:PdcEnable)))!=(s->pdc_cfg&~(PdcTypeMask|(old_enable(s)?0:PdcEnable))))return -116;
    if(old_enable(s)&&(rc=read(s,EldenWakePdc,PdcBank,&bank)))return rc;
    return 0;
}
static int enable(EldenWakeIrq *s,int on){
    uint32_t value;unsigned reg=old_enable(s)?PdcBank:PdcCfg,bit=old_enable(s)?PdcBit:PdcEnable;
    int rc=read(s,EldenWakePdc,reg,&value);if(rc)return rc;
    s->changed_pdc=1;
    return write_check(s,EldenWakePdc,reg,on?value|bit:value&~bit,bit);
}
int elden_wake_irq_acquire(EldenWakeIrq *s,const EldenWakeIrqIo *io,uint64_t owner,uint64_t epoch){
    if(!s||s->state||s->owner||!io||!io->admit||!io->read||!io->write||!io->attach||!io->mask||!io->drain||!owner||!epoch)return -22;
    int rc=io->admit(io->context,owner,epoch,0);if(rc)return rc;
    EldenWakeIrq v={0};v.io=*io;v.owner=owner;v.epoch=epoch;
    if((rc=read(&v,EldenWakePdc,PdcVersion,&v.version))||(rc=read(&v,EldenWakePdc,PdcCfg,&v.pdc_cfg))||
       (rc=read(&v,EldenWakeTlmm,TlmmCtl,&v.pin_ctl))||(rc=read(&v,EldenWakeTlmm,TlmmCfg,&v.tlmm_cfg)))return rc;
    if(!version_valid(v.version)||v.pdc_cfg==UINT32_MAX||v.pin_ctl==UINT32_MAX||v.tlmm_cfg==UINT32_MAX)return -95;
    if((v.pin_ctl&0x23cu)||(v.tlmm_cfg&TlmmEnable))return -16; // caller has not supplied a GPIO-input/masked route
    if(old_enable(&v)){
        if((rc=read(&v,EldenWakePdc,PdcBank,&v.pdc_bank)))return rc;
        if(v.pdc_bank==UINT32_MAX)return -95;if(v.pdc_bank&PdcBit)return -16;
    }else if(v.pdc_cfg&PdcEnable)return -16;
    v.state=EldenWakeRouteOwned;*s=v;
    // Attach is physical and may leave a retained registration on failure.
    s->attached=1;rc=s->io.attach(s->io.context,EldenWakeIntid,1);
    if(rc)return held(s,rc);
    if((rc=s->io.mask(s->io.context,EldenWakeIntid,1)))return held(s,rc);
    if((rc=identity(s)))return held(s,rc);
    return 0;
}
int elden_wake_irq_arm(EldenWakeIrq *s,uint64_t owner,uint64_t epoch){
    int rc=gate(s,owner,epoch,0);if(rc)return rc;
    if(s->state!=EldenWakeRouteOwned)return -16;
    if((rc=identity(s))||(rc=s->io.mask(s->io.context,EldenWakeIntid,1)))return held(s,rc);
    uint32_t cfg;if((rc=read(s,EldenWakePdc,PdcCfg,&cfg)))return held(s,rc);
    s->changed_pdc=1;
    if((rc=write_check(s,EldenWakePdc,PdcCfg,(cfg&~PdcTypeMask)|PdcFalling,PdcTypeMask)))return held(s,rc);
    // qcom_pdc_gic_set_type clears the owned GIC pending bit after a type
    // change, while masked. Never clear the shared TLMM summary or siblings.
    if((rc=s->io.drain(s->io.context,EldenWakeIntid)))return held(s,rc);
    if(s->tlmm_cfg&TlmmWakePresent){
        uint32_t tlmm;if((rc=read(s,EldenWakeTlmm,TlmmCfg,&tlmm)))return held(s,rc);
        s->changed_tlmm=1;
        if((rc=write_check(s,EldenWakeTlmm,TlmmCfg,tlmm|TlmmWakeEnable,TlmmWakeEnable)))return held(s,rc);
    }
    if((rc=enable(s,1)))return held(s,rc);
    __atomic_store_n(&s->state,EldenWakeRouteArmed,__ATOMIC_RELEASE);
    if((rc=s->io.mask(s->io.context,EldenWakeIntid,0)))return held(s,rc);
    return 0;
}
int elden_wake_irq_disarm(EldenWakeIrq *s,uint64_t owner,uint64_t epoch){
    int rc=gate(s,owner,epoch,1);if(rc)return rc;
    if((rc=s->io.mask(s->io.context,EldenWakeIntid,1)))return held(s,rc);
    if((rc=identity(s)))return held(s,rc);
    if(s->changed_pdc&&(rc=enable(s,0)))return held(s,rc);
    if((rc=s->io.drain(s->io.context,EldenWakeIntid)))return held(s,rc);
    __atomic_store_n(&s->state,EldenWakeRouteOwned,__ATOMIC_RELEASE);return 0;
}
int elden_wake_irq_stop(EldenWakeIrq *s,uint64_t owner,uint64_t epoch){
    int rc=gate(s,owner,epoch,1);if(rc)return rc;
    if((rc=elden_wake_irq_disarm(s,owner,epoch)))return rc;
    uint32_t value;
    if(s->changed_tlmm){
        if((rc=read(s,EldenWakeTlmm,TlmmCfg,&value)))return held(s,rc);
        value=(value&~TlmmWakeEnable)|(s->tlmm_cfg&TlmmWakeEnable);
        if((rc=write_check(s,EldenWakeTlmm,TlmmCfg,value,TlmmWakeEnable)))return held(s,rc);
    }
    if(s->changed_pdc){
        if((rc=read(s,EldenWakePdc,PdcCfg,&value)))return held(s,rc);
        unsigned mask=PdcTypeMask|(old_enable(s)?0:PdcEnable);
        value=(value&~mask)|(s->pdc_cfg&mask);
        if((rc=write_check(s,EldenWakePdc,PdcCfg,value,mask)))return held(s,rc);
        // A legacy bank word may have new live sibling bits. Restore only109.
        if(old_enable(s)){
            if((rc=read(s,EldenWakePdc,PdcBank,&value)))return held(s,rc);
            value=(value&~PdcBit)|(s->pdc_bank&PdcBit);
            if((rc=write_check(s,EldenWakePdc,PdcBank,value,PdcBit)))return held(s,rc);
        }
    }
    __atomic_store_n(&s->state,EldenWakeRouteStopped,__ATOMIC_RELEASE);return 0;
}
