/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaTransfer.h"
#include "EldenTouchResources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CAPACITY 16384u
#define TX_ADDRESS UINT64_C(0x180000000)
#define RX_ADDRESS UINT64_C(0x180010000)
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
typedef struct {
    uint32_t registers[0x1000/4];
    _Alignas(64) uint8_t tx_cpu[CAPACITY],rx_cpu[CAPACITY],tx_bus[CAPACITY],rx_bus[CAPACITY],staging[CAPACITY];
    unsigned commands,register_writes,tx_clean,rx_clean,invalidate,mapping_checks;
    unsigned cancel,abort,resets;
    size_t tx_length,rx_length;
    bool command,armed_tx,armed_rx,tx_requested,rx_requested,active;
    bool inject_bus_error,stalled,lingering_active,lose_route_after,lose_route_before;
    bool cache_prepared,late_bus_error;
    uint64_t epoch;
} Model;
static Model model;
static uint8_t response(uint8_t input,size_t index){return (uint8_t)(input^0xa5u^(index*7u));}
static void execute(Model *m){
    if(!m->command||(m->tx_requested&&!m->armed_tx)||(m->rx_requested&&!m->armed_rx))return;
    if(m->stalled)return;
    CHECK(!m->tx_requested||m->tx_clean);
    CHECK(!m->rx_requested||m->cache_prepared);
    if(m->inject_bus_error){
        m->registers[SE_DMA_TX_IRQ_STAT/4]|=GENI_DMA_AHB_ERR;
        m->registers[SE_DMA_RX_IRQ_STAT/4]|=GENI_DMA_AHB_ERR;return;
    }
    size_t count=m->tx_requested?m->tx_length:m->rx_length;
    CHECK(count<=CAPACITY);
    if(m->tx_requested&&m->rx_requested)CHECK(m->tx_length==m->rx_length);
    if(m->rx_requested)for(size_t i=0;i<count;i++)m->rx_bus[i]=response(m->tx_requested?m->tx_bus[i]:0,i);
    if(m->tx_requested)m->registers[SE_DMA_TX_IRQ_STAT/4]|=GENI_DMA_DONE;
    if(m->rx_requested)m->registers[SE_DMA_RX_IRQ_STAT/4]|=GENI_DMA_DONE;
    m->registers[SE_GENI_M_IRQ_STATUS/4]|=GENI_M_CMD_DONE;
    m->active=m->lingering_active;m->command=false;
}
static uint32_t rd(void *context,uint32_t off){
    Model *m=context;CHECK(!(off&3)&&off<sizeof(m->registers));
    if(off==SE_GENI_STATUS){
        if(m->commands&&!m->command&&m->late_bus_error)m->registers[SE_DMA_RX_IRQ_STAT/4]|=GENI_DMA_AHB_ERR;
        return m->active?1:0;
    }
    return m->registers[off/4];
}
static void wr(void *context,uint32_t off,uint32_t value){
    Model *m=context;CHECK(!(off&3)&&off<sizeof(m->registers));++m->register_writes;
    CHECK(off!=GENI_IF_DISABLE_RO); /* no write to read-only configuration */
    if(off==SE_GENI_M_IRQ_CLEAR){m->registers[SE_GENI_M_IRQ_STATUS/4]&=~value;return;}
    if(off==SE_DMA_TX_IRQ_CLR){m->registers[SE_DMA_TX_IRQ_STAT/4]&=~value;return;}
    if(off==SE_DMA_RX_IRQ_CLR){m->registers[SE_DMA_RX_IRQ_STAT/4]&=~value;return;}
    m->registers[off/4]=value;
    if(off==SE_GENI_M_CMD0){
        unsigned op=value>>GENI_M_OPCODE_SHIFT;CHECK(op>=1&&op<=3);
        CHECK(!(value&GENI_M_PARAM_FRAGMENTATION));
        m->tx_requested=op&1;m->rx_requested=op&2;
        m->active=m->command=true;m->armed_tx=m->armed_rx=false;++m->commands;
    }
    if(off==SE_DMA_RX_LEN){
        uint64_t address=m->registers[SE_DMA_RX_PTR_L/4]|(uint64_t)m->registers[SE_DMA_RX_PTR_H/4]<<32;
        CHECK(address==RX_ADDRESS&&m->registers[SE_DMA_RX_ATTR/4]==0);
        m->rx_length=value;m->armed_rx=true;execute(m);
    }
    if(off==SE_DMA_TX_LEN){
        uint64_t address=m->registers[SE_DMA_TX_PTR_L/4]|(uint64_t)m->registers[SE_DMA_TX_PTR_H/4]<<32;
        CHECK(address==TX_ADDRESS&&m->registers[SE_DMA_TX_ATTR/4]==GENI_DMA_TX_ATTR_EOT_BUF);
        m->tx_length=value;m->armed_tx=true;execute(m);
    }
    if(off==SE_GENI_M_CMD_CTRL_REG){
        if(value&GENI_M_CMD_CANCEL)++m->cancel;
        if(value&GENI_M_CMD_ABORT)++m->abort;
        /* Intentionally no acknowledgement; software reset is not proof. */
    }
    if(off==SE_DMA_TX_FSM_RST||off==SE_DMA_RX_FSM_RST)++m->resets;
}
static void delay(void *context,unsigned us){CHECK(context==&model&&us==1);}
static void clean(void *context,void *data,size_t n){
    Model *m=context;CHECK(data==m->tx_cpu&&n<=CAPACITY);memcpy(m->tx_bus,data,n);++m->tx_clean;
}
static void clean_inval(void *context,void *data,size_t n){
    Model *m=context;CHECK(data==m->rx_cpu&&n<=CAPACITY);memcpy(m->rx_bus,data,n);++m->rx_clean;m->cache_prepared=true;
}
static void inval(void *context,void *data,size_t n){
    Model *m=context;CHECK(data==m->rx_cpu&&n<=CAPACITY);memcpy(data,m->rx_bus,n);++m->invalidate;
}
static int mapping(void *context,enum EldenTouchDmaCheck phase,uint64_t epoch,unsigned sid,uint64_t tx,uint64_t rx,size_t span){
    Model *m=context;++m->mapping_checks;
    CHECK(epoch==m->epoch&&sid==0xa3&&tx==TX_ADDRESS&&rx==RX_ADDRESS&&span==CAPACITY);
    return (phase==EldenTouchDmaAfter&&m->lose_route_after)||(phase==EldenTouchDmaBefore&&m->lose_route_before)?-19:0;
}
static EldenTouchDmaContract setup(struct geni_spi *engine){
    Model *m=&model;memset(m,0,sizeof(*m));m->epoch=17;
    memset(engine,0,sizeof(*engine));engine->se=(struct geni_io){m,rd,wr,delay};
    engine->use_dma=1;engine->cur_speed_hz=9600000;engine->cur_bpw=8;engine->poll_limit=8;engine->poll_only=2;
    engine->dma=(struct geni_dma){m,clean,clean_inval,inval,m->tx_cpu,m->rx_cpu,TX_ADDRESS,RX_ADDRESS,CAPACITY};
    m->registers[SE_GENI_DMA_MODE_EN/4]=1;m->registers[GENI_IF_DISABLE_RO/4]=1;
    return (EldenTouchDmaContract){m,mapping,m->epoch,ELDEN_TOUCH_SE_DMA_SID,m->staging,CAPACITY,64};
}
static void valid(void){
    struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};
    CHECK(!elden_touch_dma_attach(&t,&s,&c)&&!model.register_writes);
    uint8_t tx[CAPACITY],rx[CAPACITY];for(unsigned i=0;i<CAPACITY;i++)tx[i]=(uint8_t)i;
    const unsigned lengths[]={1,3,6,65,109,4096,15361};
    for(unsigned j=0;j<sizeof(lengths)/sizeof(lengths[0]);j++){
        unsigned n=lengths[j];memset(rx,0xaa,sizeof(rx));
        CHECK(!elden_touch_dma_transfer(&t,tx,rx,n));
        for(unsigned i=0;i<n;i++)CHECK(rx[i]==response(tx[i],i));
        CHECK(rx[n]==0xaa&&!t.quarantined);
    }
    CHECK(!elden_touch_dma_transfer(&t,tx,NULL,3));
    CHECK(!elden_touch_dma_transfer(&t,NULL,rx,109));for(unsigned i=0;i<109;i++)CHECK(rx[i]==response(0,i));
    memcpy(rx,tx,65);CHECK(!elden_touch_dma_transfer(&t,rx,rx,65));for(unsigned i=0;i<65;i++)CHECK(rx[i]==response(tx[i],i));
    CHECK(t.completed==10&&model.tx_clean==9&&model.rx_clean==9&&model.invalidate==9);
}
static void invalid(void){
    for(unsigned case_id=0;case_id<14;case_id++){
        struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};
        if(case_id==0)c.check=NULL;
        if(case_id==1)c.sid=ELDEN_TOUCH_GPI_DMA_SID;
        if(case_id==2)s.dma.inval=NULL;
        if(case_id==3)s.dma.rx_phys=s.dma.tx_phys+32;
        if(case_id==4)s.dma.rx_phys=UINT64_MAX-3;
        if(case_id==5)c.staging=model.rx_cpu;
        if(case_id==6)c.epoch=0;
        if(case_id==7)s.dma.rx_virt=model.tx_cpu;
        if(case_id==8)model.registers[SE_GSI_EVENT_EN/4]=15;
        if(case_id==9)c.cache_line_bytes=63;
        if(case_id==10)++s.dma.rx_virt;
        if(case_id==11)--s.dma.size;
        if(case_id==12)s.cur_bpw=16;
        if(case_id==13)s.last_cs=1;
        CHECK(elden_touch_dma_attach(&t,&s,&c)!=0&&!model.register_writes&&!t.initialized);
    }
    struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};uint8_t tx[110]={0},rx[110];
    CHECK(!elden_touch_dma_attach(&t,&s,&c));unsigned before=model.register_writes;
    CHECK(elden_touch_dma_transfer(&t,tx,rx,CAPACITY+1)==GENI_ERR_PARAM);
    CHECK(elden_touch_dma_transfer(&t,tx,model.staging,10)==GENI_ERR_PARAM);
    CHECK(elden_touch_dma_transfer(&t,model.tx_cpu,rx,10)==GENI_ERR_PARAM);
    CHECK(!model.commands&&model.register_writes==before);
    model.lose_route_before=true;CHECK(elden_touch_dma_transfer(&t,tx,rx,10)==-19&&t.quarantined);
    CHECK(model.register_writes==before);
}
static void failures(void){
    for(unsigned case_id=0;case_id<5;case_id++){
        struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};
        uint8_t tx[109]={0},rx[109],original[109];memset(rx,0xcc,sizeof(rx));memcpy(original,rx,sizeof(rx));
        CHECK(!elden_touch_dma_attach(&t,&s,&c));
        if(case_id==0)model.inject_bus_error=true;
        if(case_id==1)model.stalled=true;
        if(case_id==2)model.lingering_active=true;
        if(case_id==3)model.lose_route_after=true;
        if(case_id==4)model.late_bus_error=true;
        CHECK(elden_touch_dma_transfer(&t,tx,rx,109)!=0&&t.quarantined&&!t.completed);
        CHECK(!memcmp(rx,original,sizeof(rx))); /* never publish failed receive */
        unsigned writes=model.register_writes;
        CHECK(elden_touch_dma_transfer(&t,tx,rx,109)==GENI_ERR_IO&&model.register_writes==writes);
        CHECK(elden_touch_dma_attach(&t,&s,&c)==GENI_ERR_IO);
        if(case_id<2)CHECK(model.cancel==1&&model.abort==1&&model.resets==2);
    }
}
static void changed_configuration(void){
    for(unsigned i=0;i<5;i++){
        struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};
        uint8_t tx[4]={0},rx[4]={1,2,3,4},original[4];memcpy(original,rx,4);
        CHECK(!elden_touch_dma_attach(&t,&s,&c));
        if(i==0)s.cur_speed_hz/=2;
        if(i==1)s.dma.rx_phys+=CAPACITY;
        if(i==2)s.se.ctx=NULL;
        if(i==3)s.dma.inval=NULL;
        if(i==4)s.last_mode=GENI_SPI_CPHA;
        CHECK(elden_touch_dma_transfer(&t,tx,rx,4)==GENI_ERR_IO);
        CHECK(t.quarantined&&!model.commands&&!model.register_writes&&!memcmp(rx,original,4));
    }
}
static void first_word_setup(void){
    struct geni_spi s;EldenTouchDmaContract c=setup(&s);EldenTouchDmaTransfer t={0};
    uint8_t tx[3]={0xff,0x12,0x34},rx[3]={0};s.cur_bpw=0;
    CHECK(!elden_touch_dma_attach(&t,&s,&c));
    CHECK(!elden_touch_dma_transfer(&t,tx,rx,3));
    CHECK(s.cur_bpw==8&&t.qualified_engine.cur_bpw==8&&model.registers[SE_SPI_WORD_LEN/4]==4);
    CHECK(!elden_touch_dma_transfer(&t,tx,rx,3)&&t.completed==2);
    s.cur_bpw=0;unsigned writes=model.register_writes;
    CHECK(elden_touch_dma_transfer(&t,tx,rx,3)==GENI_ERR_IO&&t.quarantined&&model.register_writes==writes);
}
int main(void){valid();invalid();failures();changed_configuration();first_word_setup();printf("Touch SE-DMA wrapper: %u assertions across transfer, cache, mapping and failure scenarios\n",checks);}
