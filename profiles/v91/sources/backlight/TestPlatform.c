/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Platform.h"
#include "Aw99706.h"
#include "EldenBacklightResources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
typedef struct {
    uint32_t gcc[0x60000/4],se[0x4000/4],pin[2],enable[2];
    uint8_t chip[32],reg;uint32_t pending,command,tx,rx;
    unsigned writes,commands,cancels,aborts,chip_writes,lock;
    uint64_t now;bool stuck,nack,cancel_ack,freeze_time,drop_pack;
} Machine;
static Machine machine;
static uint32_t read(void *context,enum EldenBlSpace space,uint32_t reg){
    Machine *m=context;
    if(space==EldenBlGcc){CHECK(reg<sizeof(m->gcc));return m->gcc[reg/4];}
    if(space==EldenBlPins){CHECK(reg==0||reg==0x1000);return m->pin[reg/0x1000];}
    if(space==EldenBlEnable){CHECK(reg==0||reg==4);return m->enable[reg/4];}
    CHECK(reg<sizeof(m->se));
    if(reg==GENI_M_IRQ_STATUS)return m->pending;
    if(reg==GENI_RX_FIFO_STATUS)return m->rx?1:0;
    if(reg==GENI_RX_FIFO){CHECK(m->rx);m->rx=0;return m->chip[m->reg];}
    return m->se[reg/4];
}
static void write(void *context,enum EldenBlSpace space,uint32_t reg,uint32_t value){
    Machine *m=context;++m->writes;
    CHECK(space!=EldenBlPins&&space!=EldenBlEnable);
    if(space==EldenBlGcc){CHECK(reg==ELDEN_BL_GCC_VOTE||reg==ELDEN_BL_GCC_RCG);m->gcc[reg/4]=value&~(1u<<31);return;}
    CHECK(reg<sizeof(m->se));
    if(reg==GENI_M_IRQ_CLEAR){m->pending&=~value;return;}
    if(reg==GENI_TX_PACK0&&m->drop_pack)return;
    m->se[reg/4]=value;
    if(reg==GENI_M_CMD0){
        ++m->commands;m->command=value;CHECK(((value>>9)&127)==ELDEN_BL_ADDRESS);
        m->se[0x40/4]=1;m->tx=0;
        if(m->stuck){m->pending=0;return;}
        if(m->nack){m->pending=GENI_M_CMD_DONE|(1u<<9);m->se[0x40/4]=0;return;}
        if((value>>27)==GENI_OP_WRITE)m->pending=GENI_M_TX_WATERMARK;
        else {CHECK((value>>27)==GENI_OP_READ);m->rx=1;m->pending=GENI_M_CMD_DONE;m->se[0x40/4]=0;}
    }
    if(reg==GENI_TX_FIFO){
        CHECK((m->command>>27)==GENI_OP_WRITE);CHECK(++m->tx==1);
        m->reg=value&255;CHECK(m->reg<0x1e);
        unsigned len=m->se[GENI_TX_LEN/4];CHECK(len==1||len==2);
        if(len==2){CHECK(m->reg==4||m->reg==5||m->reg==13);m->chip[m->reg]=(value>>8)&255;++m->chip_writes;}
        m->pending=GENI_M_CMD_DONE;m->se[0x40/4]=0;
    }
    if(reg==GENI_M_CMD_CTRL){
        if(value==GENI_CMD_CTRL_CANCEL){++m->cancels;if(m->cancel_ack){m->pending=GENI_M_CMD_CANCEL;m->se[0x40/4]=0;}}
        else {CHECK(value==GENI_CMD_CTRL_ABORT);++m->aborts;}
    }
}
static uint64_t now_us(void *c){return ((Machine *)c)->now;}
static void delay_us(void *c,unsigned us){Machine *m=c;if(!m->freeze_time)m->now+=us;}
static int lock(void *c,bool acquire){Machine *m=c;if(acquire){CHECK(!m->lock);m->lock=1;}else{CHECK(m->lock);m->lock=0;}return 0;}
static EldenBlPlatform setup(void){
    Machine *m=&machine;memset(m,0,sizeof(*m));
    m->pin[0]=m->pin[1]=7;m->enable[0]=1u<<9;m->enable[1]=3;
    m->gcc[ELDEN_BL_GCC_RCG/4]=0;m->gcc[ELDEN_BL_GCC_RCG/4+1]=0x601;
    m->gcc[ELDEN_BL_GCC_VOTE/4]=1u<<20;
    m->se[GENI_FW_REVISION/4]=GENI_PROTO_I2C<<8;m->se[GENI_CLK_CFG/4]=1;
    m->se[GENI_SCL_COUNTERS/4]=0x9280;m->se[GENI_TX_PACK0/4]=0xabc;
    m->se[GENI_TX_PACK1/4]=0xdef;m->se[GENI_TX_LEN/4]=2;m->se[GENI_RX_LEN/4]=1;
    /* Clean model: OEM captured FLAG=8 is a separate fault case, not admission. */
    const uint8_t stock[32]={0x65,0x39,0x32,4,0,0x31,0xd9,5,1,0x4b,0x72,1,0x48,0xf8,0,0,0,7};
    memcpy(m->chip,stock,sizeof(stock));
    EldenBlPlatform p={.io={m,read,write,now_us,delay_us,lock}};return p;
}
static void happy(void){
    EldenBlPlatform p=setup();CHECK(!elden_bl_platform_acquire(&p));
    EldenAw99706 chip;EldenBacklightIo io={&p,elden_bl_platform_read,elden_bl_platform_write};
    CHECK(!elden_aw99706_inspect(&chip,&io));CHECK(chip.level==49&&chip.enabled);
    CHECK(!elden_aw99706_set_level(&chip,511));CHECK(machine.chip[4]==1&&machine.chip[5]==255);
    CHECK(!elden_aw99706_set_level(&chip,0));CHECK(machine.chip[13]==0x78);
    CHECK(!elden_aw99706_set_level(&chip,49));CHECK(machine.chip[13]==0xf8);
    unsigned before=machine.writes;uint8_t v=0;
    CHECK(elden_bl_platform_read(&p,0x1e,&v)==EldenBlBadArgument);
    CHECK(elden_bl_platform_write(&p,0x1f,0xaa)==EldenBlBadArgument);CHECK(machine.writes==before);
    CHECK(!elden_bl_platform_release(&p));CHECK(!p.acquired&&!p.quarantined);
    CHECK(machine.gcc[ELDEN_BL_GCC_VOTE/4]==(1u<<20));
    CHECK(machine.gcc[ELDEN_BL_GCC_RCG/4]==0&&machine.gcc[ELDEN_BL_GCC_RCG/4+1]==0x601);
    CHECK(machine.se[GENI_TX_PACK0/4]==0xabc&&machine.se[GENI_TX_PACK1/4]==0xdef);
    CHECK(machine.se[GENI_TX_LEN/4]==2&&machine.se[GENI_RX_LEN/4]==1);
}
static void refusal(void){
    for(unsigned which=0;which<8;which++){
        EldenBlPlatform p=setup();
        switch(which){
        case 0:machine.se[0x40/4]=1;break;
        case 1:machine.se[0xe18/4]=1;break;
        case 2:machine.se[GENI_DMA_MODE/4]=1;break;
        case 3:machine.se[0x800/4]=1;break;
        case 4:machine.pin[0]=0;break;
        case 5:machine.enable[1]=0;break;
        case 6:machine.se[GENI_FW_REVISION/4]=1u<<8;break;
        case 7:machine.se[GENI_SCL_COUNTERS/4]=0;break;
        }
        CHECK(elden_bl_platform_acquire(&p)!=0);CHECK(!machine.commands&&!machine.chip_writes);
        CHECK(machine.gcc[ELDEN_BL_GCC_VOTE/4]==(1u<<20));CHECK(!p.quarantined);
        CHECK(machine.se[GENI_TX_PACK0/4]==0xabc);
    }
}
static void failure(void){
    for(unsigned which=0;which<4;which++){
        EldenBlPlatform p=setup();CHECK(!elden_bl_platform_acquire(&p));
        machine.stuck=which!=0;machine.nack=which==0;machine.cancel_ack=which==1;machine.freeze_time=which==3;
        uint8_t v=0;CHECK(elden_bl_platform_read(&p,0x11,&v)!=0);CHECK(p.failed);
        unsigned before=machine.commands;CHECK(elden_bl_platform_read(&p,0x11,&v)!=0);CHECK(machine.commands==before);
        if(which<2){CHECK(!p.quarantined);CHECK(!elden_bl_platform_release(&p));}
        else{CHECK(p.quarantined);CHECK(elden_bl_platform_release(&p)!=0);CHECK(p.engine_changed&&p.votes_changed);}
        CHECK(!machine.chip_writes);
    }
    EldenBlPlatform p=setup();machine.drop_pack=true;
    CHECK(elden_bl_platform_acquire(&p)!=0&&p.quarantined);CHECK(!machine.commands);
}
static void shared_clock(void){
    const unsigned clients[]={10,17,27,28};
    for(unsigned i=0;i<4;i++){
        EldenBlPlatform p=setup();machine.gcc[0x23298/4]=(1u<<31)|2;
        CHECK(!elden_bl_platform_acquire(&p));
        /* Bluetooth, SE7 or IBI appears after our snapshot. */
        machine.gcc[ELDEN_BL_GCC_VOTE/4]|=1u<<clients[i];
        CHECK(!elden_bl_platform_release(&p));
        CHECK(machine.gcc[ELDEN_BL_GCC_VOTE/4]==((1u<<20)|(1u<<clients[i])|15));
    }
}
static void secondary_event(void){
    EldenBlPlatform p=setup();machine.se[0x640/4]=1u<<27;
    CHECK(!elden_bl_platform_acquire(&p));
    uint8_t id=0;CHECK(!elden_bl_platform_read(&p,0x11,&id)&&id==7);
    CHECK(!elden_bl_platform_release(&p));
    CHECK(machine.se[0x640/4]==(1u<<27));
    p=setup();machine.se[0x640/4]=(1u<<27)|(1u<<3);
    CHECK(elden_bl_platform_acquire(&p)==EldenBlBusy&&!machine.commands);
    p=setup();machine.se[0x640/4]=1u<<27;machine.se[0x40/4]=1u<<12;
    CHECK(elden_bl_platform_acquire(&p)==EldenBlBusy&&!machine.commands);
    p=setup();machine.se[0x640/4]=1u<<27;machine.rx=1;
    CHECK(elden_bl_platform_acquire(&p)==EldenBlBusy&&!machine.commands);
}
int main(void){happy();refusal();failure();shared_clock();secondary_event();printf("Backlight GENI ownership/transactions: %u checks passed\n",checks);}
