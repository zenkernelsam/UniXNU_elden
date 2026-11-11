/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Elden SE4 ownership adapter. Register/clock/pin facts are from the pinned
 * h0cheung 6f97250e gcc-sm8750, clk-rcg2, pinctrl-sm8750 and elden DTS.
 * Uses the local shared RCG, a 19.2MHz TCXO parent and <=9.6MHz SPI; never
 * programs sheng's SM8550 DFS clock registers or a shared PLL.
 */
#include "Platform.h"
#include "EldenTouchBoard.h"
#define CMD_UPDATE 1u
#define CMD_ROOT_EN 2u
#define CMD_ROOT_OFF (1u<<31)
#define CFG_RATE_MASK ((7u<<8)|(3u<<12)|31u)
#define PIN_MASK (3u|(15u<<2)|(7u<<6)|(1u<<9))
#define ACTIVE_MASK ((1u<<0)|(1u<<12))
static const uint32_t branches[]={ELDEN_TOUCH_GCC_HALT_CORE_2X,ELDEN_TOUCH_GCC_HALT_CORE,
    ELDEN_TOUCH_GCC_HALT_M_AHB,ELDEN_TOUCH_GCC_HALT_S_AHB,ELDEN_TOUCH_GCC_HALT_S4};
/* qcom-geni-se.c IRQ enable registers and DMA pointer/attribute state.
 * LEN and FSM_RESET are trigger registers and are deliberately excluded. */
static const uint32_t dma_config[]={0xc48,0xd48,SE_DMA_TX_PTR_L,SE_DMA_TX_PTR_H,SE_DMA_TX_ATTR,
    SE_DMA_RX_PTR_L,SE_DMA_RX_PTR_H,SE_DMA_RX_ATTR};
static const uint32_t config[]={GENI_OUTPUT_CTRL,SE_GENI_CGC_CTRL,GENI_SER_M_CLK_CFG,SE_GENI_CLK_SEL,
    SE_GENI_BYTE_GRAN,SE_GENI_DMA_MODE_EN,SE_GENI_TX_PACKING_CFG0,SE_GENI_TX_PACKING_CFG1,
    SE_GENI_RX_PACKING_CFG0,SE_GENI_RX_PACKING_CFG1,SE_SPI_CPHA,SE_SPI_LOOPBACK,SE_SPI_CPOL,
    SE_SPI_DEMUX_OUTPUT_INV,SE_SPI_DEMUX_SEL,SE_SPI_TRANS_CFG,SE_SPI_WORD_LEN,
    SE_GENI_M_IRQ_EN,SE_GENI_S_IRQ_EN,SE_GENI_TX_WATERMARK_REG,SE_GENI_RX_WATERMARK_REG,
    SE_GENI_RX_RFR_WATERMARK_REG,SE_IRQ_EN,SE_DMA_GENERAL_CFG,SE_GSI_EVENT_EN,
    GENI_FORCE_DEFAULT_REG,SE_SPI_TX_TRANS_LEN,SE_SPI_RX_TRANS_LEN};
