/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Spmi.h"
#include "EldenAdcResources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
typedef struct {uint32_t core[0x3000/4],owner[896],obs[8],writer[8];uint8_t sdam[256];unsigned commands;bool timeout,denied;} Bus;
static Bus bus;
static uint32_t read32(void *p,enum EldenAdcSpmiSpace s,uint32_t off){
    Bus *b=p;CHECK(!(off&3));
    switch(s){
    case EldenAdcSpmiCore:CHECK(off<sizeof(b->core));return b->core[off/4];
    case EldenAdcSpmiOwner:CHECK(off<sizeof(b->owner));return b->owner[off/4];
    case EldenAdcSpmiObserver:CHECK(off<sizeof(b->obs));return b->obs[off/4];
    case EldenAdcSpmiWriter:CHECK(off<sizeof(b->writer));return b->writer[off/4];
    }
    return 0xffffffff;
}
static void write32(void *p,enum EldenAdcSpmiSpace s,uint32_t off,uint32_t value){
    Bus *b=p;CHECK(s==EldenAdcSpmiObserver||s==EldenAdcSpmiWriter);CHECK(off<32&&!(off&3));
    uint32_t *r=s==EldenAdcSpmiObserver?b->obs:b->writer;r[off/4]=value;
    if(off)return;
    b->commands++;unsigned address=(value>>4)&255,n=(value&7)+1;
    CHECK((value>>27)==(s==EldenAdcSpmiObserver?1u:0u));CHECK(address+n<=256);
    if(s==EldenAdcSpmiObserver){
        r[6]=r[7]=0;for(unsigned i=0;i<n;i++)r[6+i/4]|=(uint32_t)b->sdam[address+i]<<(8*(i%4));
    }else for(unsigned i=0;i<n;i++)b->sdam[address+i]=(uint8_t)(r[4+i/4]>>(8*(i%4)));
    r[2]=b->timeout?0:b->denied?5:1;
}
static void delay(void *p,unsigned us){(void)p;CHECK(us==1);}
static EldenAdcSpmi setup(void){
    memset(&bus,0,sizeof(bus));bus.core[0]=ELDEN_ADC_SPMI_VERSION;bus.core[1]=896;
    bus.core[(0x2000+4*591)/4]=(0x90u<<8)|1;
    EldenAdcSpmi s={.io={&bus,read32,write32,delay}};return s;
}
int main(void){
    EldenAdcSpmi s=setup();CHECK(!elden_adc_spmi_find(&s,true)&&s.apid==591&&s.write_owner==0);
    const uint8_t cfg[]={7,0x80,1,0x14,0x80,0x47,0};uint8_t out[7];
    CHECK(!elden_adc_spmi_write(&s,0x4f,cfg,7));CHECK(bus.writer[0]==0x4f6);
    CHECK(bus.writer[4]==0x14018007&&bus.writer[5]==0x4780);
    CHECK(!elden_adc_spmi_read(&s,0x4f,out,7)&&!memcmp(cfg,out,7)&&bus.obs[0]==0x080004f6);
    unsigned before=bus.commands;uint8_t one=1;
    CHECK(elden_adc_spmi_write(&s,0x48,&one,1)==EldenAdcArgument&&bus.commands==before);
    CHECK(elden_adc_spmi_read(&s,0xff,out,2)==EldenAdcArgument&&bus.commands==before);
    bus.owner[591]=1;CHECK(elden_adc_spmi_write(&s,0xe5,&one,1)==EldenAdcBusy&&bus.commands==before);
    s=setup();bus.owner[591]=1;CHECK(elden_adc_spmi_find(&s,true)==EldenAdcBusy);
    CHECK(!elden_adc_spmi_find(&s,false)&&s.write_owner==1);
    CHECK(elden_adc_spmi_write(&s,0xe5,&one,1)==EldenAdcBusy&&!bus.commands);
    CHECK(!elden_adc_spmi_read(&s,0x45,out,1));
    s=setup();bus.core[(0x2000+4*592)/4]=(0x90u<<8)|1;CHECK(elden_adc_spmi_find(&s,true)==EldenAdcBusy);
    s=setup();CHECK(!elden_adc_spmi_find(&s,true));bus.timeout=true;
    CHECK(elden_adc_spmi_read(&s,0x45,out,1)==EldenAdcTimeout&&s.uncertain);
    before=bus.commands;CHECK(elden_adc_spmi_read(&s,0x45,out,1)==EldenAdcBusy&&bus.commands==before);
    s=setup();CHECK(!elden_adc_spmi_find(&s,true));bus.denied=true;
    CHECK(elden_adc_spmi_write(&s,0xe5,&one,1)==EldenAdcIoError&&s.uncertain);
    s=setup();s.target=EldenSpmiUsb2Gpio;
    bus.core[(0x2000+4*610)/4]=(0x188u<<8)|1;
    CHECK(!elden_adc_spmi_find(&s,true)&&s.apid==610);
    CHECK(!elden_adc_spmi_read(&s,4,out,1));
    before=bus.commands;
    CHECK(elden_adc_spmi_write(&s,0x41,&one,1)==EldenAdcArgument&&bus.commands==before);
    CHECK(elden_adc_spmi_write(&s,0xe5,&one,1)==EldenAdcArgument&&bus.commands==before);
    one=0x80;CHECK(!elden_adc_spmi_write(&s,0x44,&one,1)&&bus.sdam[0x44]==0x80);
    one=1;CHECK(!elden_adc_spmi_write(&s,0x40,&one,1)&&bus.sdam[0x40]==1);
    printf("SPMI v7.2 discovery, ownership and byte framing: %u checks passed\n",checks);
}
