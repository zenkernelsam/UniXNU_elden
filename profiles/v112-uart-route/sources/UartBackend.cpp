// SPDX-License-Identifier: BSD-2-Clause-Patent
// Real bounded MMIO adapter. No automatic startup; actual WCN power/reset
// authority is supplied by the separate shared radio owner.
#include "UartBackend.h"
#include "UartPlatform.h"
#include "EldenBluetoothBoard.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <stdarg.h>
extern "C" {
void elden_usb_log_putc(char);
__attribute__((weak)) volatile uint32_t sm8750_gcc_vote_owner;
__attribute__((weak)) int elden_wcn_power_lease_check(uint64_t,unsigned){return EldenWcnUartAgain;}
__attribute__((weak)) uint32_t elden_wcn_power_parent_hz(uint64_t,unsigned){return 0;}
}
namespace {
static volatile unsigned lease;
constexpr uint32_t GccOwner=0x42545541u;
static void log(const char *format,...){
    char b[256];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);
    for(const char *p=b;*p;p++)elden_usb_log_putc(*p);
}
struct Region{
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned bytes=0;
    ~Region(){if(keep)keep->release();}
    bool map(uint64_t pa,unsigned size){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,size);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        va=keep?(uintptr_t)keep->getVirtualAddress():0;bytes=size;return va!=0;
    }
    uint32_t read(unsigned offset){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return 0xffffffffu;
        uint32_t value;uintptr_t p=va+offset;
        __asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(p):"memory");return value;
    }
    void write(unsigned offset,uint32_t value){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return;
        uintptr_t p=va+offset;
        __asm__ volatile("str %w0, [%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");
    }
};
struct Context{
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Region regions[3],gic;EldenWcnUart uart={};uint64_t epoch=0;
    bool map(){
        return gic.map(0x01600000,4096)&&regions[EldenWcnUartGcc].map(ELDEN_BT_GCC_BASE,ELDEN_BT_GCC_BYTES)&&
            regions[EldenWcnUartPins].map(ELDEN_BT_TLMM_BASE+ELDEN_BT_PIN_CTS*ELDEN_BT_GPIO_STRIDE,0x4000)&&
            regions[EldenWcnUartSe].map(ELDEN_BT_SE_BASE,ELDEN_BT_SE_BYTES);
    }
};
static Context *active_context;
static uint32_t read(void *p,EldenWcnUartSpace s,uint32_t r){return static_cast<Context *>(p)->regions[s].read(r);}
static void write(void *p,EldenWcnUartSpace s,uint32_t r,uint32_t value){static_cast<Context *>(p)->regions[s].write(r,value);}
static uint64_t now_us(void *){
    uint64_t ticks;__asm__ volatile("mrs %0,cntpct_el0":"=r"(ticks));
    return ticks/96u*5u+(ticks%96u)*5u/96u; // exact 19.2MHz, no multiply overflow
}
static uint64_t now_ms(void *p){return now_us(p)/1000;}
static void delay_us(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
static void delay_ms(void *,unsigned ms){IOSleep(ms);}
static int admit(void *p,EldenWcnUartAdmission op){
    Context *c=static_cast<Context *>(p);int rc=elden_wcn_power_lease_check(c->epoch,(unsigned)op);
    if(rc||op==EldenWcnUartRadioStopped)return rc;
    const unsigned id=32+ELDEN_BT_SE_SPI;
    return c->gic.read(0x100+4*(id/32))&(1u<<(id%32))?EldenWcnUartBusy:0;
}
static uint32_t parent(void *p,unsigned source){return elden_wcn_power_parent_hz(static_cast<Context *>(p)->epoch,source);}
static int gcc_lock(void *,bool acquire){
    if(!acquire){
        if(__atomic_load_n(&sm8750_gcc_vote_owner,__ATOMIC_ACQUIRE)!=GccOwner)return EldenWcnUartProtocol;
        __atomic_store_n(&sm8750_gcc_vote_owner,0u,__ATOMIC_RELEASE);return 0;
    }
    for(unsigned n=0;n<100;n++){
        uint32_t empty=0;
        if(__atomic_compare_exchange_n(&sm8750_gcc_vote_owner,&empty,GccOwner,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
        IODelay(1);
    }
    return EldenWcnUartBusy;
}
static int send(void *p,const uint8_t *data,size_t bytes,unsigned timeout){
    Context *c=static_cast<Context *>(p);int rc=elden_wcn_uart_write(&c->uart,data,bytes,timeout);
    if(rc)log("\nELDEN_BT_UART tx error=%d stage=%u quarantine=%u\n",rc,c->uart.stage,c->uart.quarantined);
    return rc;
}
static size_t receive(void *p,uint8_t *data,size_t capacity){
    Context *c=static_cast<Context *>(p);size_t bytes=0;
    int rc=elden_wcn_uart_read(&c->uart,data,capacity,&bytes);
    if(rc){log("\nELDEN_BT_UART rx error=%d quarantine=%u\n",rc,c->uart.quarantined);return capacity+1;}
    return bytes;
}
static int flow(void *p,bool ready){return elden_wcn_uart_flow(&static_cast<Context *>(p)->uart,ready);}
static int baud(void *p,uint32_t value){return elden_wcn_uart_baud(&static_cast<Context *>(p)->uart,value);}
static void report(void *p,unsigned stage,int error){
    log("\nELDEN_BT_UART protocol-stage=%u error=%d\n",stage,error);
    Context *c=static_cast<Context *>(p);
    if(!error||!c->uart.acquired||c->uart.quarantined||
       elden_wcn_power_lease_check(c->epoch,EldenWcnUartLive))return;
    /* Read only registers already used by this active owner. Do not pop FIFO,
     * acknowledge an IRQ, wait or retry after a protocol failure. */
    Region &se=c->regions[EldenWcnUartSe];
    log("\nELDEN_BT_UART lines ios=%08x macro=%08x txcfg=%08x rxcmd=%08x pins=%08x/%08x/%08x/%08x\n",
        se.read(PB_SE_IOS),se.read(0x240),se.read(PB_SE_UART_TX_TRANS_CFG),se.read(PB_SE_S_CMD0),
        c->regions[EldenWcnUartPins].read(0),c->regions[EldenWcnUartPins].read(0x1000),
        c->regions[EldenWcnUartPins].read(0x2000),c->regions[EldenWcnUartPins].read(0x3000));
    log("\nELDEN_BT_UART failure status=%08x fifo=%08x sirq=%08x sen=%08x rfr=%08x\n",
        se.read(PB_SE_STATUS),se.read(PB_SE_RX_FIFO_STATUS),se.read(PB_SE_S_IRQ_STATUS),
        se.read(PB_SE_S_IRQ_EN),se.read(PB_SE_UART_MANUAL_RFR));
    log("\nELDEN_BT_UART failure clock=%08x/%08x level=%08x pack=%08x/%08x gran=%08x stale=%08x global=%08x tx=%u rx=%u\n",
        se.read(PB_SE_M_CLK_CFG),se.read(PB_SE_S_CLK_CFG),se.read(PB_SE_CLK_SEL),
        se.read(PB_SE_RX_PACK0),se.read(PB_SE_RX_PACK1),se.read(PB_SE_BYTE_GRAN),
        se.read(PB_SE_UART_RX_STALE),se.read(0xe1c),c->uart.tx_bytes,c->uart.rx_bytes);
}
static void dispose(Context *c){
    active_context=nullptr;delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);
}
}
extern "C" int elden_wcn_uart_backend_open(uint64_t epoch,EldenWcnStartupIo *out,void **handle){
    if(!epoch||!out||!handle)return EldenWcnUartArgument;
    int rc=elden_wcn_power_lease_check(epoch,EldenWcnUartAcquire);
    if(rc)return rc; // before maps or writes; no stand-in power authority
    unsigned empty=0;
    if(!__atomic_compare_exchange_n(&lease,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return EldenWcnUartBusy;
    Context *c=new Context;if(!c){__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);return -12;}
    active_context=c;
    c->epoch=epoch;
    if(!c->map()){dispose(c);return -12;}
    c->uart.io={c,read,write,now_us,delay_us,admit,parent,gcc_lock};
    rc=elden_wcn_uart_acquire(&c->uart);
    log("\nELDEN_BT_UART acquire=%d stage=%u packing=4x8 baud=%u no-dma=1\n",rc,c->uart.stage,c->uart.baud);
    if(rc){
        if(!c->uart.clock_changed&&!c->uart.engine_changed&&!c->uart.pins_changed)dispose(c);
        else if(elden_wcn_uart_release(&c->uart)==0)dispose(c);
        else *handle=c; // lets the same owner close after a qualified physical reset
        // Uncertain state retains the maps, clock/pin lease and owner.
        return rc;
    }
    *out={c,send,receive,now_ms,delay_ms,flow,baud,report};*handle=c;return 0;
}
extern "C" int elden_wcn_uart_backend_close(void *handle){
    if(!handle||handle!=active_context)return EldenWcnUartArgument;
    Context *c=static_cast<Context *>(handle);int rc=elden_wcn_uart_release(&c->uart);
    log("\nELDEN_BT_UART release=%d tx=%u rx=%u dropped-on-stop=%u quarantine=%u first-dropped-byte=%02x\n",
        rc,c->uart.tx_bytes,c->uart.rx_bytes,c->uart.discarded_rx,c->uart.quarantined,
        c->uart.first_discarded_rx);
    if(!rc)dispose(c);return rc;
}
