/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Aw99706.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
typedef struct {uint8_t regs[32],written[64];unsigned count,reads;int fail_at;bool wrong_readback;} Fake;
static int read(void *p,uint8_t reg,uint8_t *value){Fake *f=p;CHECK(reg!=0x1e&&reg!=0x1f);++f->reads;*value=f->regs[reg];if(f->wrong_readback&&f->count&&reg==5)*value^=1;return 0;}
static int write(void *p,uint8_t reg,uint8_t value){Fake *f=p;CHECK(reg==4||reg==5||reg==0x0d);if(f->fail_at>=0&&(int)f->count==f->fail_at)return -1;f->written[f->count++]=reg;f->regs[reg]=value;return 0;}
static void reset(Fake *f){
    /* Clean model, not the captured OEM state: that capture has FLAG=8 (OVL).
     * The actual captured FLAG is tested separately below and must refuse. */
    const uint8_t captured[32]={0x65,0x39,0x32,4,0,0x31,0xd9,5,1,0x4b,0x72,1,0x48,0xf8,0,0,0,7};
    memset(f,0,sizeof(*f));memcpy(f->regs,captured,sizeof(captured));f->fail_at=-1;
}
int main(void){
    Fake f;reset(&f);EldenBacklightIo io={&f,read,write};EldenAw99706 s;
    CHECK(!elden_aw99706_inspect(&s,&io)&&s.level==49&&s.enabled&&f.count==0);
    CHECK(!elden_aw99706_set_level(&s,511)&&f.regs[4]==1&&f.regs[5]==255&&f.count==2);
    CHECK(f.written[0]==4&&f.written[1]==5&&f.regs[2]==0x32&&f.regs[6]==0xd9);
    CHECK(!elden_aw99706_set_level(&s,0)&&f.regs[0x0d]==0x78&&!s.enabled);
    CHECK(!elden_aw99706_set_level(&s,200)&&f.regs[0x0d]==0xf8&&s.enabled);
    unsigned before=f.count;CHECK(elden_aw99706_set_level(&s,4096)==EldenBlArgument&&f.count==before);
    CHECK(!elden_aw99706_set_level(&s,200)&&f.count==before);
    reset(&f);f.regs[0x11]=0;CHECK(elden_aw99706_inspect(&s,&io)==EldenBlIdentity&&f.count==0);
    reset(&f);f.regs[0x0d]=0;CHECK(elden_aw99706_inspect(&s,&io)==EldenBlConfiguration&&f.count==0);
    reset(&f);f.regs[0x12]=1;CHECK(elden_aw99706_inspect(&s,&io)==EldenBlFault&&f.count==0);
    for(unsigned bit=0;bit<8;++bit){
        reset(&f);f.regs[0x10]=1u<<bit;
        CHECK(elden_aw99706_inspect(&s,&io)==EldenBlFault&&s.observed_flags==(1u<<bit)&&f.count==0);
        f.regs[0x10]=0;CHECK(elden_aw99706_set_level(&s,24)==EldenBlConfiguration&&f.count==0);
    }
    reset(&f);f.regs[4]=0xa0;CHECK(!elden_aw99706_inspect(&s,&io));CHECK(!elden_aw99706_set_level(&s,0x345)&&f.regs[4]==0xa3);
    reset(&f);CHECK(!elden_aw99706_inspect(&s,&io));f.fail_at=1;
    CHECK(elden_aw99706_set_level(&s,0x456)==EldenBlIo&&s.failed&&s.level==49);
    before=f.count;CHECK(elden_aw99706_set_level(&s,0x31)==EldenBlConfiguration&&f.count==before);
    reset(&f);CHECK(!elden_aw99706_inspect(&s,&io));f.wrong_readback=true;
    CHECK(elden_aw99706_set_level(&s,0x200)==EldenBlVerify&&s.failed);
    for(unsigned reg=0x10;reg<=0x13;reg++){
        if(reg==0x11)continue;
        reset(&f);CHECK(!elden_aw99706_inspect(&s,&io));f.regs[reg]=1;
        CHECK(elden_aw99706_set_level(&s,24)==EldenBlFault&&f.count==0);
        f.regs[reg]=0;CHECK(elden_aw99706_set_level(&s,49)==EldenBlConfiguration&&f.count==0);
    }
    for(unsigned mode=0;mode<4;mode++){
        reset(&f);f.regs[7]=(f.regs[7]&~3u)|mode;
        CHECK(elden_aw99706_inspect(&s,&io)==(mode==1?0:EldenBlConfiguration)&&f.count==0);
    }
    for(unsigned reg=4;reg<=13;reg++){
        if(reg!=4&&reg!=5&&reg!=7&&reg!=13)continue;
        reset(&f);CHECK(!elden_aw99706_inspect(&s,&io));f.regs[reg]^=1;
        CHECK(elden_aw99706_set_level(&s,24)==EldenBlVerify&&f.count==0);
    }
    printf("AW99706: %u checks, original trace, zero/off/re-enable, no MTP or configuration writes\n",checks);return 0;
}
