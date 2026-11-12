/* SPDX-License-Identifier: GPL-2.0-only */
/* Full HCI fixture over an independently timed 1x8 FIFO/CTS/RFR/RCG model.
 * The model's physical behavior is an assumption, never hardware evidence. */
#define ELDEN_WCN_HCI_TEST_MAIN hci_fixture_main
#include "TestWcn7861Hci.c"
#include "UartPlatform.h"
#include "RunControl.h"
#include "EldenBluetoothBoard.h"
typedef struct {
    HciModel controller;
    EldenWcnRunControl lifecycle;
    uint32_t gcc[0x60000/4],pins[4],se[0x4000/4];
    uint8_t tx[128],rx[128],wire[65536];
    unsigned tx_head,tx_count,rx_head,rx_count,wire_bytes,tx_remaining;
    uint64_t us,next_tx,next_rx;
    bool m_active,s_active,cts_blocked,power_valid,radio_stopped,counter_stuck;
    bool branch_stuck,rcg_stuck,bad_parent,foreign_lock,hold_cancel,hold_abort;
    bool locked;unsigned errors,writes,se_reads,tx_starts,completed,cancels,aborts;
} UartModel;
static uint32_t model_baud(UartModel *m){
    unsigned level=m->se[PB_SE_CLK_SEL/4];bool dfs=m->gcc[(ELDEN_BT_RCG+0x14)/4]&1;
    uint32_t cfg=m->gcc[(ELDEN_BT_RCG+(dfs?0x1c+4*level:4))/4],div=(m->se[PB_SE_M_CLK_CFG/4]>>4)&4095;
    uint32_t a=m->gcc[(ELDEN_BT_RCG+(dfs?0x5c+4*level:8))/4]&65535;
    uint32_t b=~m->gcc[(ELDEN_BT_RCG+(dfs?0x9c+4*level:12))/4]&65535;
    uint32_t parent=((cfg>>8)&7)==1?600000000:300000000;
    if(!div||!a||!(a+b))return 0;
    return (uint32_t)((uint64_t)parent*a/(a+b)/16/div);
}
static void tick(UartModel *m,unsigned delta){
    uint64_t target=m->us+delta;
    while(m->us<target){
        uint32_t baud=model_baud(m);unsigned byte_us=baud?(10000000u+baud-1)/baud:100;
        bool tx=m->m_active&&m->tx_count&&!m->cts_blocked;
        bool rfr=m->se[PB_SE_UART_MANUAL_RFR/4]==0&&m->rx_count<60;
        bool rx=m->s_active&&rfr&&m->controller.peripheral.pos<m->controller.peripheral.used;
        uint64_t next=target;
        if(tx&&m->next_tx<next)next=m->next_tx>m->us?m->next_tx:m->us;
        if(rx&&m->next_rx<next)next=m->next_rx>m->us?m->next_rx:m->us;
        m->us=next;
        if(tx&&m->us>=m->next_tx){
            CHECK(m->se[PB_SE_TX_PACK0/4]==15&&m->se[PB_SE_TX_PACK1/4]==0);
            CHECK(m->wire_bytes<sizeof(m->wire));
            m->wire[m->wire_bytes++]=m->tx[m->tx_head];m->tx_head=(m->tx_head+1)%128;m->tx_count--;
            m->next_tx=m->us+byte_us;
            if(!--m->tx_remaining){
                m->m_active=false;m->se[PB_SE_M_IRQ_STATUS/4]|=PB_IRQ_DONE;m->completed++;
                CHECK(!hci_write(&m->controller,m->wire,m->wire_bytes,2000));m->wire_bytes=0;
            }
        }
        if(rx&&m->us>=m->next_rx){
            CHECK(m->se[PB_SE_RX_PACK0/4]==15&&m->se[PB_SE_RX_PACK1/4]==0);
            CHECK(m->rx_count<64);
            m->rx[(m->rx_head+m->rx_count++)%128]=m->controller.peripheral.queue[m->controller.peripheral.pos++];
            m->se[PB_SE_S_IRQ_STATUS/4]|=PB_IRQ_RX_LAST;m->next_rx=m->us+byte_us;
        }
    }
}
static uint32_t uart_rd(void *p,EldenWcnUartSpace space,uint32_t reg){
    UartModel *m=p;tick(m,1);
    CHECK(!(reg&3));
    if(space==EldenWcnUartGcc){
        CHECK(reg<sizeof(m->gcc));
        const uint32_t halts[]={ELDEN_BT_HALT_M_AHB,ELDEN_BT_HALT_S_AHB,ELDEN_BT_HALT_CORE,ELDEN_BT_HALT_CORE_2X,ELDEN_BT_HALT_SE6};
        for(unsigned i=0;i<5;i++)if(reg==halts[i])return m->branch_stuck||(m->gcc[ELDEN_BT_GCC_VOTE/4]&ELDEN_BT_GCC_VOTE_MASK)!=ELDEN_BT_GCC_VOTE_MASK?0x80000000u:0;
        if(reg==ELDEN_BT_RCG){
            uint32_t value=m->gcc[reg/4];
            return value|(value&2?0:0x80000000u);
        }
        return m->gcc[reg/4];
    }
    if(space==EldenWcnUartPins){CHECK(reg<0x4000&&reg%0x1000==0);return m->pins[reg/0x1000];}
    CHECK(reg<sizeof(m->se));m->se_reads++;
    CHECK((m->gcc[ELDEN_BT_GCC_VOTE/4]&ELDEN_BT_GCC_VOTE_MASK)==ELDEN_BT_GCC_VOTE_MASK);
    switch(reg){
      case PB_SE_STATUS:return (m->m_active?PB_STATUS_M_ACTIVE:0)|(m->s_active?PB_STATUS_S_ACTIVE:0);
      case PB_SE_TX_FIFO_STATUS:return m->tx_count;
      case PB_SE_RX_FIFO_STATUS:return m->rx_count;
      case PB_SE_RX_FIFO:{
        CHECK(m->rx_count);uint8_t b=m->rx[m->rx_head];m->rx_head=(m->rx_head+1)%128;m->rx_count--;
        return 0xcafe0000u|b; // upper bytes are not part of the 1x8 entry
      }
      default:return m->se[reg/4];
    }
}
static void uart_wr(void *p,EldenWcnUartSpace space,uint32_t reg,uint32_t value){
    UartModel *m=p;m->writes++;tick(m,1);
    if(space==EldenWcnUartGcc){
        CHECK(reg<sizeof(m->gcc));
        if(reg==ELDEN_BT_GCC_VOTE)CHECK(m->locked);
        m->gcc[reg/4]=reg==ELDEN_BT_RCG?value&~0x80000000u:value;
        if(reg==ELDEN_BT_RCG&&!m->rcg_stuck)m->gcc[reg/4]&=~1u;
        return;
    }
    if(space==EldenWcnUartPins){CHECK(reg<0x4000&&reg%0x1000==0);m->pins[reg/0x1000]=value;return;}
    CHECK(reg<sizeof(m->se));
    switch(reg){
      case PB_SE_M_CMD0:CHECK(!m->m_active&&value==PB_UART_START_TX);m->m_active=true;m->tx_remaining=m->se[PB_SE_UART_TX_TRANS_LEN/4];m->tx_starts++;m->next_tx=m->us;break;
      case PB_SE_S_CMD0:CHECK(!m->s_active&&value==PB_UART_START_RX);m->s_active=true;m->next_rx=m->us;break;
      case PB_SE_M_CMD_CTRL:
        if(value==PB_CMD_CTRL_CANCEL){m->cancels++;if(!m->hold_cancel){m->m_active=false;m->tx_count=0;m->se[PB_SE_M_IRQ_STATUS/4]|=PB_IRQ_CANCEL;}}
        if(value==PB_CMD_CTRL_ABORT){m->aborts++;if(!m->hold_abort){m->m_active=false;m->tx_count=0;m->se[PB_SE_M_IRQ_STATUS/4]|=PB_IRQ_ABORT;}}
        break;
      case PB_SE_S_CMD_CTRL:m->s_active=false;m->se[PB_SE_S_IRQ_STATUS/4]|=PB_IRQ_CANCEL;break;
      case PB_SE_M_IRQ_CLEAR:m->se[PB_SE_M_IRQ_STATUS/4]&=~value;break;
      case PB_SE_S_IRQ_CLEAR:m->se[PB_SE_S_IRQ_STATUS/4]&=~value;break;
      case PB_SE_TX_FIFO:CHECK(m->tx_count<64&&value<=255);m->tx[(m->tx_head+m->tx_count++)%128]=(uint8_t)value;break;
      default:m->se[reg/4]=value;break;
    }
}
static void uart_delay(void *p,unsigned us){tick(p,us);}
static uint64_t uart_time(void *p){UartModel *m=p;return m->counter_stuck?0:m->us;}
static int uart_admit(void *p,EldenWcnUartAdmission op){
    UartModel *m=p;
    if(op==EldenWcnUartRadioStopped)return m->radio_stopped?0:-11;
    return m->power_valid&&elden_wcn_run_live(&m->lifecycle)?0:-11;
}
static uint32_t uart_parent(void *p,unsigned source){
    UartModel *m=p;return m->bad_parent?0:source==1?600000000:300000000;
}
static int uart_lock(void *p,bool acquire){
    UartModel *m=p;if(acquire){if(m->foreign_lock||m->locked)return -16;m->locked=true;}
    else{CHECK(m->locked);m->locked=false;}return 0;
}
static UartModel *fresh_uart(EldenWcnUart *u){
    UartModel *m=calloc(1,sizeof(*m));CHECK(m);
    CHECK(elden_wcn_run_queue(&m->lifecycle)&&elden_wcn_run_enter(&m->lifecycle));
    m->power_valid=true;m->gcc[(ELDEN_BT_RCG+4)/4]=0x2601;
    m->gcc[(ELDEN_BT_RCG+8)/4]=384;m->gcc[(ELDEN_BT_RCG+12)/4]=~(15625-384)&65535;
    m->gcc[(ELDEN_BT_RCG+16)/4]=~15625u&65535;
    m->gcc[ELDEN_BT_GCC_VOTE/4]=1u<<18; // unrelated consumer preserved
    m->se[PB_SE_FW_REVISION/4]=2u<<8|0x66;
    m->se[PB_SE_HW_PARAM_0/4]=32u<<24|64u<<16;m->se[PB_SE_HW_PARAM_1/4]=32u<<24|64u<<16;
    memset(u,0,sizeof(*u));u->io=(EldenWcnUartIo){m,uart_rd,uart_wr,uart_time,uart_delay,uart_admit,uart_parent,uart_lock};
    return m;
}
static int physical_write(void *p,const uint8_t *b,size_t n,unsigned timeout){return elden_wcn_uart_write(p,b,n,timeout);}
static size_t physical_read(void *p,uint8_t *b,size_t capacity){size_t n=0;return elden_wcn_uart_read(p,b,capacity,&n)?capacity+1:n;}
static uint64_t physical_now(void *p){EldenWcnUart *u=p;return uart_time(u->io.context)/1000;}
static void physical_delay(void *p,unsigned ms){EldenWcnUart *u=p;uart_delay(u->io.context,ms*1000);}
static int physical_flow(void *p,bool ready){return elden_wcn_uart_flow(p,ready);}
static int physical_baud(void *p,uint32_t baud_value){return elden_wcn_uart_baud(p,baud_value);}
static void combined(uint8_t *fw[5],size_t size[5],bool dfs){
    EldenWcnUart u;UartModel *m=fresh_uart(&u);
    if(dfs){
        m->gcc[(ELDEN_BT_RCG+0x14)/4]=1;
        m->gcc[(ELDEN_BT_RCG+0x1c+8)/4]=0x2601;
        m->gcc[(ELDEN_BT_RCG+0x5c+8)/4]=384;
        m->gcc[(ELDEN_BT_RCG+0x9c+8)/4]=~(15625-384)&65535;
        m->gcc[(ELDEN_BT_RCG+0x1c+12)/4]=0x2101;
        m->gcc[(ELDEN_BT_RCG+0x5c+12)/4]=16;
        m->gcc[(ELDEN_BT_RCG+0x9c+12)/4]=~(75-16)&65535;
    }
    for(unsigned i=0;i<3;i++){
        m->controller.peripheral.fw[i]=fw[i];m->controller.peripheral.size[i]=size[i];
        CHECK(!elden_wcn_peripheral_prepare((enum EldenWcnPeripheralFirmware)i,fw[i],size[i],&m->controller.peripheral.plan[i]));
    }
    CHECK(!elden_wcn_uart_acquire(&u)&&u.baud==115200&&model_baud(m)==115200);
    EldenWcnStartup wire={0};
    EldenWcnStartupIo io={&u,physical_write,physical_read,physical_now,physical_delay,physical_flow,physical_baud,NULL};
    CHECK(!elden_wcn_startup_prepare(&wire,&io,fw[0],size[0],fw[1],size[1],fw[2],size[2]));
    CHECK(!elden_wcn_startup_run(&wire)&&wire.ready&&u.baud==8000000&&model_baud(m)==8000000);
    if(dfs)CHECK(u.level==3&&!u.rate_changed);
    m->controller.active=true;m->controller.bt=fw[3];m->controller.bt_bytes=size[3];
    m->controller.nvm=fw[4];m->controller.nvm_bytes=size[4];
    const uint8_t addr[]={1,2,3,4,5,0xc2};memcpy(m->controller.address,addr,6);
    EldenWcnHci hci={0};
    CHECK(!elden_wcn_hci_prepare(&hci,&wire,fw[3],size[3],fw[4],size[4],addr));
    CHECK(!elden_wcn_hci_run(&hci)&&hci.ready&&u.tx_bytes>219296);
    CHECK(m->completed==m->tx_starts&&!m->m_active&&!m->tx_count);
    m->radio_stopped=true;m->gcc[ELDEN_BT_GCC_VOTE/4]|=1u<<6;
    CHECK(!elden_wcn_uart_release(&u)&&!u.acquired&&!u.quarantined);
    CHECK((m->gcc[ELDEN_BT_GCC_VOTE/4]&(1u<<18))&&
          (m->gcc[ELDEN_BT_GCC_VOTE/4]&(1u<<6))&&
          !(m->gcc[ELDEN_BT_GCC_VOTE/4]&(1u<<10))&&
          (m->gcc[ELDEN_BT_GCC_VOTE/4]&15u)==15u);
    CHECK(!m->pins[0]&&!m->pins[3]&&!m->se[PB_SE_TX_PACK0/4]);
    free(m);
}
int main(int argc,char **argv){
    CHECK(argc==6);uint8_t *fw[5];size_t size[5];for(unsigned i=0;i<5;i++)fw[i]=load(argv[i+1],&size[i]);
    combined(fw,size,false);combined(fw,size,true);
    unsigned cases=2;
    for(unsigned scenario=0;scenario<9;scenario++){
        EldenWcnUart u;UartModel *m=fresh_uart(&u);cases++;
        if(scenario==0)m->power_valid=false;
        if(scenario==1)m->bad_parent=true;
        if(scenario==2)m->se[PB_SE_IF_DISABLE/4]=1;
        if(scenario==3)m->se[PB_SE_GSI_EVENT_EN/4]=1;
        if(scenario==4)m->m_active=true;
        if(scenario==5)m->branch_stuck=true;
        if(scenario==6)m->rcg_stuck=true;
        if(scenario==7)m->pins[0]=3u<<2;
        if(scenario==8)m->foreign_lock=true;
        CHECK(elden_wcn_uart_acquire(&u)<0&&!u.acquired);
        if(scenario<2)CHECK(!m->writes&&!m->se_reads);
        if(scenario==5||scenario==8)CHECK(!m->se_reads);
        m->radio_stopped=true;m->m_active=false;m->branch_stuck=false;m->rcg_stuck=false;m->foreign_lock=false;
        CHECK(!elden_wcn_uart_release(&u));free(m);
    }
    for(unsigned scenario=0;scenario<3;scenario++){
        EldenWcnUart u;UartModel *m=fresh_uart(&u);cases++;
        CHECK(!elden_wcn_uart_acquire(&u));m->cts_blocked=true;
        m->counter_stuck=scenario==1;m->hold_cancel=scenario==2;m->hold_abort=scenario==2;
        const uint8_t wake_byte=0xfd;
        CHECK(elden_wcn_uart_write(&u,&wake_byte,1,2)<0&&u.quarantined&&!u.tx_bytes);
        CHECK(m->cancels==1);
        unsigned writes=m->writes;CHECK(elden_wcn_uart_write(&u,&wake_byte,1,2)<0&&m->writes==writes);
        CHECK(elden_wcn_uart_release(&u)<0&&m->writes==writes);
        m->radio_stopped=true;m->m_active=false;m->tx_count=0;
        CHECK(!elden_wcn_uart_release(&u));free(m);
    }
    {
        EldenWcnUart u;UartModel *m=fresh_uart(&u);cases++;
        CHECK(!elden_wcn_uart_acquire(&u));
        const uint8_t data[]={0xfc,0,0xff,0x34,0x1e};
        push(&m->controller.peripheral,data,sizeof(data));tick(m,1000);
        for(unsigned i=0;i<sizeof(data);i++){
            uint8_t byte=0xa5;size_t bytes=0;
            CHECK(!elden_wcn_uart_read(&u,&byte,1,&bytes)&&bytes==1&&byte==data[i]);
        }
        uint8_t byte=0;size_t bytes=1;
        CHECK(!elden_wcn_uart_read(&u,&byte,1,&bytes)&&!bytes);
        m->se[PB_SE_S_IRQ_STATUS/4]|=PB_IRQ_RX_WR_ERR;
        CHECK(elden_wcn_uart_read(&u,&byte,1,&bytes)<0&&u.quarantined);
        m->radio_stopped=true;CHECK(!elden_wcn_uart_release(&u));free(m);
    }
    {
        EldenWcnUart u;UartModel *m=fresh_uart(&u);cases++;
        CHECK(elden_wcn_run_stop(&m->lifecycle)==EldenWcnStopping);
        CHECK(elden_wcn_uart_acquire(&u)==EldenWcnUartAgain&&!m->writes&&!m->se_reads);
        CHECK(!elden_wcn_run_queue(&m->lifecycle));
        elden_wcn_run_complete(&m->lifecycle,false);
        CHECK(elden_wcn_run_stop(&m->lifecycle)==EldenWcnStopped);free(m);
    }
    {
        EldenWcnUart u;UartModel *m=fresh_uart(&u);cases++;
        CHECK(!elden_wcn_uart_acquire(&u));
        CHECK(elden_wcn_run_stop(&m->lifecycle)==EldenWcnStopping);
        unsigned writes=m->writes;uint8_t byte=0xfd;size_t got=0;
        CHECK(elden_wcn_uart_write(&u,&byte,1,1000)==EldenWcnUartAgain&&m->writes==writes);
        CHECK(elden_wcn_uart_read(&u,&byte,1,&got)<0&&m->writes==writes);
        CHECK(elden_wcn_uart_release(&u)==EldenWcnUartAgain&&m->writes==writes);
        CHECK(elden_wcn_run_state(&m->lifecycle)==EldenWcnStopping);
        m->radio_stopped=true;
        CHECK(!elden_wcn_uart_release(&u)&&!u.acquired&&!u.quarantined);
        CHECK(m->gcc[ELDEN_BT_GCC_VOTE/4]&(1u<<18));
        elden_wcn_run_complete(&m->lifecycle,false);
        CHECK(elden_wcn_run_stop(&m->lifecycle)==EldenWcnStopped);free(m);
    }
    for(unsigned i=0;i<5;i++)free(fw[i]);
    printf("PASS %u UART/platform scenarios, %u FIFO/wire/clock/lease assertions\n",cases,checks);
    return 0;
}
