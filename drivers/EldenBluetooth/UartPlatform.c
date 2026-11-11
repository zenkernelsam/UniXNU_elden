/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Register facts and 1x8 packing from exact TB322FC Linux6f97250e,
 * qcom-geni-se / qcom_geni_serial / gcc-sm8750. Canonical BtGeni is retained
 * for read-only discovery, basic setup and receiver commands.
 */
#include "UartPlatform.h"
#include "EldenBluetoothBoard.h"
#define ROOT_EN 2u
#define ROOT_OFF (1u<<31)
#define UPDATE 1u
#define RATE_MASK 0x371fu
#define PIN_MASK (3u|(15u<<2)|(7u<<6)|(1u<<9))
#define GLOBAL_IRQ 0xe1cu
#define PACK_1X8 0x0fu
#define OTHER_SE_VOTES ((0xffu<<4)&~(1u<<10))
static const uint32_t halts[]={ELDEN_BT_HALT_M_AHB,ELDEN_BT_HALT_S_AHB,
    ELDEN_BT_HALT_CORE,ELDEN_BT_HALT_CORE_2X,ELDEN_BT_HALT_SE6};
static const uint32_t config[]={PB_SE_CGC_CTRL,PB_SE_DMA_GENERAL_CFG,PB_SE_OUTPUT_CTRL,
    PB_SE_FORCE_DEFAULT,PB_SE_GSI_EVENT_EN,PB_SE_DMA_MODE,PB_SE_TX_PACK0,PB_SE_TX_PACK1,
    PB_SE_RX_PACK0,PB_SE_RX_PACK1,PB_SE_BYTE_GRAN,PB_SE_TX_WATERMARK,
    PB_SE_RX_WATERMARK,PB_SE_RFR_WATERMARK,PB_SE_M_IRQ_EN,PB_SE_S_IRQ_EN,
    PB_SE_UART_LOOPBACK,PB_SE_UART_TX_TRANS_CFG,PB_SE_UART_TX_PARITY,
    PB_SE_UART_RX_TRANS_CFG,PB_SE_UART_RX_PARITY,PB_SE_UART_TX_WORD_LEN,
    PB_SE_UART_RX_WORD_LEN,PB_SE_UART_TX_STOP_LEN,PB_SE_UART_RX_STALE,
    PB_SE_UART_MANUAL_RFR,PB_SE_CLK_SEL,PB_SE_M_CLK_CFG,PB_SE_S_CLK_CFG,
    PB_SE_UART_TX_TRANS_LEN,GLOBAL_IRQ};
typedef struct {uint32_t baud,hz;unsigned source,m,n,div;} Rate;
static const Rate rates[]={{115200,7372800,6,384,15625,4},
                          {8000000,128000000,1,16,75,1}};
