/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Pins.h"
#include <string.h>
enum {BadArgument=-22,BadConfiguration=-19,IoError=-5};
static int pmic_snapshot(EldenThermalPins *p,EldenThermalPinView *v){
    const uint8_t addresses[]={4,5,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x4a,8};
    uint8_t *fields[]={&v->type,&v->subtype,&v->mode,&v->vin,&v->pull,&v->input,&v->source,&v->drive,&v->enable,&v->atest,&v->status};
    for(unsigned i=0;i<sizeof(addresses);i++)if(p->io.pmic_read(p->io.context,addresses[i],fields[i]))return IoError;
    if(v->type!=0x10||v->subtype<0x10||v->subtype>0x15||(v->mode&3)>2||
       (v->source&15)||(v->drive&0x30)||!(v->enable&0x80))return BadConfiguration;
    v->level[1]=(v->mode&3)==1?((v->source>>7)&1):(v->status&1);
    return 0;
}
static int snapshot(EldenThermalPins *p,EldenThermalPinView *v){
    memset(v,0,sizeof(*v));
    v->tlmm_config=p->io.tlmm_read(p->io.context,0);v->tlmm_io=p->io.tlmm_read(p->io.context,4);
    if(v->tlmm_config==0xffffffffu||v->tlmm_io==0xffffffffu||(v->tlmm_config&0x3c)||!(v->tlmm_config&(1u<<9)))return BadConfiguration;
    v->level[0]=(v->tlmm_io>>1)&1;return pmic_snapshot(p,v);
}
static bool same_pmic(const EldenThermalPinView *v,const EldenThermalPinView *e){
    return v->type==e->type&&v->subtype==e->subtype&&v->mode==e->mode&&v->vin==e->vin&&
        v->pull==e->pull&&v->input==e->input&&v->source==e->source&&v->drive==e->drive&&
        v->enable==e->enable&&v->atest==e->atest;
}
int elden_thermal_pins_probe(EldenThermalPins *p,EldenThermalPinView *out){
    if(!p||!out||!p->io.tlmm_read||!p->io.tlmm_write||!p->io.pmic_read||!p->io.pmic_write||p->ready)return BadArgument;
    EldenThermalPinView v;int rc=snapshot(p,&v);if(rc)return rc;
    p->expected=v;p->ready=true;*out=v;return 0;
}
int elden_thermal_pins_refresh(EldenThermalPins *p,EldenThermalPinView *out){
    if(!p||!out||!p->ready)return BadArgument;
    EldenThermalPinView v;int rc=snapshot(p,&v);if(rc)return rc;
    const EldenThermalPinView *e=&p->expected;
    if(v.tlmm_config!=e->tlmm_config||!same_pmic(&v,e))return BadConfiguration;
    *out=v;return 0;
}
static int fail(EldenThermalPins *p,unsigned i,int error){p->failed[i]=true;p->error[i]=error;return error;}
int elden_thermal_pin_set(EldenThermalPins *p,unsigned port,int level){
    if(!p||!p->ready||port<1||port>2||(level!=0&&level!=1))return BadArgument;
    unsigned i=port-1;if(p->failed[i])return p->error[i];
    /* The two paths are independent: a failed SPMI transaction must not
     * prevent closing the healthy TLMM path on an emergency. */
    if(port==1){
        uint32_t cfg=p->io.tlmm_read(p->io.context,0),io=p->io.tlmm_read(p->io.context,4);
        if(cfg!=p->expected.tlmm_config||io==0xffffffffu)return fail(p,i,BadConfiguration);
        if(((io>>1)&1)==(unsigned)level)return 0;
        uint32_t wanted=(io&~2u)|((uint32_t)level<<1);
        if(p->io.tlmm_write(p->io.context,4,wanted))return fail(p,i,IoError);
        ++p->writes[i];
        if((p->io.tlmm_read(p->io.context,4)&2)!=(wanted&2)||p->io.tlmm_read(p->io.context,0)!=cfg)return fail(p,i,IoError);
        p->expected.tlmm_io=wanted;p->expected.level[0]=level;return 0;
    }
    EldenThermalPinView v={0};int rc=pmic_snapshot(p,&v);if(rc)return fail(p,i,rc);
    if(!same_pmic(&v,&p->expected))return fail(p,i,BadConfiguration);
    if(v.level[1]==level)return 0;
    uint8_t desired=(uint8_t)((v.source&0x7f)|(level?0x80:0)),mode=(uint8_t)((v.mode&~3u)|1u);
    if(desired!=v.source){if(p->io.pmic_write(p->io.context,0x44,desired))return fail(p,i,IoError);++p->writes[i];}
    if(mode!=v.mode){if(p->io.pmic_write(p->io.context,0x40,mode))return fail(p,i,IoError);++p->writes[i];}
    p->expected.source=desired;p->expected.mode=mode;
    rc=pmic_snapshot(p,&v);
    if(rc||!same_pmic(&v,&p->expected)||v.level[1]!=level)return fail(p,i,rc?rc:IoError);
    p->expected.level[1]=level;return 0;
}