static uint32_t rd(EldenTouchPlatform *p,enum EldenTouchSpace space,uint32_t reg){return p->io.read(p->io.context,space,reg);}
static void wr(EldenTouchPlatform *p,enum EldenTouchSpace space,uint32_t reg,uint32_t value){p->io.write(p->io.context,space,reg,value);}
static void delay(EldenTouchPlatform *p,unsigned us){p->io.delay(p->io.context,us);}
static int fail(EldenTouchPlatform *p,int error,bool quarantine){p->error=error;p->quarantined|=quarantine;return error;}
static int wait(EldenTouchPlatform *p,uint32_t reg,uint32_t mask,bool set){
    for(unsigned n=0;n<500;n++){
        if((rd(p,EldenTouchGcc,reg)&mask)==(set?mask:0))return 0;
        delay(p,1);
    }
    return fail(p,GENI_ERR_TIMEOUT,true);
}
static uint32_t se_read(void *context,uint32_t reg){return rd(context,EldenTouchSe,reg);}
static void se_write(void *context,uint32_t reg,uint32_t value){
    /* PIO completions are polled. Suppress the temporary interrupt-enable
     * writes in the reusable core as well as its final interrupt disable.
     */
    EldenTouchPlatform *p=context;
    wr(p,EldenTouchSe,reg,reg==SE_IRQ_EN&&!p->dma_mode?0:value);
}
static void se_delay(void *context,unsigned us){delay(context,us);}
static uint32_t qup_read(void *context,uint32_t reg){return rd(context,EldenTouchQup,reg);}
static int update(EldenTouchPlatform *p){
    wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG,rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG)|CMD_UPDATE);
    return wait(p,ELDEN_TOUCH_GCC_S4_RCG,CMD_UPDATE,false);
}
int elden_touch_platform_release(EldenTouchPlatform *p){
    if(!p||!p->io.read||!p->io.write||!p->io.delay)return GENI_ERR_PARAM;
    if(p->quarantined)return GENI_ERR_IO;
    if(p->touched_engine){
        if((rd(p,EldenTouchSe,SE_GENI_STATUS)&ACTIVE_MASK)||rd(p,EldenTouchSe,SE_GSI_EVENT_EN))
            return fail(p,GENI_ERR_IO,true);
        if(p->dma_mode){
            if(rd(p,EldenTouchSe,SE_GENI_TX_FIFO_STATUS)||rd(p,EldenTouchSe,SE_GENI_RX_FIFO_STATUS)||
               rd(p,EldenTouchSe,SE_DMA_TX_IRQ_STAT)||rd(p,EldenTouchSe,SE_DMA_RX_IRQ_STAT))return fail(p,GENI_ERR_IO,true);
            wr(p,EldenTouchSe,0xc50,0xffffffffu);wr(p,EldenTouchSe,0xd50,0xffffffffu);
            for(unsigned i=2;i<8;i++)wr(p,EldenTouchSe,dma_config[i],p->dma_saved[i]);
            wr(p,EldenTouchSe,SE_DMA_TX_IRQ_EN_SET,p->dma_saved[0]);
            wr(p,EldenTouchSe,SE_DMA_RX_IRQ_EN_SET,p->dma_saved[1]);
            for(unsigned i=0;i<8;i++)if(rd(p,EldenTouchSe,dma_config[i])!=p->dma_saved[i])return fail(p,GENI_ERR_IO,true);
        }
        wr(p,EldenTouchSe,SE_IRQ_EN,0);
        wr(p,EldenTouchSe,SE_GENI_M_IRQ_CLEAR,0xffffffffu);
        wr(p,EldenTouchSe,SE_GENI_S_IRQ_CLEAR,0xffffffffu);
        /* Restore interrupt outputs last, after the original configuration. */
        for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);i++)if(config[i]!=SE_IRQ_EN)wr(p,EldenTouchSe,config[i],p->engine[i]);
        for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);i++)if(config[i]!=SE_IRQ_EN&&rd(p,EldenTouchSe,config[i])!=p->engine[i])
            return fail(p,GENI_ERR_IO,true);
        wr(p,EldenTouchSe,SE_IRQ_EN,p->engine[22]);
        if(rd(p,EldenTouchSe,SE_IRQ_EN)!=p->engine[22])return fail(p,GENI_ERR_IO,true);
        p->touched_engine=false;
    }
    if(p->touched_pins){
        for(unsigned i=0;i<4;i++){
            uint32_t reg=i*ELDEN_TOUCH_GPIO_STRIDE,live=rd(p,EldenTouchPins,reg);
            wr(p,EldenTouchPins,reg,(live&~PIN_MASK)|(p->pin[i]&PIN_MASK));
            if((rd(p,EldenTouchPins,reg)&PIN_MASK)!=(p->pin[i]&PIN_MASK))return fail(p,GENI_ERR_IO,true);
        }
        uint32_t live=rd(p,EldenTouchIrq,0);
        wr(p,EldenTouchIrq,0,(live&~PIN_MASK)|(p->irq_pin&PIN_MASK));
        if((rd(p,EldenTouchIrq,0)&PIN_MASK)!=(p->irq_pin&PIN_MASK))return fail(p,GENI_ERR_IO,true);
        p->touched_pins=false;
    }
    if(p->touched_cfg){
        uint32_t live=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4);
        wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4,(live&~CFG_RATE_MASK)|(p->original_cfg&CFG_RATE_MASK));
        if(update(p))return p->error;
        if((rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4)&CFG_RATE_MASK)!=(p->original_cfg&CFG_RATE_MASK))
            return fail(p,GENI_ERR_IO,true);
        p->touched_cfg=false;
    }
    if(p->touched_clock){
        uint32_t live=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_VOTE);
        wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_VOTE,(live&~ELDEN_TOUCH_GCC_VOTE_MASK)|(p->original_vote&ELDEN_TOUCH_GCC_VOTE_MASK));
        if((rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_VOTE)&ELDEN_TOUCH_GCC_VOTE_MASK)!=(p->original_vote&ELDEN_TOUCH_GCC_VOTE_MASK))
            return fail(p,GENI_ERR_IO,true);
        live=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG);
        wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG,(live&~CMD_ROOT_EN)|(p->original_cmd&CMD_ROOT_EN));
        if((rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG)&CMD_ROOT_EN)!=(p->original_cmd&CMD_ROOT_EN))
            return fail(p,GENI_ERR_IO,true);
        p->touched_clock=false;
    }
    p->acquired=false;return 0;
}
static int abandon(EldenTouchPlatform *p,int error){
    p->error=error;
    if(!p->quarantined&&elden_touch_platform_release(p))return p->error;
    return error;
}
static int configure(EldenTouchPlatform *p,bool dma){
    if(!dma&&p->spi.info.fifo_disabled)return abandon(p,GENI_ERR_NOTSUP);
    if(p->spi.info.tx_fifo_depth<4||p->spi.info.fifo_width_bits!=32)return abandon(p,GENI_ERR_NODEV);
    if((rd(p,EldenTouchSe,SE_GENI_STATUS)&ACTIVE_MASK)||
       (!dma&&rd(p,EldenTouchSe,SE_GSI_EVENT_EN))||
       rd(p,EldenTouchSe,SE_GENI_TX_FIFO_STATUS)||rd(p,EldenTouchSe,SE_GENI_RX_FIFO_STATUS))return abandon(p,GENI_ERR_IO);
    if(dma){
        if(p->entry_gsi!=15||p->entry_dma!=1||
           rd(p,EldenTouchSe,SE_GSI_EVENT_EN)!=15||rd(p,EldenTouchSe,SE_GENI_DMA_MODE_EN)!=1||
           rd(p,EldenTouchSe,SE_DMA_TX_IRQ_STAT)||rd(p,EldenTouchSe,SE_DMA_RX_IRQ_STAT)||
           !p->spi.dma.tx_virt||!p->spi.dma.rx_virt||!p->spi.dma.tx_phys||!p->spi.dma.rx_phys||
           !p->spi.dma.clean||!p->spi.dma.clean_inval||!p->spi.dma.inval||!p->spi.dma.size)
            return abandon(p,GENI_ERR_IO);
        for(unsigned i=0;i<8;i++)p->dma_saved[i]=rd(p,EldenTouchSe,dma_config[i]);
    }
    for(unsigned i=0;i<4;i++){
        p->pin[i]=rd(p,EldenTouchPins,i*ELDEN_TOUCH_GPIO_STRIDE);
        if(((p->pin[i]>>2)&15)>ELDEN_TOUCH_GPIO_FUNC)return abandon(p,GENI_ERR_IO);
    }
    p->irq_pin=rd(p,EldenTouchIrq,0);
    if((p->irq_pin&(15u<<2))||(p->irq_pin&(1u<<9)))return abandon(p,GENI_ERR_IO);
    for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);i++)p->engine[i]=rd(p,EldenTouchSe,config[i]);
    p->stage=3;p->touched_cfg=true;
    wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4,(p->original_cfg&~CFG_RATE_MASK)|1u);
    if(update(p))return p->error;
    if((rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4)&CFG_RATE_MASK)!=1u)return fail(p,GENI_ERR_IO,true);
    p->touched_pins=true;
    for(unsigned i=0;i<4;i++){
        /* sm8750.dtsi qup_spi4_{cs,data_clk}: 6mA, bias-disable. */
        uint32_t value=(p->pin[i]&~PIN_MASK)|(ELDEN_TOUCH_GPIO_FUNC<<2)|((ELDEN_TOUCH_SPI_DRIVE_MA/2-1)<<6);
        wr(p,EldenTouchPins,i*ELDEN_TOUCH_GPIO_STRIDE,value);
        if(rd(p,EldenTouchPins,i*ELDEN_TOUCH_GPIO_STRIDE)!=value)return fail(p,GENI_ERR_IO,true);
    }
    /* gpio162 remains an input; the worker polls it and never programs GIC
     * SPI162, interrupt routing/enable or W1C status registers.
     */
    uint32_t irq=(p->irq_pin&~PIN_MASK)|((ELDEN_TOUCH_IRQ_DRIVE_MA/2-1)<<6)|3u;
    wr(p,EldenTouchIrq,0,irq);
    if(rd(p,EldenTouchIrq,0)!=irq)return fail(p,GENI_ERR_IO,true);
    p->stage=4;p->dma_mode=dma;p->spi.poll_only=dma?2:1;p->spi.poll_limit=50000;
    p->spi.allow_fifo_disabled=0;p->spi.use_dma=dma;p->touched_engine=true;
    int error=geni_spi_init(&p->spi);if(error)return abandon(p,error);
    error=geni_spi_setup(&p->spi,0,0);if(error)return abandon(p,error);
    error=geni_spi_set_clock(&p->spi,ELDEN_TOUCH_SPI_MAX_HZ,19200000,0);if(error)return abandon(p,error);
    p->acquired=true;p->stage=5;return 0;
}
int elden_touch_platform_acquire(EldenTouchPlatform *p){
    if(!p||!p->io.read||!p->io.write||!p->io.delay||p->acquired||p->quarantined||p->touched_clock)return GENI_ERR_PARAM;
    p->stage=1;p->error=0;
    p->original_vote=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_VOTE);
    p->original_cmd=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG);
    p->original_cfg=rd(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG+4);
    if(p->original_cmd&CMD_UPDATE)return fail(p,GENI_ERR_IO,false);
    /* First wake the original source without changing its frequency, then
     * qualify idle/FIFO state before changing this SE's source to TCXO.
     */
    p->touched_clock=true;
    wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_S4_RCG,p->original_cmd|CMD_ROOT_EN);
    if(wait(p,ELDEN_TOUCH_GCC_S4_RCG,CMD_ROOT_OFF,false))return p->error;
    wr(p,EldenTouchGcc,ELDEN_TOUCH_GCC_VOTE,p->original_vote|ELDEN_TOUCH_GCC_VOTE_MASK);
    for(unsigned i=0;i<sizeof(branches)/sizeof(branches[0]);i++)if(wait(p,branches[i],ELDEN_TOUCH_GCC_CLK_OFF,false))return p->error;
    p->stage=2;
    p->spi.se=(struct geni_io){p,se_read,se_write,se_delay};
    p->spi.wrapper=(struct geni_io){p,qup_read,NULL,NULL};
    int error=geni_spi_read_info(&p->spi);if(error)return abandon(p,error);
    if(p->spi.info.protocol!=GENI_PROTO_SPI)return abandon(p,GENI_ERR_PROTO);
    /* Capture the inherited transport before rejecting FIFO-disabled mode.
     * This never touches firmware-only0x2008 or starts a DMA/SPI command. */
    p->entry_gsi=rd(p,EldenTouchSe,SE_GSI_EVENT_EN);p->entry_dma=rd(p,EldenTouchSe,SE_GENI_DMA_MODE_EN);
    p->entry_tx=rd(p,EldenTouchSe,SE_GENI_TX_FIFO_STATUS);p->entry_rx=rd(p,EldenTouchSe,SE_GENI_RX_FIFO_STATUS);
    p->entry_m_irq=rd(p,EldenTouchSe,SE_GENI_M_IRQ_STATUS);p->entry_s_irq=rd(p,EldenTouchSe,0x640);
    p->entry_dma_tx_status=rd(p,EldenTouchSe,SE_DMA_TX_IRQ_STAT);p->entry_dma_rx_status=rd(p,EldenTouchSe,SE_DMA_RX_IRQ_STAT);
    p->entry_dma_tx_in=rd(p,EldenTouchSe,SE_DMA_TX_LEN_IN);p->entry_dma_rx_in=rd(p,EldenTouchSe,SE_DMA_RX_LEN_IN);
    if(p->survey_only)return 0;
    return configure(p,false);
}
int elden_touch_platform_activate_dma(EldenTouchPlatform *p){
    if(!p||p->stage!=2||!p->survey_only||!p->touched_clock||p->acquired||p->quarantined)return GENI_ERR_PARAM;
    return configure(p,true);
}

int elden_touch_platform_transfer(EldenTouchPlatform *p,const uint8_t *tx,uint8_t *rx,size_t size){
    if(!p||!p->acquired||p->quarantined||p->dma_mode)return GENI_ERR_IO;
    int error=geni_spi_transfer(&p->spi,tx,rx,size,0);
    if(error)return fail(p,error,true); /* no reuse, unmap or clock removal */
    return 0;
}
