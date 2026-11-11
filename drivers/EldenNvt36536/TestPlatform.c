/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Platform.h"
#include "EldenTouchBoard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct model {
    uint32_t gcc[0x60000/4],pins[0x4000/4],irq[0x1000/4],se[0x4000/4],qup[0x2000/4];
    unsigned writes,se_writes,se_reads,delay,commands;
    bool halt_stuck,update_stuck,transaction_stuck,pin_readback_bad;
};
static uint32_t *reg(struct model *m,enum EldenTouchSpace space,uint32_t address){
    CHECK(!(address&3));
    switch(space){
      case EldenTouchGcc:CHECK(address<0x60000);return &m->gcc[address/4];
      case EldenTouchPins:CHECK(address<0x4000);return &m->pins[address/4];
      case EldenTouchIrq:CHECK(address<0x1000);return &m->irq[address/4];
      case EldenTouchSe:CHECK(address<0x4000);return &m->se[address/4];
      case EldenTouchQup:CHECK(address<0x2000);return &m->qup[address/4];
    }
    CHECK(false);return NULL;
}
static uint32_t read(void *context,enum EldenTouchSpace space,uint32_t address){
    struct model *m=context;
    if(space==EldenTouchSe)m->se_reads++;
    if(space==EldenTouchGcc){
        const unsigned branch[5]={0x2315c,0x23148,0x23140,0x23144,0x183cc},bits[5]={18,19,20,21,26};
        for(unsigned i=0;i<5;i++)if(address==branch[i])return m->halt_stuck||!(m->gcc[0x52008/4]&(1u<<bits[i]))?0x80000000:0;
    }
    return *reg(m,space,address);
}
static void write(void *context,enum EldenTouchSpace space,uint32_t address,uint32_t value){
    struct model *m=context;m->writes++;
    CHECK(space!=EldenTouchQup);
    if(space==EldenTouchSe){
        m->se_writes++;
        if(address==0xc4c){m->se[0xc48/4]|=value;return;}
        if(address==0xd4c){m->se[0xd48/4]|=value;return;}
        if(address==0xc50){m->se[0xc48/4]&=~value;return;}
        if(address==0xd50){m->se[0xd48/4]&=~value;return;}
        if(address==SE_GENI_M_IRQ_CLEAR){m->se[SE_GENI_M_IRQ_STATUS/4]&=~value;return;}
        if(address==SE_GENI_M_CMD0){m->commands++;m->se[SE_GENI_STATUS/4]=1;m->se[SE_GENI_M_IRQ_STATUS/4]=0;}
    }
    if(space==EldenTouchIrq)CHECK(address==0); // no interrupt enable/target/ack
    if(space==EldenTouchPins)CHECK(!(address%0x1000));
    if(space==EldenTouchPins&&m->pin_readback_bad)value^=4;
    if(space==EldenTouchGcc&&address==ELDEN_TOUCH_GCC_S4_RCG){
        value=value&2?value&~(1u<<31):value|(1u<<31);
        if(!m->update_stuck)value&=~1u;
    }
    *reg(m,space,address)=value;
}
static void delay(void *context,unsigned us){((struct model *)context)->delay+=us;}
static void cache_stub(void *context,void *memory,size_t bytes){(void)context;(void)memory;(void)bytes;CHECK(false);}
static struct model *fresh(EldenTouchPlatform *p){
    struct model *m=calloc(1,sizeof(*m));CHECK(m!=NULL);memset(p,0,sizeof(*p));
    p->io=(EldenTouchIo){m,read,write,delay};
    m->gcc[ELDEN_TOUCH_GCC_VOTE/4]=(1u<<4)|(1u<<19);
    m->gcc[ELDEN_TOUCH_GCC_S4_RCG/4]=1u<<31;
    m->gcc[(ELDEN_TOUCH_GCC_S4_RCG+4)/4]=(6u<<8)|(2u<<12)|7u;
    m->qup[QUPV3_HW_VER_REG/4]=0x30080000;
    m->se[GENI_FW_REVISION_RO/4]=1u<<8;m->se[SE_HW_PARAM_0/4]=0x20100000;
    m->se[SE_IRQ_EN/4]=0x123;m->se[GENI_OUTPUT_CTRL/4]=0x41;
    for(unsigned i=0;i<4;i++)m->pins[i*0x1000/4]=0x10000080;
    m->irq[0]=0x20000000;return m;
}
int main(void){
    EldenTouchPlatform p;struct model *m=fresh(&p);uint32_t vote=m->gcc[ELDEN_TOUCH_GCC_VOTE/4],cfg=m->gcc[(ELDEN_TOUCH_GCC_S4_RCG+4)/4];
    CHECK(!elden_touch_platform_acquire(&p));CHECK(p.acquired&&!p.quarantined);
    CHECK(!p.spi.use_dma&&!p.spi.allow_fifo_disabled&&p.spi.poll_only==1);
    CHECK(m->se[SE_IRQ_EN/4]==0);CHECK(m->se[GENI_SER_M_CLK_CFG/4]==33);
    CHECK((m->gcc[(ELDEN_TOUCH_GCC_S4_RCG+4)/4]&((7u<<8)|(3u<<12)|31u))==1);
    for(unsigned i=0;i<4;i++)CHECK(((m->pins[i*0x1000/4]>>2)&15)==1);
    // A peer's unrelated vote/pad flags appearing during the lease survive.
    m->gcc[ELDEN_TOUCH_GCC_VOTE/4]|=1u<<8;m->pins[0]|=1u<<28;
    CHECK(!elden_touch_platform_release(&p));CHECK(!p.acquired);
    CHECK(m->gcc[ELDEN_TOUCH_GCC_VOTE/4]==(vote|(1u<<8)));
    CHECK(m->gcc[(ELDEN_TOUCH_GCC_S4_RCG+4)/4]==cfg);
    CHECK(m->se[SE_IRQ_EN/4]==0x123&&m->se[GENI_OUTPUT_CTRL/4]==0x41);
    CHECK(m->pins[0]==0x10000080&&m->irq[0]==0x20000000);free(m);
    /* Survey mode records disabled/active DMA as data, never takes over SE. */
    for(unsigned survey=0;survey<3;survey++){
        m=fresh(&p);p.survey_only=true;vote=m->gcc[ELDEN_TOUCH_GCC_VOTE/4];
        if(survey==1)m->se[GENI_IF_DISABLE_RO/4]=1;
        if(survey==2){m->se[SE_GSI_EVENT_EN/4]=15;m->se[SE_GENI_DMA_MODE_EN/4]=1;m->se[SE_GENI_STATUS/4]=1;}
        CHECK(!elden_touch_platform_acquire(&p)&&!p.acquired&&p.stage==2);
        CHECK(p.entry_gsi==m->se[SE_GSI_EVENT_EN/4]&&p.entry_dma==m->se[SE_GENI_DMA_MODE_EN/4]);
        CHECK(!m->se_writes&&!m->commands&&!p.touched_pins&&!p.touched_cfg);
        CHECK(!elden_touch_platform_release(&p)&&m->gcc[ELDEN_TOUCH_GCC_VOTE/4]==vote);free(m);
    }
    m=fresh(&p);p.survey_only=true;m->se[GENI_IF_DISABLE_RO/4]=1;
    m->se[SE_GSI_EVENT_EN/4]=15;m->se[SE_GENI_DMA_MODE_EN/4]=1;
    m->se[0xc48/4]=9;m->se[0xd48/4]=10;m->se[SE_DMA_TX_PTR_L/4]=0x12340000;
    CHECK(!elden_touch_platform_acquire(&p));
    uint8_t dma_tx[64],dma_rx[64];
    p.spi.dma=(struct geni_dma){m,cache_stub,cache_stub,cache_stub,dma_tx,dma_rx,0x10000,0x20000,64};
    CHECK(!elden_touch_platform_activate_dma(&p)&&p.acquired&&p.dma_mode&&p.spi.use_dma&&p.spi.poll_only==2);
    CHECK(m->se[SE_GSI_EVENT_EN/4]==0&&m->se[SE_GENI_DMA_MODE_EN/4]==1&&!m->commands);
    CHECK(elden_touch_platform_transfer(&p,dma_tx,NULL,1)==GENI_ERR_IO); // wrapper is mandatory
    m->se[0xc48/4]=7;m->se[0xd48/4]=7;m->se[SE_DMA_TX_PTR_L/4]=0xdead0000;
    CHECK(!elden_touch_platform_release(&p));
    CHECK(m->se[SE_GSI_EVENT_EN/4]==15&&m->se[0xc48/4]==9&&m->se[0xd48/4]==10&&
          m->se[SE_DMA_TX_PTR_L/4]==0x12340000&&!m->commands);free(m);
    // Refuse GPI/FIFO-disabled/active SE/foreign pin ownership before SE writes.
    for(unsigned test=0;test<5;test++){
        m=fresh(&p);vote=m->gcc[ELDEN_TOUCH_GCC_VOTE/4];
        if(test==0)m->se[GENI_IF_DISABLE_RO/4]=1;
        if(test==1)m->se[SE_GSI_EVENT_EN/4]=1;
        if(test==2)m->se[SE_GENI_STATUS/4]=1u<<12;
        if(test==3)m->pins[0]|=2u<<2;
        if(test==4)m->se[GENI_FW_REVISION_RO/4]=2u<<8;
        CHECK(elden_touch_platform_acquire(&p)<0);CHECK(!p.quarantined&&!p.acquired);
        CHECK(!m->se_writes&&!m->commands);CHECK(m->gcc[ELDEN_TOUCH_GCC_VOTE/4]==vote);free(m);
    }
    m=fresh(&p);m->halt_stuck=true;
    CHECK(elden_touch_platform_acquire(&p)==GENI_ERR_TIMEOUT);CHECK(p.quarantined);
    CHECK(!m->se_reads&&!m->se_writes);unsigned writes=m->writes;
    CHECK(elden_touch_platform_release(&p)<0&&m->writes==writes);free(m);
    m=fresh(&p);m->update_stuck=true;
    CHECK(elden_touch_platform_acquire(&p)==GENI_ERR_TIMEOUT);CHECK(p.quarantined);
    CHECK(!m->se_writes);writes=m->writes;CHECK(elden_touch_platform_release(&p)<0&&m->writes==writes);free(m);
    m=fresh(&p);m->pin_readback_bad=true;
    CHECK(elden_touch_platform_acquire(&p)<0&&p.quarantined);CHECK(!m->se_writes);free(m);
    m=fresh(&p);CHECK(!elden_touch_platform_acquire(&p));m->se[SE_GENI_STATUS/4]=1;
    writes=m->writes;CHECK(elden_touch_platform_release(&p)<0&&p.quarantined&&m->writes==writes);free(m);
    m=fresh(&p);CHECK(!elden_touch_platform_acquire(&p));uint8_t tx[3]={0xff,0,0};
    CHECK(elden_touch_platform_transfer(&p,tx,NULL,3)==GENI_ERR_TIMEOUT&&p.quarantined);
    writes=m->writes;CHECK(elden_touch_platform_transfer(&p,tx,NULL,3)<0&&m->writes==writes);
    CHECK(elden_touch_platform_release(&p)<0&&m->writes==writes);free(m);
    printf("PASS %u clock/pin/PIO ownership, restoration and quarantine checks\n",checks);return 0;
}