static uint32_t rd(EldenWcnUart *u,EldenWcnUartSpace space,uint32_t reg){
    return u->io.read(u->io.context,space,reg);
}
static void wr(EldenWcnUart *u,EldenWcnUartSpace space,uint32_t reg,uint32_t value){
    u->io.write(u->io.context,space,reg,value);
}
static int fail(EldenWcnUart *u,int error,bool uncertain){
    u->error=error;u->quarantined|=uncertain;return error;
}
static int live(EldenWcnUart *u){
    if(!u||!u->acquired||u->quarantined)return EldenWcnUartProtocol;
    int rc=u->io.admit(u->io.context,EldenWcnUartLive);
    return rc?fail(u,rc,true):0;
}
static uint32_t se_rd(void *context,uint32_t reg){return rd(context,EldenWcnUartSe,reg);}
static void se_wr(void *context,uint32_t reg,uint32_t value){wr(context,EldenWcnUartSe,reg,value);}
static void delay(void *context,unsigned us){EldenWcnUart *u=context;u->io.delay_us(u->io.context,us);}
static void sleep_ms(void *context,unsigned ms){delay(context,ms*1000u);}
static uint64_t now_ms(void *context){EldenWcnUart *u=context;return u->io.now_us(u->io.context)/1000;}
static int wait_bits(EldenWcnUart *u,EldenWcnUartSpace space,uint32_t reg,uint32_t mask,bool on){
    for(unsigned n=0;n<1000;n++){
        if((rd(u,space,reg)&mask)==(on?mask:0))return 0;
        delay(u,10);
    }
    return fail(u,EldenWcnUartTimeout,true);
}
static int vote(EldenWcnUart *u,bool restore){
    int rc=u->io.gcc_lock(u->io.context,true);if(rc)return fail(u,rc,true);
    uint32_t current=rd(u,EldenWcnUartGcc,ELDEN_BT_GCC_VOTE),mask=ELDEN_BT_GCC_VOTE_MASK;
    if(restore&&(current&OTHER_SE_VOTES))mask&=~0xfu; /* keep a live sibling wrapper */
    uint32_t value=restore?(current&~mask)|(u->vote&mask):current|mask;
    wr(u,EldenWcnUartGcc,ELDEN_BT_GCC_VOTE,value);
    bool good=(rd(u,EldenWcnUartGcc,ELDEN_BT_GCC_VOTE)&mask)==(value&mask);
    int unlocked=u->io.gcc_lock(u->io.context,false);
    if(!good||unlocked)return fail(u,unlocked?unlocked:EldenWcnUartClock,true);
    return 0;
}
static int apply_rate(EldenWcnUart *u,const Rate *r){
    uint32_t parent=u->io.parent_hz(u->io.context,r->source);
    if(!parent||(uint64_t)parent*r->m/r->n!=r->hz)return EldenWcnUartClock;
    uint32_t m=r->m,n=~(r->n-r->m)&65535u,d=~r->n&65535u;
    uint32_t cfg=(r->source<<8)|(2u<<12)|1u;
    uint32_t dfs=rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+0x14);
    if(dfs&1){
        for(unsigned level=0;level<8;level++){
            if((rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+0x1c+4*level)&RATE_MASK)==cfg&&
               (rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+0x5c+4*level)&65535u)==m&&
               (rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+0x9c+4*level)&65535u)==n){
                u->level=level;return 0;
            }
        }
        return EldenWcnUartClock;
    }
    u->rate_changed=true;
    const uint32_t values[]={cfg,m,n,d};
    for(unsigned i=0;i<4;i++){
        uint32_t reg=ELDEN_BT_RCG+4+4*i,mask=i?65535u:RATE_MASK;
        wr(u,EldenWcnUartGcc,reg,(rd(u,EldenWcnUartGcc,reg)&~mask)|(values[i]&mask));
    }
    wr(u,EldenWcnUartGcc,ELDEN_BT_RCG,rd(u,EldenWcnUartGcc,ELDEN_BT_RCG)|UPDATE);
    if(wait_bits(u,EldenWcnUartGcc,ELDEN_BT_RCG,UPDATE,false))return u->error;
    for(unsigned i=0;i<4;i++){
        uint32_t mask=i?65535u:RATE_MASK;
        if((rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+4+4*i)&mask)!=(values[i]&mask))
            return fail(u,EldenWcnUartClock,true);
    }
    u->level=0;return 0;
}
static int stop_rx(EldenWcnUart *u){
    if(!u->engine.rx_running)return 0;
    if(pb_geni_rx_stop(&u->engine)||
       (rd(u,EldenWcnUartSe,PB_SE_STATUS)&PB_STATUS_S_ACTIVE))
        return fail(u,EldenWcnUartProtocol,true);
    return 0;
}
int elden_wcn_uart_acquire(EldenWcnUart *u){
    if(!u||!u->io.read||!u->io.write||!u->io.now_us||!u->io.delay_us||
       !u->io.admit||!u->io.parent_hz||!u->io.gcc_lock||
       u->acquired||u->quarantined||u->clock_changed)return EldenWcnUartArgument;
    int rc=u->io.admit(u->io.context,EldenWcnUartAcquire);if(rc)return rc;
    /* Qualify both exact parents before the first register write. */
    if(u->io.parent_hz(u->io.context,1)!=600000000||
       u->io.parent_hz(u->io.context,6)!=300000000)return EldenWcnUartClock;
    u->stage=1;u->vote=rd(u,EldenWcnUartGcc,ELDEN_BT_GCC_VOTE);
    for(unsigned i=0;i<5;i++)u->rcg[i]=rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+4*i);
    if(u->rcg[0]&UPDATE)return EldenWcnUartBusy;
    u->clock_changed=true;
    wr(u,EldenWcnUartGcc,ELDEN_BT_RCG,u->rcg[0]|ROOT_EN);
    if(wait_bits(u,EldenWcnUartGcc,ELDEN_BT_RCG,ROOT_OFF,false)||vote(u,false))return u->error;
    for(unsigned i=0;i<5;i++){
        uint32_t value=rd(u,EldenWcnUartGcc,halts[i]);
        if((value&ROOT_OFF)&&!(value&2u)&&wait_bits(u,EldenWcnUartGcc,halts[i],ROOT_OFF,false))return u->error;
    }
    PbGeniIo io={u,se_rd,se_wr,delay,sleep_ms,now_ms};PbGeniInfo info={0};
    rc=pb_geni_probe(&u->engine,&io,&info);u->stage=2;
    unsigned width=(info.param0>>24)&63u,depth=(info.param0>>16)&255u,rx=(info.param1>>16)&255u;
    if(rc||width!=32||depth<16||depth>128||rx<16||rx>128||
       (info.status&(PB_STATUS_M_ACTIVE|PB_STATUS_S_ACTIVE))||
       rd(u,EldenWcnUartSe,PB_SE_GSI_EVENT_EN)||rd(u,EldenWcnUartSe,PB_SE_DMA_MODE)||
       (rd(u,EldenWcnUartSe,PB_SE_TX_FIFO_STATUS)&PB_TX_FIFO_WC)||
       (rd(u,EldenWcnUartSe,PB_SE_RX_FIFO_STATUS)&PB_RX_FIFO_WC))
        return fail(u,rc?rc:EldenWcnUartBusy,false);
    for(unsigned i=0;i<4;i++){
        u->pin[i]=rd(u,EldenWcnUartPins,i*ELDEN_BT_GPIO_STRIDE);
        if(((u->pin[i]>>2)&15u)>ELDEN_BT_GPIO_FUNC)return fail(u,EldenWcnUartBusy,false);
    }
    for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);i++)u->configuration[i]=rd(u,EldenWcnUartSe,config[i]);
    if((rc=apply_rate(u,&rates[0])))return fail(u,rc,u->rate_changed);
    u->pins_changed=true;
    for(unsigned i=0;i<4;i++){
        uint32_t value=(u->pin[i]&~PIN_MASK)|(ELDEN_BT_GPIO_FUNC<<2)|(i<2?1u:3u);
        wr(u,EldenWcnUartPins,i*ELDEN_BT_GPIO_STRIDE,value);
        if((rd(u,EldenWcnUartPins,i*ELDEN_BT_GPIO_STRIDE)&PIN_MASK)!=(value&PIN_MASK))
            return fail(u,EldenWcnUartProtocol,true);
    }
    u->engine_changed=true;wr(u,EldenWcnUartSe,GLOBAL_IRQ,0);
    if(pb_geni_config(&u->engine,rates[0].div,u->level))return fail(u,EldenWcnUartProtocol,true);
    /* Stop bit terminates the packing vector after one byte. No FIFO tail
     * padding enters H4 even if the reader asks for a single byte. */
    wr(u,EldenWcnUartSe,PB_SE_TX_PACK0,PACK_1X8);wr(u,EldenWcnUartSe,PB_SE_TX_PACK1,0);
    wr(u,EldenWcnUartSe,PB_SE_RX_PACK0,PACK_1X8);wr(u,EldenWcnUartSe,PB_SE_RX_PACK1,0);
    if(rd(u,EldenWcnUartSe,PB_SE_TX_PACK0)!=PACK_1X8||
       rd(u,EldenWcnUartSe,PB_SE_RX_PACK0)!=PACK_1X8)return fail(u,EldenWcnUartProtocol,true);
    if(pb_geni_rx_start(&u->engine))return fail(u,EldenWcnUartProtocol,true);
    pb_geni_flow(&u->engine,1,1);u->rfr_ready=true;u->baud=115200;
    u->stage=3;u->acquired=true;return 0;
}
int elden_wcn_uart_flow(EldenWcnUart *u,bool ready){
    int rc=live(u);if(rc)return rc;
    pb_geni_flow(&u->engine,1,ready);u->rfr_ready=ready;return 0;
}
int elden_wcn_uart_baud(EldenWcnUart *u,uint32_t baud){
    int rc=live(u);if(rc)return rc;
    const Rate *rate=NULL;for(unsigned i=0;i<2;i++)if(rates[i].baud==baud)rate=&rates[i];
    if(!rate)return EldenWcnUartArgument;
    if(u->rfr_ready||(rd(u,EldenWcnUartSe,PB_SE_STATUS)&PB_STATUS_M_ACTIVE)||
       (rd(u,EldenWcnUartSe,PB_SE_RX_FIFO_STATUS)&PB_RX_FIFO_WC))return fail(u,EldenWcnUartBusy,true);
    if(stop_rx(u))return u->error;
    if((rc=apply_rate(u,rate))||pb_geni_set_clock(&u->engine,rate->div,u->level))
        return fail(u,rc?rc:EldenWcnUartProtocol,true);
    if(pb_geni_rx_start(&u->engine))return fail(u,EldenWcnUartProtocol,true);
    u->baud=baud;return 0;
}
static void cancel_tx(EldenWcnUart *u){
    const uint32_t requests[]={PB_CMD_CTRL_CANCEL,PB_CMD_CTRL_ABORT};
    const uint32_t flags[]={PB_IRQ_CANCEL,PB_IRQ_ABORT};
    for(unsigned phase=0;phase<2;phase++){
        wr(u,EldenWcnUartSe,PB_SE_M_CMD_CTRL,requests[phase]);
        for(unsigned n=0;n<1000;n++){
            if((rd(u,EldenWcnUartSe,PB_SE_M_IRQ_STATUS)&flags[phase])&&
               !(rd(u,EldenWcnUartSe,PB_SE_STATUS)&PB_STATUS_M_ACTIVE)){
                wr(u,EldenWcnUartSe,PB_SE_M_IRQ_CLEAR,flags[phase]|PB_IRQ_DONE);return;
            }
            delay(u,10);
        }
    }
}
int elden_wcn_uart_write(EldenWcnUart *u,const uint8_t *data,size_t size,unsigned timeout){
    int rc=live(u);if(rc)return rc;
    if(!data||!size||size>65535||!timeout||timeout>5000)return EldenWcnUartArgument;
    if(rd(u,EldenWcnUartSe,PB_SE_STATUS)&PB_STATUS_M_ACTIVE)return fail(u,EldenWcnUartBusy,true);
    wr(u,EldenWcnUartSe,PB_SE_M_IRQ_CLEAR,0xffffffffu);
    wr(u,EldenWcnUartSe,PB_SE_UART_TX_TRANS_LEN,(uint32_t)size);
    wr(u,EldenWcnUartSe,PB_SE_M_CMD0,PB_UART_START_TX);
    uint64_t start=u->io.now_us(u->io.context);size_t sent=0;
    /* Independent poll cap bounds a stopped hardware counter too. */
    for(unsigned n=0;n<timeout*40u+1000u;n++){
        uint32_t irq=rd(u,EldenWcnUartSe,PB_SE_M_IRQ_STATUS);
        uint32_t used=rd(u,EldenWcnUartSe,PB_SE_TX_FIFO_STATUS)&PB_TX_FIFO_WC;
        if((irq&PB_M_IRQ_ERRORS)||used>u->engine.tx_depth){rc=EldenWcnUartProtocol;break;}
        while(used<u->engine.tx_depth&&sent<size){wr(u,EldenWcnUartSe,PB_SE_TX_FIFO,data[sent++]);used++;}
        if(sent==size&&(irq&PB_IRQ_DONE)){
            if(used||(rd(u,EldenWcnUartSe,PB_SE_STATUS)&PB_STATUS_M_ACTIVE)){rc=EldenWcnUartProtocol;break;}
            wr(u,EldenWcnUartSe,PB_SE_M_IRQ_CLEAR,PB_IRQ_DONE);u->tx_bytes+=(unsigned)size;return 0;
        }
        uint64_t now=u->io.now_us(u->io.context);
        if(now<start||now-start>=(uint64_t)timeout*1000u){rc=EldenWcnUartTimeout;break;}
        delay(u,25);
        rc=EldenWcnUartTimeout;
    }
    cancel_tx(u);return fail(u,rc,true);
}
int elden_wcn_uart_read(EldenWcnUart *u,uint8_t *out,size_t capacity,size_t *bytes){
    int rc=live(u);if(rc)return rc;
    if(!out||!bytes||!capacity||capacity>65535)return EldenWcnUartArgument;
    *bytes=0;
    uint32_t irq=rd(u,EldenWcnUartSe,PB_SE_S_IRQ_STATUS);
    if(irq&(PB_S_IRQ_ERRORS|PB_S_IRQ_GP))return fail(u,EldenWcnUartProtocol,true);
    uint32_t words=rd(u,EldenWcnUartSe,PB_SE_RX_FIFO_STATUS)&PB_RX_FIFO_WC;
    if(words>u->engine.rx_depth)return fail(u,EldenWcnUartProtocol,true);
    if(words>capacity)words=(uint32_t)capacity;
    for(unsigned i=0;i<words;i++)out[i]=(uint8_t)rd(u,EldenWcnUartSe,PB_SE_RX_FIFO);
    wr(u,EldenWcnUartSe,PB_SE_S_IRQ_CLEAR,irq);
    *bytes=words;u->rx_bytes+=words;return 0;
}
int elden_wcn_uart_release(EldenWcnUart *u){
    if(!u||!u->io.admit||!u->io.read||!u->io.write)return EldenWcnUartArgument;
    int rc=u->io.admit(u->io.context,EldenWcnUartRadioStopped);if(rc)return rc;
    /* Quarantine clears only after this physical owner proves radio reset and
     * every sequencer/FIFO is actually stopped. It is not software retry. */
    if(u->engine_changed){
        pb_geni_flow(&u->engine,1,0);u->rfr_ready=false;
        if(stop_rx(u))return u->error;
        if((rd(u,EldenWcnUartSe,PB_SE_STATUS)&(PB_STATUS_M_ACTIVE|PB_STATUS_S_ACTIVE))||
           (rd(u,EldenWcnUartSe,PB_SE_TX_FIFO_STATUS)&PB_TX_FIFO_WC))
            return fail(u,EldenWcnUartBusy,true);
        uint32_t words=rd(u,EldenWcnUartSe,PB_SE_RX_FIFO_STATUS)&PB_RX_FIFO_WC;
        if(words>u->engine.rx_depth)return fail(u,EldenWcnUartProtocol,true);
        for(unsigned i=0;i<words;i++){(void)rd(u,EldenWcnUartSe,PB_SE_RX_FIFO);u->discarded_rx++;}
        wr(u,EldenWcnUartSe,GLOBAL_IRQ,0);
        wr(u,EldenWcnUartSe,PB_SE_M_IRQ_CLEAR,0xffffffffu);wr(u,EldenWcnUartSe,PB_SE_S_IRQ_CLEAR,0xffffffffu);
        for(unsigned i=0;i<sizeof(config)/sizeof(config[0])-1;i++)wr(u,EldenWcnUartSe,config[i],u->configuration[i]);
        for(unsigned i=0;i<sizeof(config)/sizeof(config[0])-1;i++)
            if(rd(u,EldenWcnUartSe,config[i])!=u->configuration[i])return fail(u,EldenWcnUartProtocol,true);
        wr(u,EldenWcnUartSe,GLOBAL_IRQ,u->configuration[30]);
        if(rd(u,EldenWcnUartSe,GLOBAL_IRQ)!=u->configuration[30])
            return fail(u,EldenWcnUartProtocol,true);
        u->engine_changed=false;
    }
    if(u->pins_changed){
        for(unsigned i=0;i<4;i++){
            uint32_t reg=i*ELDEN_BT_GPIO_STRIDE,live_value=rd(u,EldenWcnUartPins,reg);
            wr(u,EldenWcnUartPins,reg,(live_value&~PIN_MASK)|(u->pin[i]&PIN_MASK));
            if((rd(u,EldenWcnUartPins,reg)&PIN_MASK)!=(u->pin[i]&PIN_MASK))
                return fail(u,EldenWcnUartProtocol,true);
        }
        u->pins_changed=false;
    }
    if(u->rate_changed){
        for(unsigned i=1;i<5;i++){
            uint32_t reg=ELDEN_BT_RCG+4*i,mask=i==1?RATE_MASK:65535u;
            wr(u,EldenWcnUartGcc,reg,(rd(u,EldenWcnUartGcc,reg)&~mask)|(u->rcg[i]&mask));
        }
        wr(u,EldenWcnUartGcc,ELDEN_BT_RCG,rd(u,EldenWcnUartGcc,ELDEN_BT_RCG)|UPDATE);
        if(wait_bits(u,EldenWcnUartGcc,ELDEN_BT_RCG,UPDATE,false))return u->error;
        for(unsigned i=1;i<5;i++){
            uint32_t mask=i==1?RATE_MASK:65535u;
            if((rd(u,EldenWcnUartGcc,ELDEN_BT_RCG+4*i)&mask)!=(u->rcg[i]&mask))
                return fail(u,EldenWcnUartClock,true);
        }
        u->rate_changed=false;
    }
    if(u->clock_changed){
        if(vote(u,true))return u->error;
        uint32_t current=rd(u,EldenWcnUartGcc,ELDEN_BT_RCG);
        wr(u,EldenWcnUartGcc,ELDEN_BT_RCG,(current&~ROOT_EN)|(u->rcg[0]&ROOT_EN));
        if((rd(u,EldenWcnUartGcc,ELDEN_BT_RCG)&ROOT_EN)!=(u->rcg[0]&ROOT_EN))
            return fail(u,EldenWcnUartClock,true);
        u->clock_changed=false;
    }
    u->acquired=false;u->quarantined=false;u->stage=4;return 0;
}
