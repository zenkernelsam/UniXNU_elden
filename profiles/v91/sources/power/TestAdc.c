/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Adc.h"
#include "Policy.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
typedef struct {
    uint8_t reg[256];uint64_t time;
    unsigned phase,polls,writes,requests,conversions,fail_write;
    bool stuck,freeze_time,bad_echo,fault;
    uint16_t code1,code2;uint8_t last_config[7];
} Device;
static int read(void *p,uint8_t offset,uint8_t *out,size_t count){
    Device *d=p;CHECK((unsigned)offset+count<=256);
    if(d->phase==1&&offset==EldenAdcEoc&&!d->stuck&&++d->polls==2){
        d->reg[EldenAdcEoc]|=1;d->reg[EldenAdcStatus]|=d->fault?0x80:1;
        d->reg[EldenAdcHs]=1;d->reg[EldenAdcRequest]=0;
        uint16_t code=d->reg[EldenAdcConfig]==7?d->code1:d->code2;
        d->reg[EldenAdcData]=code&255;d->reg[EldenAdcData+1]=code>>8;d->phase=0;
    }
    if(d->phase==2&&offset==EldenAdcHs&&!d->stuck){
        d->reg[EldenAdcEoc]&=~1u;d->reg[EldenAdcStatus]&=~1u;
        d->reg[EldenAdcHs]=1;d->reg[EldenAdcRequest]=0;d->phase=0;
    }
    memcpy(out,d->reg+offset,count);
    if(d->bad_echo&&d->writes&&offset==EldenAdcConfig)out[3]^=4;
    return 0;
}
static int write(void *p,uint8_t offset,const uint8_t *bytes,size_t count){
    Device *d=p;CHECK((offset==EldenAdcConfig&&count==7)||((offset==EldenAdcEocClear||offset==EldenAdcChannel||offset==EldenAdcRequest)&&count==1));
    if(++d->writes==d->fail_write)return -5;
    memcpy(d->reg+offset,bytes,count);
    if(offset==EldenAdcConfig)memcpy(d->last_config,bytes,7);
    if(offset==EldenAdcRequest){
        CHECK(bytes[0]==1);++d->requests;d->reg[EldenAdcHs]=0x80;
        if(d->reg[EldenAdcChannel]&0x80){
            CHECK(d->reg[EldenAdcChannel]==0x80);d->phase=1;d->polls=0;++d->conversions;
        }else{CHECK(d->reg[EldenAdcChannel]==0);d->phase=2;}
    }
    return 0;
}
static uint64_t now(void *p){return ((Device *)p)->time;}
static void delay(void *p,unsigned us){Device *d=p;if(!d->freeze_time)d->time+=us;}
static EldenAdc setup(Device *d){
    memset(d,0,sizeof(*d));d->reg[EldenAdcHs]=1;d->reg[EldenAdcStatus]=0x40;
    d->reg[EldenAdcEoc]=0x20;d->reg[0x48]=0x42;d->reg[0x49]=4;d->reg[0x6e]=0x80;
    const uint8_t cfg[]={7,0,1,0x14,0x80,0x5e,2};memcpy(d->reg+EldenAdcConfig,cfg,7);
    d->code1=0x7aaf;d->code2=0x1ea1;
    EldenAdc a={.io={d,read,write,now,delay}};return a;
}
static void normal(void){
    Device d;EldenAdc a=setup(&d);uint8_t before[7];memcpy(before,d.reg+EldenAdcConfig,7);
    EldenAdcView view;CHECK(!elden_adc_inspect(&a,&view)&&!d.writes);
    CHECK(!elden_adc_acquire(&a)&&!d.writes);
    uint16_t code;int mc;
    CHECK(!elden_adc_sample(&a,1,&code,&mc)&&code==0x7aaf&&mc==27415);
    const uint8_t one[]={7,0x80,1,0x14,0x80,0x47,0};CHECK(!memcmp(d.last_config,one,7));
    CHECK(!elden_adc_sample(&a,2,&code,&mc)&&code==0x1ea1&&mc==27473);
    const uint8_t two[]={1,0x80,1,0x18,0x80,0x47,2};CHECK(!memcmp(d.last_config,two,7));
    CHECK(a.samples==2&&d.requests==4&&d.conversions==2);
    CHECK(!elden_adc_release(&a)&&!memcmp(before,d.reg+EldenAdcConfig,7)&&d.requests==4);
    CHECK(d.reg[EldenAdcEoc]==0x20&&d.reg[0x48]==0x42&&d.reg[0x49]==4&&d.reg[0x6e]==0x80);
}
static void failures(void){
    for(unsigned failure=0;failure<7;failure++){
        Device d;EldenAdc a=setup(&d);CHECK(!elden_adc_acquire(&a));
        uint16_t code=0x55aa;int mc=123456;
        if(failure==0)d.stuck=true;
        if(failure==1)d.freeze_time=d.stuck=true;
        if(failure==2)d.bad_echo=true;
        if(failure==3)d.fault=true;
        if(failure==4)d.fail_write=2;
        if(failure==5)d.fail_write=5;
        if(failure==6)d.code1=65535;
        CHECK(elden_adc_sample(&a,1,&code,&mc)!=0&&code==0x55aa&&mc==123456);
        CHECK(a.failed);unsigned writes=d.writes;
        if(failure!=6){CHECK(a.quarantined);CHECK(elden_adc_release(&a)!=0&&d.writes==writes);}
        else{CHECK(!a.quarantined&&!a.inflight);CHECK(!elden_adc_release(&a));}
    }
    for(unsigned failure=0;failure<3;failure++){
        Device d;EldenAdc a=setup(&d);
        if(failure==0)d.reg[EldenAdcHs]=0x80;
        if(failure==1)d.reg[EldenAdcEoc]|=1;
        if(failure==2)d.reg[EldenAdcStatus]|=0x80;
        CHECK(elden_adc_acquire(&a)!=0&&!d.writes&&!a.quarantined);
    }
}
static void scale_and_policy(void){
    int mc=9;CHECK(!elden_adc_therm_scale(1,0x7ab4,&mc)&&mc==27409);
    CHECK(!elden_adc_therm_scale(1,0x8000,&mc)); /* valid Gen4, invalid Gen3 */
    CHECK(elden_adc_therm_scale(2,0x8000,&mc)==EldenAdcRange);
    CHECK(elden_adc_therm_scale(1,65535,&mc)==EldenAdcRange);
    CHECK(elden_adc_therm_scale(1,65534,&mc)==EldenAdcRange); /* resistance cannot narrow safely */
    for(unsigned port=1;port<=2;port++){
        CHECK(elden_usb_thermal_decision(port,true,44999,false,INT_MIN)==EldenThermalLow);
        CHECK(elden_usb_thermal_decision(port,true,49900,true,4000)==EldenThermalLow);
        CHECK(elden_usb_thermal_decision(port,true,50000,true,4000)==EldenThermalKeep);
        CHECK(elden_usb_thermal_decision(port,true,65000,false,0)==EldenThermalKeep);
        CHECK(elden_usb_thermal_decision(port,false,90000,true,3000)==EldenThermalKeep);
    }
    CHECK(elden_usb_thermal_decision(1,true,75099,false,0)==EldenThermalKeep);
    CHECK(elden_usb_thermal_decision(1,true,75100,false,0)==EldenThermalHigh);
    CHECK(elden_usb_thermal_decision(1,true,65100,true,3500)==EldenThermalHigh);
    CHECK(elden_usb_thermal_decision(1,true,65100,true,3510)==EldenThermalKeep);
    CHECK(elden_usb_thermal_decision(2,true,70099,false,0)==EldenThermalKeep);
    CHECK(elden_usb_thermal_decision(2,true,70100,false,0)==EldenThermalHigh);
    CHECK(elden_usb_thermal_decision(2,true,60100,true,4000)==EldenThermalHigh);
    CHECK(elden_usb_thermal_decision(2,true,60100,true,4010)==EldenThermalKeep);
}
int main(void){normal();failures();scale_and_policy();printf("ADC transaction, measured scale and thermal policy: %u checks passed\n",checks);}
