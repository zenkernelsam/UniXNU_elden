/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaSurvey.h"
#include <assert.h>
#include <stdio.h>
static uint32_t registers[0x2000/4];
static unsigned reads,routes,last_slot,context_count,high_read;
static unsigned all_slots,all_banks;
static void slot_entry(void *p,unsigned slot,uint32_t smr,uint32_t s2cr){
    assert(p==registers&&slot==all_slots++);
    assert(smr==registers[(0x800+slot*4)/4]&&s2cr==registers[(0xc00+slot*4)/4]);
}
static void bank_entry(void *p,unsigned bank,uint32_t cbar,uint32_t cba2r){
    assert(p==registers&&bank==all_banks++);
    assert(cbar==registers[(0x1000+bank*4)/4]&&cba2r==registers[(0x1800+bank*4)/4]);
}
static uint32_t read32(void *context,unsigned off){
    assert(context==registers&&!(off&3)&&off<0x2000);reads++;
    if(off>=0x800+128*4&&off<0xc00)high_read++;
    return registers[off/4];
}
static void route(void *context,const EldenTouchDmaRoute *r){
    assert(context==registers);routes++;last_slot=r->slot;context_count+=r->context_valid;
}
static void reset(void){
    memset(registers,0,sizeof(registers));registers[0x20/4]=0x48000000|200;
    registers[0x24/4]=16;registers[0x28/4]=0x1000;
    reads=routes=last_slot=context_count=high_read=0;
}
int main(void){
    EldenTouchDmaSurvey s;reset();
    registers[(0x800+180*4)/4]=0x800000a3;registers[(0xc00+180*4)/4]=(1u<<16);
    registers[(0x800+181*4)/4]=0x800000b6;registers[(0xc00+181*4)/4]=9;
    registers[(0x1000+9*4)/4]=0x10000;registers[(0x1800+9*4)/4]=1;
    assert(!elden_touch_dma_survey(registers,read32,route,0xa3,0xb6,&s));
    assert(routes==2&&s.matches[0]==1&&s.matches[1]==1&&last_slot==181&&context_count==1&&high_read);
    assert(!elden_touch_dma_inventory(registers,read32,&s,slot_entry,bank_entry));
    assert(all_slots==200&&all_banks==16);
    registers[0]^=2;all_slots=all_banks=0;
    assert(elden_touch_dma_inventory(registers,read32,&s,slot_entry,bank_entry)==-5&&!all_slots&&!all_banks);
    registers[0]^=2;
    registers[(0x800+182*4)/4]=0x800000a3;
    assert(!elden_touch_dma_survey(registers,read32,NULL,0xa3,0xb6,&s)&&s.matches[0]==2);
    reset();registers[0x800/4]=0x803f0080;
    assert(!elden_touch_dma_survey(registers,read32,route,0xa3,0xb6,&s)&&s.matches[0]==1&&s.matches[1]==1);
    reset();registers[0]=8;
    assert(elden_touch_dma_survey(registers,read32,route,0xa3,0xb6,&s)==-95&&reads==4&&!routes);
    reset();registers[0x24/4]=0xffffffff;
    assert(elden_touch_dma_survey(registers,read32,route,0xa3,0xb6,&s)==-95&&reads==4);
    reset();registers[0x800/4]=0x800000a3;registers[0xc00/4]=255;
    assert(!elden_touch_dma_survey(registers,read32,route,0xa3,0xb6,&s)&&routes==1&&!context_count);
    assert(elden_touch_dma_survey(registers,read32,route,0x8000,0xb6,&s)==-22);
    puts("PASS SMMU survey: >128 slots, overlapping masks, invalid banks, unsupported format, read-only bounds");
}
