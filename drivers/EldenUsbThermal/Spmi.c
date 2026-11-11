/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Qualcomm v7.2 observer/write-channel ABI; same verified controller as the
 * Elden SDAM recovery route, a separate ADC peripheral and separate lease. */
#include "Spmi.h"
#include "EldenAdcResources.h"
static uint32_t rd(EldenAdcSpmi *s,enum EldenAdcSpmiSpace w,uint32_t r){return s->io.read32(s->io.context,w,r);}
static void wr(EldenAdcSpmi *s,enum EldenAdcSpmiSpace w,uint32_t r,uint32_t v){s->io.write32(s->io.context,w,r,v);}
static unsigned ppid(const EldenAdcSpmi *s){
    if(s->target==EldenSpmiDcLimit)return ELDEN_DC_PMIC_SID<<8|ELDEN_DC_LIMIT_BASE>>8;
    if(s->target==EldenSpmiDcPath)return ELDEN_DC_PMIC_SID<<8|ELDEN_DC_PATH_BASE>>8;
    if(s->target==EldenSpmiDcUsb)return ELDEN_DC_PMIC_SID<<8|ELDEN_DC_USB_BASE>>8;
    return s->target==EldenSpmiUsb2Gpio?(ELDEN_THERMAL_GPIO2_SID<<8|ELDEN_THERMAL_GPIO2_BASE>>8):
        (ELDEN_ADC_SDAM_SID<<8|ELDEN_ADC_SDAM_BASE>>8);
}
int elden_adc_spmi_find(EldenAdcSpmi *s,bool writable){
    if(!s||!s->io.read32||!s->io.write32||!s->io.delay_us||s->selected||s->uncertain)return EldenAdcArgument;
    if(s->target<EldenSpmiAdcSdam||s->target>EldenSpmiDcUsb)return EldenAdcArgument;
    if(writable&&(s->target==EldenSpmiDcPath||s->target==EldenSpmiDcUsb))return EldenAdcArgument;
    if(rd(s,EldenAdcSpmiCore,0)!=ELDEN_ADC_SPMI_VERSION||
       (rd(s,EldenAdcSpmiCore,4)&0x7ff)!=ELDEN_ADC_SPMI_APIDS)return EldenAdcFault;
    s->matches=s->owned_matches=0;
    unsigned chosen=0,owner=0,any=0,any_owner=0;
    for(unsigned i=0;i<ELDEN_ADC_SPMI_APIDS;i++){
        uint32_t row=rd(s,EldenAdcSpmiCore,0x2000+4*i);
        if(!row||((row>>8)&0xfff)!=ppid(s))continue;
        unsigned ee=rd(s,EldenAdcSpmiOwner,4*i)&7;
        ++s->matches;any=i;any_owner=ee;
        if(ee==ELDEN_ADC_SPMI_EE){++s->owned_matches;chosen=i;owner=ee;}
    }
    if(s->owned_matches==1){s->apid=chosen;s->write_owner=owner;}
    else if(!writable&&s->matches==1){s->apid=any;s->write_owner=any_owner;}
    else return EldenAdcBusy;
    s->selected=true;s->writable=writable;return 0;
}
static bool live(EldenAdcSpmi *s){
    return s->selected&&!s->uncertain&&
        rd(s,EldenAdcSpmiCore,0)==ELDEN_ADC_SPMI_VERSION&&
        ((rd(s,EldenAdcSpmiCore,0x2000+4*s->apid)>>8)&0xfff)==ppid(s)&&
        (rd(s,EldenAdcSpmiOwner,4*s->apid)&7)==s->write_owner;
}
static int done(EldenAdcSpmi *s,enum EldenAdcSpmiSpace space){
    for(unsigned i=0;i<10000;i++){
        s->last_status=rd(s,space,8);
        if(s->last_status&1){
            if(s->last_status&0xe){if(space==EldenAdcSpmiWriter)s->uncertain=true;return EldenAdcIoError;}
            return 0;
        }
        s->io.delay_us(s->io.context,1);
    }
    s->uncertain=true;return EldenAdcTimeout;
}
int elden_adc_spmi_read(void *p,uint8_t offset,uint8_t *bytes,size_t count){
    EldenAdcSpmi *s=p;
    if(!s||!bytes||!count||count>8||(unsigned)offset+count>256)return EldenAdcArgument;
    if(!live(s))return EldenAdcBusy;
    bool allowed=((offset==EldenAdcHs||offset==EldenAdcStatus||offset==EldenAdcEoc||offset==EldenAdcRequest||offset==0x6e)&&count==1)||
        (offset==EldenAdcConfig&&count==7)||(offset==EldenAdcData&&count==2)||(offset==0x48&&count==2);
    if(s->target==EldenSpmiUsb2Gpio)allowed=count==1&&(offset==4||offset==5||offset==8||
        (offset>=0x40&&offset<=0x46)||offset==0x4a);
    if(s->target==EldenSpmiDcLimit)allowed=count==1&&offset==0x52;
    if(s->target==EldenSpmiDcPath)allowed=count==1&&offset==0x0b;
    if(s->target==EldenSpmiDcUsb)allowed=count==1&&(offset==0x52||offset==0x54);
    if(!allowed)return EldenAdcArgument;
    wr(s,EldenAdcSpmiObserver,0,(1u<<27)|((uint32_t)offset<<4)|(uint32_t)(count-1));++s->commands;
    int rc=done(s,EldenAdcSpmiObserver);if(rc)return rc;
    uint32_t lo=rd(s,EldenAdcSpmiObserver,0x18),hi=count>4?rd(s,EldenAdcSpmiObserver,0x1c):0;
    for(unsigned i=0;i<count;i++)bytes[i]=(uint8_t)((i<4?lo:hi)>>(8*(i&3)));
    return 0;
}
int elden_adc_spmi_write(void *p,uint8_t offset,const uint8_t *bytes,size_t count){
    EldenAdcSpmi *s=p;
    if(!s||!bytes||!count||count>8||(unsigned)offset+count>256)return EldenAdcArgument;
    if(!live(s)||!s->writable||s->write_owner!=ELDEN_ADC_SPMI_EE)return EldenAdcBusy;
    bool allowed=(offset==EldenAdcConfig&&count==7)||
        (count==1&&((offset==EldenAdcEocClear&&bytes[0]==1)||(offset==EldenAdcRequest&&bytes[0]==1)||(offset==EldenAdcChannel&&bytes[0]==0)));
    if(s->target==EldenSpmiUsb2Gpio)allowed=count==1&&(offset==0x40||offset==0x44);
    if(s->target==EldenSpmiDcLimit)allowed=count==1&&offset==0x52&&(bytes[0]==9||bytes[0]==10);
    if(s->target==EldenSpmiDcPath||s->target==EldenSpmiDcUsb)allowed=false;
    if(!allowed)return EldenAdcArgument;
    uint32_t lo=0,hi=0;
    for(unsigned i=0;i<count;i++){if(i<4)lo|=(uint32_t)bytes[i]<<(8*i);else hi|=(uint32_t)bytes[i]<<(8*(i-4));}
    wr(s,EldenAdcSpmiWriter,0x10,lo);if(count>4)wr(s,EldenAdcSpmiWriter,0x14,hi);
    wr(s,EldenAdcSpmiWriter,0,((uint32_t)offset<<4)|(uint32_t)(count-1));++s->commands;
    return done(s,EldenAdcSpmiWriter);
}
