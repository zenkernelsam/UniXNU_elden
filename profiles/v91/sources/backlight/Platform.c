/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Platform.h"
#include "EldenBacklightResources.h"
#define ROOT_ENABLE 2u
#define ROOT_OFF (1u << 31)
#define ACTIVE ((1u << 0) | (1u << 12))
#define STATUS 0x40u
#define GSI_EVENT 0xe18u
#define TX_STATUS 0x800u
#define FIFO_COUNT 0x1ffffffu
#define SECONDARY_RX_LAST (1u << 27)
static const uint32_t branches[]={0x232a0,0x2329c,0x23298,0x232b4,0x1e27c};
static const uint32_t saved[]={GENI_DMA_MODE,GENI_TX_PACK0,GENI_TX_PACK1,
    GENI_RX_PACK0,GENI_RX_PACK1,GENI_BYTE_GRAN,GENI_TX_WATERMARK,
    GENI_M_IRQ_EN,GENI_SE_IRQ_EN,GENI_TX_LEN,GENI_RX_LEN};
static uint32_t rd(EldenBlPlatform *p,enum EldenBlSpace s,uint32_t r){return p->io.read(p->io.context,s,r);}
static void wr(EldenBlPlatform *p,enum EldenBlSpace s,uint32_t r,uint32_t v){p->io.write(p->io.context,s,r,v);}
static void delay(EldenBlPlatform *p,unsigned us){p->io.delay_us(p->io.context,us);}
static int fail(EldenBlPlatform *p,int error,bool quarantine){
    p->error=error;p->failed=true;p->quarantined|=quarantine;return error;
}
static bool idle(EldenBlPlatform *p){
    return !(rd(p,EldenBlSe,STATUS)&ACTIVE) && !rd(p,EldenBlSe,GSI_EVENT) &&
        !rd(p,EldenBlSe,GENI_DMA_MODE) &&
        !(rd(p,EldenBlSe,TX_STATUS)&FIFO_COUNT) &&
        !(rd(p,EldenBlSe,GENI_RX_FIFO_STATUS)&FIFO_COUNT);
}
static int root_wait(EldenBlPlatform *p){
    for(unsigned n=0;n<1000;n++){
        if(!(rd(p,EldenBlGcc,ELDEN_BL_GCC_RCG)&ROOT_OFF))return 0;
        delay(p,1);
    }
    return fail(p,EldenBlTimedOut,true);
}
static int vote(EldenBlPlatform *p,bool restore){
    if(p->io.gcc_lock(p->io.context,true))return fail(p,EldenBlBusy,restore);
    uint32_t live=rd(p,EldenBlGcc,ELDEN_BL_GCC_VOTE),value=live;
    if(live==0xffffffffu){p->io.gcc_lock(p->io.context,false);return fail(p,EldenBlNotReady,false);}
    if(restore){
        /* Bluetooth SE6 shares these core/AHB gates. Never clear an added
         * common vote while any sibling SE still has an AP vote. */
        uint32_t mask=p->added_votes;
        if(live&ELDEN_BL_GCC_SIBLINGS)mask&=~ELDEN_BL_GCC_COMMON;
        value=live&~mask;
    }else{
        p->vote=live;
        uint32_t mask=ELDEN_BL_GCC_COMMON|ELDEN_BL_GCC_SE_VOTE;
        p->added_votes=mask&~live;value=live|mask;
    }
    if(value!=live)wr(p,EldenBlGcc,ELDEN_BL_GCC_VOTE,value);
    bool good=rd(p,EldenBlGcc,ELDEN_BL_GCC_VOTE)==value;
    int unlock=p->io.gcc_lock(p->io.context,false);
    if(!good||unlock)return fail(p,EldenBlTransport,true);
    p->votes_changed=!restore;return 0;
}
static uint32_t se_read(void *c,uint32_t r){return rd(c,EldenBlSe,r);}
static void se_write(void *c,uint32_t r,uint32_t v){
    /* This adapter polls. Keep the physical IRQ output masked; status bits
     * still feed the canonical state machine. No unhandled level interrupt. */
    wr(c,EldenBlSe,r,r==GENI_SE_IRQ_EN?0:v);
}
int elden_bl_platform_release(EldenBlPlatform *p){
    if(!p||!p->io.read||!p->io.write||!p->io.gcc_lock)return EldenBlBadArgument;
    if(p->quarantined)return p->error?p->error:EldenBlTransport;
    if(p->engine_changed){
        if(!idle(p)||geni_i2c_busy(&p->bus))return fail(p,EldenBlTransport,true);
        wr(p,EldenBlSe,GENI_SE_IRQ_EN,0);
        uint32_t status=rd(p,EldenBlSe,GENI_M_IRQ_STATUS);
        if(status)wr(p,EldenBlSe,GENI_M_IRQ_CLEAR,status);
        for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)
            if(saved[i]!=GENI_SE_IRQ_EN)wr(p,EldenBlSe,saved[i],p->engine[i]);
        for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)
            if(saved[i]!=GENI_SE_IRQ_EN&&rd(p,EldenBlSe,saved[i])!=p->engine[i])return fail(p,EldenBlTransport,true);
        wr(p,EldenBlSe,GENI_SE_IRQ_EN,p->engine[8]);
        if(rd(p,EldenBlSe,GENI_SE_IRQ_EN)!=p->engine[8])return fail(p,EldenBlTransport,true);
        p->engine_changed=false;
    }
    if(p->votes_changed&&vote(p,true))return p->error;
    if(p->root_changed){
        uint32_t live=rd(p,EldenBlGcc,ELDEN_BL_GCC_RCG);
        wr(p,EldenBlGcc,ELDEN_BL_GCC_RCG,(live&~ROOT_ENABLE)|(p->rcg&ROOT_ENABLE));
        if((rd(p,EldenBlGcc,ELDEN_BL_GCC_RCG)&ROOT_ENABLE)!=(p->rcg&ROOT_ENABLE))return fail(p,EldenBlTransport,true);
        p->root_changed=false;
    }
    p->acquired=false;return 0;
}
static int abandon(EldenBlPlatform *p,int error){
    fail(p,error,false);int released=elden_bl_platform_release(p);
    return released?released:error;
}
int elden_bl_platform_acquire(EldenBlPlatform *p){
    if(!p||!p->io.read||!p->io.write||!p->io.now_us||!p->io.delay_us||!p->io.gcc_lock||
       p->acquired||p->failed||p->quarantined||p->root_changed||p->votes_changed)return EldenBlBadArgument;
    p->stage=1;
    p->rcg=rd(p,EldenBlGcc,ELDEN_BL_GCC_RCG);p->rcg_cfg=rd(p,EldenBlGcc,ELDEN_BL_GCC_RCG+4);
    if(p->rcg==0xffffffffu||p->rcg_cfg==0xffffffffu||(p->rcg&1))return fail(p,EldenBlBusy,false);
    /* Preserve the boot loader's exact source/divider and SCL timings. */
    p->root_changed=true;wr(p,EldenBlGcc,ELDEN_BL_GCC_RCG,p->rcg|ROOT_ENABLE);
    if(root_wait(p))return p->error;
    if(vote(p,false))return p->quarantined?p->error:abandon(p,p->error);
    for(unsigned i=0;i<sizeof(branches)/sizeof(branches[0]);i++){
        bool good=false;
        for(unsigned n=0;n<1000;n++){
            uint32_t v=rd(p,EldenBlGcc,branches[i]);
            /* AHB hardware clock gating can report HALT while idle. */
            if(v!=0xffffffffu&&(!(v&ROOT_OFF)||(v&2))){good=true;break;}
            delay(p,1);
        }
        if(!good)return abandon(p,EldenBlNotReady);
    }
    p->stage=2;
    for(unsigned i=0;i<2;i++){
        p->pins[i]=rd(p,EldenBlPins,i*ELDEN_BL_GPIO_STRIDE);
        if(((p->pins[i]>>2)&15)!=ELDEN_BL_PIN_FUNCTION||(p->pins[i]&(1u<<9)))return abandon(p,EldenBlNotReady);
    }
    p->pins[2]=rd(p,EldenBlEnable,0);p->hwen=rd(p,EldenBlEnable,4);
    if((p->pins[2]&(15u<<2))||!(p->pins[2]&(1u<<9))||!(p->hwen&2))return abandon(p,EldenBlNotReady);
    p->stage=3;
    p->last_status=rd(p,EldenBlSe,GENI_M_IRQ_STATUS);
    p->entry_status=rd(p,EldenBlSe,STATUS);p->entry_gsi=rd(p,EldenBlSe,GSI_EVENT);
    p->entry_dma=rd(p,EldenBlSe,GENI_DMA_MODE);p->entry_tx=rd(p,EldenBlSe,TX_STATUS);
    p->entry_rx=rd(p,EldenBlSe,GENI_RX_FIFO_STATUS);p->entry_secondary=rd(p,EldenBlSe,0x640);
    p->entry_interface=rd(p,EldenBlSe,GENI_IF_DISABLE);p->entry_revision=rd(p,EldenBlSe,GENI_FW_REVISION);
    p->entry_clock=rd(p,EldenBlSe,GENI_CLK_CFG);p->entry_scl=rd(p,EldenBlSe,GENI_SCL_COUNTERS);
    /* S_RX_FIFO_LAST can remain latched after the boot loader's transfer.
     * The hardware active bits, empty FIFOs and disabled DMA/GSI establish
     * idleness. Preserve that event, rather than clearing a foreign IRQ.
     * Any other secondary status still refuses takeover. */
    if(!idle(p)||(rd(p,EldenBlSe,0x640)&~SECONDARY_RX_LAST)||
       (p->last_status&(GENI_M_ERRORS|GENI_M_CMD_DONE|GENI_M_CMD_CANCEL|GENI_M_CMD_ABORT)))return abandon(p,EldenBlBusy);
    if(((rd(p,EldenBlSe,GENI_FW_REVISION)>>8)&255)!=GENI_PROTO_I2C||
       (rd(p,EldenBlSe,GENI_IF_DISABLE)&1)||!(rd(p,EldenBlSe,GENI_CLK_CFG)&1)||
       !rd(p,EldenBlSe,GENI_SCL_COUNTERS))return abandon(p,EldenBlNotReady);
    for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)p->engine[i]=rd(p,EldenBlSe,saved[i]);
    p->stage=4;p->engine_changed=true;wr(p,EldenBlSe,GENI_SE_IRQ_EN,0);
    GeniIo io={se_read,se_write,p};
    if(geni_i2c_prepare(&p->bus,&io,ELDEN_BL_ADDRESS,&p->config))return abandon(p,EldenBlNotReady);
    if(rd(p,EldenBlSe,GENI_SE_IRQ_EN)||rd(p,EldenBlSe,GENI_TX_PACK0)!=0x7f8fe||
       rd(p,EldenBlSe,GENI_RX_PACK0)!=0x7f8fe)return fail(p,EldenBlTransport,true);
    p->stage=5;p->acquired=true;return 0;
}
static int transfer(EldenBlPlatform *p,uint8_t reg,uint8_t *value,bool writing){
    if(!p||!value||!p->acquired||p->failed||p->quarantined)return EldenBlBadArgument;
    if(writing ? (reg!=4&&reg!=5&&reg!=0x0d) :
        (reg!=0&&reg!=4&&reg!=5&&reg!=7&&reg!=0x0d&&reg!=0x10&&reg!=0x11&&reg!=0x12&&reg!=0x13))return EldenBlBadArgument;
    if((rd(p,EldenBlEnable,0)&((15u<<2)|(1u<<9)))!=(1u<<9)||
       !(rd(p,EldenBlEnable,4)&2))return fail(p,EldenBlNotReady,false);
    if(!idle(p))return fail(p,EldenBlBusy,true);
    int rc=writing?geni_i2c_start_write(&p->bus,reg,*value):geni_i2c_start_read(&p->bus,reg);
    if(rc)return fail(p,EldenBlBusy,true);
    uint64_t start=p->io.now_us(p->io.context);unsigned last_phase=p->bus.phase;
    for(unsigned n=0;n<6000;n++){
        rc=geni_i2c_poll(&p->bus);
        uint64_t now=p->io.now_us(p->io.context);
        if(rc!=GeniPending)break;
        if(last_phase!=p->bus.phase){start=now;last_phase=p->bus.phase;}
        if(now-start>=10000){rc=geni_i2c_timeout(&p->bus);start=now;last_phase=p->bus.phase;}
        if(rc!=GeniPending)break;
        delay(p,10);
    }
    p->last_status=p->bus.error_status;
    /* Software Idle after an unacknowledged abort is not hardware Idle. */
    bool quiet=false;
    for(unsigned n=0;n<1000;n++){if(idle(p)){quiet=true;break;}delay(p,1);}
    if(rc!=GeniDone||!quiet)return fail(p,rc==GeniPending||p->bus.error==GeniErrTimeout?EldenBlTimedOut:EldenBlTransport,!quiet||geni_i2c_busy(&p->bus));
    ++p->transfers;if(!writing)*value=p->bus.data;return 0;
}
int elden_bl_platform_read(void *p,uint8_t reg,uint8_t *value){return transfer(p,reg,value,false);}
int elden_bl_platform_write(void *p,uint8_t reg,uint8_t value){return transfer(p,reg,&value,true);}
