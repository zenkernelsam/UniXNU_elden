/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent decoder of actual original GENI two-chip command bytes. */
#include "Platform.h"
#include "EldenBacklightResources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"I2c10 line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint32_t gcc[0x60000/4],se[0x4000/4],pin[2],enable[2];
    uint8_t chip[32],wl[32],reg,address;uint32_t pending,command,tx,rx;
    unsigned writes,commands,cancels,aborts,chip_writes,wl_writes,lock;
    uint64_t now;bool stuck,nack,cancel_ack,freeze_time,release_fail;
} Machine;
static uint32_t read_reg(void *context,enum EldenBlSpace space,uint32_t reg){Machine *m=context;
    if(space==EldenBlGcc){CHECK(reg<sizeof(m->gcc));return m->gcc[reg/4];}
    if(space==EldenBlPins){CHECK(reg==0||reg==0x1000);return m->pin[reg/0x1000];}
    if(space==EldenBlEnable){CHECK(reg==0||reg==4);return m->enable[reg/4];}
    CHECK(reg<sizeof(m->se));if(reg==GENI_M_IRQ_STATUS)return m->pending;
    if(reg==GENI_RX_FIFO_STATUS)return m->rx?1:0;
    if(reg==GENI_RX_FIFO){CHECK(m->rx&&m->reg<32);m->rx=0;return m->address==0x2f?m->wl[m->reg]:m->chip[m->reg];}
    return m->se[reg/4];}
static void write_reg(void *context,enum EldenBlSpace space,uint32_t reg,uint32_t value){Machine *m=context;m->writes++;
    CHECK(space!=EldenBlPins&&space!=EldenBlEnable);
    if(space==EldenBlGcc){CHECK(reg==ELDEN_BL_GCC_VOTE||reg==ELDEN_BL_GCC_RCG);if(!m->release_fail)m->gcc[reg/4]=value&~(1u<<31);return;}
    CHECK(reg<sizeof(m->se));if(reg==GENI_M_IRQ_CLEAR){m->pending&=~value;return;}
    m->se[reg/4]=value;
    if(reg==GENI_M_CMD0){m->commands++;m->command=value;m->address=(value>>9)&127;CHECK(m->address==0x76||m->address==0x2f);
        m->se[0x40/4]=1;m->tx=0;if(m->stuck){m->pending=0;return;}
        if(m->nack){m->pending=GENI_M_CMD_DONE|(1u<<9);m->se[0x40/4]=0;return;}
        if((value>>27)==GENI_OP_WRITE)m->pending=GENI_M_TX_WATERMARK;
        else{CHECK((value>>27)==GENI_OP_READ);m->rx=1;m->pending=GENI_M_CMD_DONE;m->se[0x40/4]=0;}}
    if(reg==GENI_TX_FIFO){CHECK((m->command>>27)==GENI_OP_WRITE&&++m->tx==1);m->reg=value&255;CHECK(m->reg<32);
        unsigned len=m->se[GENI_TX_LEN/4];CHECK(len==1||len==2);
        if(len==2){if(m->address==0x2f){CHECK(m->reg==14||(m->reg>=3&&m->reg<=9));
                if(m->reg!=14)CHECK(!(m->wl[14]&(1u<<(m->reg-3))));m->wl[m->reg]=(value>>8)&255;m->wl_writes++;}
            else{CHECK(m->reg==4||m->reg==5||m->reg==13);m->chip[m->reg]=(value>>8)&255;m->chip_writes++;}}
        m->pending=GENI_M_CMD_DONE;m->se[0x40/4]=0;}
    if(reg==GENI_M_CMD_CTRL){if(value==GENI_CMD_CTRL_CANCEL){m->cancels++;if(m->cancel_ack){m->pending=GENI_M_CMD_CANCEL;m->se[0x40/4]=0;}}
        else{CHECK(value==GENI_CMD_CTRL_ABORT);m->aborts++;}}
}
static uint64_t now_us(void *v){return ((Machine *)v)->now;}
static void delay_us(void *v,unsigned us){Machine *m=v;if(!m->freeze_time)m->now+=us;}
static int lock(void *v,bool on){Machine *m=v;if(on){CHECK(!m->lock);m->lock=1;}else{CHECK(m->lock);m->lock=0;}return 0;}
void *elden_camera_i2c10_peer_create(EldenBlPlatform *p){Machine *m=calloc(1,sizeof(*m));CHECK(m);m->pin[0]=m->pin[1]=7;m->enable[0]=1u<<9;m->enable[1]=3;
    m->gcc[ELDEN_BL_GCC_RCG/4+1]=0x601;m->gcc[ELDEN_BL_GCC_VOTE/4]=1u<<20;
    m->se[GENI_FW_REVISION/4]=GENI_PROTO_I2C<<8;m->se[GENI_CLK_CFG/4]=1;m->se[GENI_SCL_COUNTERS/4]=0x9280;
    m->se[GENI_TX_PACK0/4]=0xabc;m->se[GENI_TX_PACK1/4]=0xdef;m->se[GENI_TX_LEN/4]=2;m->se[GENI_RX_LEN/4]=1;
    const uint8_t stock[32]={0x65,0x39,0x32,4,0,0x31,0xd9,5,1,0x4b,0x72,1,0x48,0xf8,0,0,0,7};memcpy(m->chip,stock,32);
    m->wl[0]=0x82;for(unsigned i=0;i<7;i++)m->wl[i+3]=(uint8_t)(i<2?0x80|20u:140u+i);
    *p=(EldenBlPlatform){.io={m,read_reg,write_reg,now_us,delay_us,lock}};return m;}
unsigned elden_camera_i2c10_peer_stat(void *v,unsigned field){Machine *m=v;switch(field){case 0:return m->commands;case 1:return m->chip_writes;
    case 2:return m->wl_writes;case 3:return m->cancels;case 4:return m->aborts;case 5:return checks;case 6:return m->wl[14];
    case 7:return m->gcc[ELDEN_BL_GCC_VOTE/4];default:return 0;}}
void elden_camera_i2c10_peer_fault(void *v,unsigned fault){Machine *m=v;m->nack=fault==1;m->stuck=fault==2||fault==3;m->cancel_ack=fault==2;m->release_fail=fault==4;}
void elden_camera_i2c10_peer_destroy(void *v){free(v);}
