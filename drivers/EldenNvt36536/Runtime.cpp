// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off Elden touch owner. No physical operation occurs in mode zero.
// Modes: 1=running chip identification, 2=stock SRAM upload/PID, 3=real HID,
// 4=inherited engine/SMMU survey. Survey never starts SPI or programs SMMU.
// 5=exact single-SID SE-DMA takeover and running-chip identification only.
// 6=same qualified DMA path plus the stock volatile SRAM firmware upload.
// 7=stock SRAM plus continuous checked point frames and physical HID.
#include "EldenTouchBoard.h"
extern "C" {
#include "Platform.h"
#include "Protocol.h"
#include "PointReport.h"
#include "DmaSurvey.h"
#include "DmaRoute.h"
#include "DmaTransfer.h"
#include "RunControl.h"
}
#include "NativeDmaBuffer.h"
#include "EldenNvtFirmware.h" // generated, ignored build input; not in the repo
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <stdarg.h>
extern "C" {
extern volatile int native_owner_data_status,elden_init_exec_accepted;
int elden_recovery_armed(void);
void elden_usb_log_putc(char);
int elden_touch_hid_ready(void);
int elden_touch_hid_publish(const struct elden_nvt_frame *);
int elden_hid_request_stop(void);
int elden_hid_stop_ready(void);
extern volatile uint32_t native_touch_hid_reports;
extern volatile uint32_t native_touch_edge_stats[4];
}
namespace {
static unsigned lease,smmu_lease;
static EldenTouchRunControl run_control={};
static int input_release_error;
static void log(const char *format,...){
    char line[320];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    for(const char *p=line;*p;++p)elden_usb_log_putc(*p);
}
struct Region {
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned bytes=0;
    ~Region(){if(keep)keep->release();}
    bool map(uint64_t pa,unsigned length){
        IODeviceMemory *memory=IODeviceMemory::withRange(pa,length);if(!memory)return false;
        keep=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
        va=keep?(uintptr_t)keep->getVirtualAddress():0;bytes=length;return va!=0;
    }
    uint32_t read(unsigned offset){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return 0xffffffffu;
        uint32_t value;uintptr_t address=va+offset;
        __asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(address):"memory");return value;
    }
    void write(unsigned offset,uint32_t value){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return;
        uintptr_t address=va+offset;
        __asm__ volatile("str %w0, [%1]\n\tdsb sy"::"r"(value),"r"(address):"memory");
    }
};
struct Context {
    static void *operator new(size_t bytes) noexcept {return IOMalloc(bytes);}
    static void operator delete(void *memory) noexcept {if(memory)IOFree(memory,sizeof(Context));}
    Region region[5];EldenTouchPlatform platform={};EldenNvtDevice device={};
    Region dma_global,dma_cb0;EldenTouchDmaRouteOwner route={};
    EldenTouchDmaTransfer dma_transfer={};EldenTouchDmaBuffer *dma_memory=nullptr;
    bool dma_mode=false,streaming=false;
    uint8_t tx[ELDEN_NVT_FW_CHUNK+1]={},rx[110]={};
    elden_nvt_frame previous={};unsigned reports=0,invalid=0,errors=0,frames=0,hidWaits=0;
    bool map(){
        return region[EldenTouchGcc].map(ELDEN_TOUCH_GCC_BASE,0x60000)&&
            region[EldenTouchPins].map(ELDEN_TOUCH_TLMM_BASE+ELDEN_TOUCH_GPIO_MISO*ELDEN_TOUCH_GPIO_STRIDE,0x4000)&&
            region[EldenTouchIrq].map(ELDEN_TOUCH_TLMM_BASE+ELDEN_TOUCH_GPIO_IRQ*ELDEN_TOUCH_GPIO_STRIDE,0x1000)&&
            region[EldenTouchSe].map(ELDEN_TOUCH_SE_BASE,ELDEN_TOUCH_SE_SIZE)&&
            region[EldenTouchQup].map(ELDEN_TOUCH_QUP_BASE,ELDEN_TOUCH_QUP_SIZE);
    }
};
static uint32_t read(void *context,EldenTouchSpace space,uint32_t reg){return static_cast<Context *>(context)->region[space].read(reg);}
static void write(void *context,EldenTouchSpace space,uint32_t reg,uint32_t value){static_cast<Context *>(context)->region[space].write(reg,value);}
static void delay(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
static int transfer(void *context,const uint8_t *tx,uint8_t *rx,size_t bytes){
    Context *c=static_cast<Context *>(context);
    if(!c->dma_mode)return elden_touch_platform_transfer(&c->platform,tx,rx,bytes);
    int rc=elden_touch_dma_transfer(&c->dma_transfer,tx,rx,bytes);
    if(!c->streaming||rc||c->dma_transfer.completed<80||c->dma_transfer.completed%1024==0)
    log("\nELDEN_TOUCH_DMA xfer=%u bytes=%u error=%d tx-in=%u rx-in=%u rx=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
        c->dma_transfer.completed,(unsigned)bytes,rc,c->platform.spi.last_tx_in,c->platform.spi.last_rx_in,
        rx&&!rc&&bytes>0?rx[0]:0,rx&&!rc&&bytes>1?rx[1]:0,rx&&!rc&&bytes>2?rx[2]:0,rx&&!rc&&bytes>3?rx[3]:0,
        rx&&!rc&&bytes>4?rx[4]:0,rx&&!rc&&bytes>5?rx[5]:0,rx&&!rc&&bytes>6?rx[6]:0,rx&&!rc&&bytes>7?rx[7]:0);
    return rc;
}
static int dma_idle(void *p){
    Context *c=static_cast<Context *>(p);Region &r=c->region[EldenTouchSe];
    if(c->platform.quarantined||r.read(SE_GENI_STATUS)||r.read(SE_GENI_TX_FIFO_STATUS)||r.read(SE_GENI_RX_FIFO_STATUS)||
       r.read(SE_DMA_TX_IRQ_STAT)||r.read(SE_DMA_RX_IRQ_STAT)||r.read(SE_GENI_DMA_MODE_EN)!=1||
       r.read(SE_GSI_EVENT_EN)!=(c->platform.touched_engine?0u:15u))return -16;
    return 0;
}
static int dma_check(void *p,EldenTouchDmaCheck,uint64_t epoch,unsigned sid,uint64_t tx,uint64_t rx,size_t bytes){
    Context *c=static_cast<Context *>(p);
    if(sid!=ELDEN_TOUCH_SE_DMA_SID||tx!=c->route.tx||rx!=c->route.rx||bytes!=c->route.bytes)return -22;
    return elden_touch_route_observe(&c->route,epoch);
}
static int dma_release_proof(void *p,uint64_t epoch){
    Context *c=static_cast<Context *>(p);
    return c->route.detached&&!c->route.claimed&&!c->route.poisoned&&c->route.epoch==epoch&&
        !c->platform.touched_engine&&!c->platform.quarantined?0:-16;
}
static int prepare_dma(Context *c){
    int rc=EldenTouchDmaBuffer::create(&c->dma_memory);if(rc)return rc;
    if(!c->dma_global.map(ELDEN_TOUCH_SMMU_BASE,ELDEN_TOUCH_SMMU_GLOBAL_BYTES)||
       !c->dma_cb0.map(ELDEN_TOUCH_SMMU_BASE+ELDEN_TOUCH_DMA_CB0_OFFSET,4096))return -12;
    EldenTouchRouteIo io={c,
        [](void *p,unsigned off)->uint32_t{Context *x=static_cast<Context *>(p);
            if(off==ELDEN_TOUCH_DMA_CB0_OFFSET)return x->dma_cb0.read(0);
            return x->dma_global.read(off);},
        [](void *p,unsigned off,uint32_t value)->int{
            if(off!=0x800+4*ELDEN_TOUCH_DMA_SLOT&&off!=0xc00+4*ELDEN_TOUCH_DMA_SLOT)return -22;
            static_cast<Context *>(p)->dma_global.write(off,value);return 0;},
        [](void *p,uint64_t epoch)->int{Context *x=static_cast<Context *>(p);
            /* Current frozen profile has no SMMU configuration writers after
             * storage/USB startup. Future clients must share this lease; the
             * full live snapshot also rejects any foreign route changes. */
            if(epoch!=1||!x->dma_mode||x->platform.stage!=2||native_owner_data_status||!elden_init_exec_accepted||
               !__sync_bool_compare_and_swap(&smmu_lease,0u,1u))return -16;
            return 0;},
        [](void *,uint64_t){__atomic_store_n(&smmu_lease,0u,__ATOMIC_RELEASE);},dma_idle};
    rc=elden_touch_route_attach(&c->route,&io,1,c->dma_memory->txPhysical(),c->dma_memory->rxPhysical(),c->dma_memory->span());
    log("\nELDEN_TOUCH_DMA attach=%d stage=%u writes=%u held=%u poison=%u slot=7 bank=0 tx=%llx rx=%llx bytes=%u\n",
        rc,c->route.stage,c->route.writes,c->route.claimed,c->route.poisoned,
        (unsigned long long)c->dma_memory->txPhysical(),(unsigned long long)c->dma_memory->rxPhysical(),(unsigned)c->dma_memory->span());
    log("\nELDEN_TOUCH_DMA cb0-sctlr=%08x captured=%u preserved=1\n",c->route.cb0_sctlr,c->route.cb0_seen);
    if(rc)return rc;
    EldenTouchDmaContract contract={};contract.context=c;contract.check=dma_check;contract.sid=ELDEN_TOUCH_SE_DMA_SID;
    rc=c->dma_memory->exportBuffers(&c->platform.spi,&contract,1,c->route.tx,c->route.rx);if(rc)return rc;
    rc=elden_touch_platform_activate_dma(&c->platform);if(rc)return rc;
    return elden_touch_dma_attach(&c->dma_transfer,&c->platform.spi,&contract);
}
static bool same(const elden_nvt_frame &a,const elden_nvt_frame &b){
    if(a.count!=b.count)return false;
    for(unsigned i=0;i<a.count;i++)if(a.contacts[i].id!=b.contacts[i].id||a.contacts[i].x!=b.contacts[i].x||a.contacts[i].y!=b.contacts[i].y)return false;
    return true;
}
static void release_contacts(Context *c){
    if(c->previous.count){
        elden_nvt_frame empty={};int rc=elden_touch_hid_publish(&empty);
        if(rc){__atomic_store_n(&input_release_error,rc,__ATOMIC_RELEASE);log("\nELDEN_TOUCH release-contact error=%d\n",rc);}
        else c->previous=empty;
    }
}
static void survey_smmu(){
    Region global;
    if(!global.map(ELDEN_TOUCH_SMMU_BASE,ELDEN_TOUCH_SMMU_GLOBAL_BYTES)){
        log("\nELDEN_TOUCH_DMA SMMU mapping refused\n");return;
    }
    EldenTouchDmaSurvey observed;
    int rc=elden_touch_dma_survey(&global,
        [](void *p,unsigned off){return static_cast<Region *>(p)->read(off);},
        [](void *,const EldenTouchDmaRoute *r){
            log("\nELDEN_TOUCH_DMA route sid=%x slot=%u smr=%08x s2cr=%08x type=%u bank=%u\n",
                r->sid,r->slot,r->smr,r->s2cr,r->type,r->bank);
            if(r->context_valid)log("\nELDEN_TOUCH_DMA context sid=%x bank=%u cbar=%08x cba2r=%08x\n",
                r->sid,r->bank,r->cbar,r->cba2r);
        },ELDEN_TOUCH_SE_DMA_SID,ELDEN_TOUCH_GPI_DMA_SID,&observed);
    log("\nELDEN_TOUCH_DMA SMMU result=%d cr0=%08x id0=%08x id1=%08x id2=%08x groups=%u\n",
        rc,observed.cr0,observed.id0,observed.id1,observed.id2,observed.groups);
    log("\nELDEN_TOUCH_DMA matches sid=%x count=%u sid=%x count=%u DMA-qualified=0\n",
        ELDEN_TOUCH_SE_DMA_SID,observed.matches[0],ELDEN_TOUCH_GPI_DMA_SID,observed.matches[1]);
    if(!rc){
        int inventory=elden_touch_dma_inventory(&global,
            [](void *p,unsigned off){return static_cast<Region *>(p)->read(off);},&observed,
            [](void *,unsigned i,uint32_t smr,uint32_t s2cr){
                log("\nELDEN_TOUCH_DMA slot=%u smr=%08x s2cr=%08x\n",i,smr,s2cr);
            },
            [](void *,unsigned i,uint32_t cbar,uint32_t cba2r){
                log("\nELDEN_TOUCH_DMA bank=%u cbar=%08x cba2r=%08x\n",i,cbar,cba2r);
            });
        log("\nELDEN_TOUCH_DMA inventory=%d slots=%u banks=%u global-only=1 writes=0\n",
            inventory,observed.groups,observed.id1&255);
    }
}
static void finish(Context *c,int error){
    release_contacts(c);
    if(c->dma_mode){
        if(c->route.poisoned||c->dma_transfer.quarantined)c->platform.quarantined=true;
        if(c->route.attached&&!c->platform.quarantined){
            int route_rc=elden_touch_route_detach(&c->route,c->route.epoch);
            if(route_rc)c->platform.quarantined=true;
            log("\nELDEN_TOUCH_DMA detach=%d stage=%u writes=%u poison=%u\n",route_rc,c->route.stage,c->route.writes,c->route.poisoned);
        }
    }
    int released=elden_touch_platform_release(&c->platform);
    if(!released&&c->dma_memory){
        released=EldenTouchDmaBuffer::destroy(&c->dma_memory,c,dma_release_proof);
        log("\nELDEN_TOUCH_DMA memory-release=%d\n",released);
    }
    log("\nELDEN_TOUCH stop error=%d protocol-stage=%u platform-stage=%u released=%d quarantine=%u reports=%u invalid=%u\n",
        error,c->device.stage,c->platform.stage,released,c->platform.quarantined,c->reports,c->invalid);
    if(released==0){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}
    // Uncertain transport retains maps/clock/pin ownership until full reset.
}
static void run(unsigned mode){
    if(elden_touch_run_state(&run_control)!=EldenTouchRunning)return;
    EldenNvtImage image={};int error=elden_nvt_image(&image,elden_nvt_firmware,sizeof(elden_nvt_firmware));
    if(error){log("\nELDEN_TOUCH refused firmware-preflight=%d hardware-writes=0\n",error);return;}
    if(!__sync_bool_compare_and_swap(&lease,0,1)){log("\nELDEN_TOUCH refused owner busy\n");return;}
    Context *c=new Context;
    if(!c){__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);log("\nELDEN_TOUCH refused allocation\n");return;}
    if(!c->map()){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);log("\nELDEN_TOUCH refused mapping\n");return;}
    c->dma_mode=mode>=5;
    c->platform.io={c,read,write,delay};c->platform.survey_only=mode>=4;
    error=elden_touch_platform_acquire(&c->platform);
    log("\nELDEN_TOUCH platform=%d stage=%u protocol=%u fifo-disabled=%u width=%u depth=%u mode=%u dma=0 source=TCXO spi-max=9600000\n",
        error,c->platform.stage,c->platform.spi.info.protocol,c->platform.spi.info.fifo_disabled,
        c->platform.spi.info.fifo_width_bits,c->platform.spi.info.tx_fifo_depth,mode);
    log("\nELDEN_TOUCH entry status=%x gsi=%x dma=%x tx=%x rx=%x mirq=%x sirq=%x dma-tx=%x dma-rx=%x tx-in=%x rx-in=%x\n",
        c->platform.spi.info.geni_status,c->platform.entry_gsi,c->platform.entry_dma,c->platform.entry_tx,c->platform.entry_rx,
        c->platform.entry_m_irq,c->platform.entry_s_irq,c->platform.entry_dma_tx_status,c->platform.entry_dma_rx_status,
        c->platform.entry_dma_tx_in,c->platform.entry_dma_rx_in);
    if(mode==4){
        if(!c->platform.quarantined)survey_smmu();
        finish(c,error);return;
    }
    if(error){finish(c,error);return;}
    if(mode>=5){
        error=prepare_dma(c);
        log("\nELDEN_TOUCH_DMA prepared=%d platform-stage=%u word=%u\n",error,c->platform.stage,c->platform.spi.cur_bpw);
        if(error){finish(c,error);return;}
    }
    c->device.context=c;c->device.transfer=transfer;c->device.delay_us=delay;
    c->device.tx=c->tx;c->device.rx=c->rx;c->device.tx_bytes=sizeof(c->tx);c->device.rx_bytes=sizeof(c->rx);
    if(mode==1||mode==5){
        error=elden_nvt_identify(&c->device,false);
        if(!error)error=elden_nvt_firmware_info(&c->device);
    }else error=elden_nvt_upload(&c->device,&image);
    log("\nELDEN_TOUCH chip=%d trim=%02x,%02x,%02x,%02x,%02x,%02x pid=%04x fw=%02x cascade=%u stage=%u\n",
        error,c->device.trim[0],c->device.trim[1],c->device.trim[2],c->device.trim[3],c->device.trim[4],c->device.trim[5],
        c->device.pid,c->device.firmware_version,c->device.cascade,c->device.stage);
    if(error||(mode!=3&&mode!=7)){finish(c,error);return;}
    for(unsigned n=0;n<12000&&!elden_touch_hid_ready()&&elden_touch_run_state(&run_control)==EldenTouchRunning;n++)IOSleep(10);
    if(elden_touch_run_state(&run_control)!=EldenTouchRunning){finish(c,0);return;}
    if(!elden_touch_hid_ready()){finish(c,-19);return;}
    log("\nELDEN_TOUCH real-input start gpio-irq=162 poll-ms=8 raw-max=19040,30400 normalized-max=32767 no-panel-reset=1\n");
    c->streaming=true;
    while(elden_touch_run_state(&run_control)==EldenTouchRunning){
        uint8_t packet[ELDEN_NVT_POINT_BYTES];elden_nvt_frame frame={};
        error=elden_nvt_points(&c->device,packet);
        if(error){finish(c,error);return;}
        error=elden_nvt_decode(packet,sizeof(packet),ELDEN_TOUCH_RAW_MAX_X,ELDEN_TOUCH_RAW_MAX_Y,&frame);
        if(c->frames+c->invalid<2||error){
            char raw[180];unsigned at=(unsigned)snprintf(raw,sizeof(raw),"\nELDEN_TOUCH_PACKET first66=");
            for(unsigned i=0;i<66;i++)at+=(unsigned)snprintf(raw+at,sizeof(raw)-at,"%02x",packet[i]);
            snprintf(raw+at,sizeof(raw)-at,"\n");log("%s",raw);
        }
        if(error){
            c->invalid++;c->errors++;release_contacts(c);
            log("\nELDEN_TOUCH bad-frame=%d consecutive=%u\n",error,c->errors);
            if(c->errors>=3){finish(c,error);return;}
        }else{
            c->errors=0;c->frames++;
            if(c->frames<=2||c->frames%256==0)log("\nELDEN_TOUCH frames=%u reports=%u contacts=%u hid=%u irq=%u ready=%u edge=%u,%u,%u,%u\n",
                c->frames,c->reports,frame.count,native_touch_hid_reports,c->region[EldenTouchIrq].read(4)&1,elden_touch_hid_ready(),
                native_touch_edge_stats[0],native_touch_edge_stats[1],native_touch_edge_stats[2],native_touch_edge_stats[3]);
            if(!same(frame,c->previous)){
                error=elden_touch_hid_publish(&frame);
                if(error){
                    const elden_nvt_contact &p=frame.contacts[0];
                    if(!c->hidWaits||c->hidWaits%128==0)log("\nELDEN_TOUCH publish-error=%d contacts=%u first-id=%u raw=%u,%u normalized=%u,%u\n",
                            error,frame.count,p.id,p.raw_x,p.raw_y,p.x,p.y);
                    // These codes reject before updating or sending a report.
                    // Driver matching may briefly disappear during startup;
                    // keep sampling and publish the latest frame when ready.
                    if(error==-1002||error==-1003||error==-1004||(error<=-1011&&error>=-1013)){
                        c->hidWaits++;IOSleep(8);continue;
                    }
                    finish(c,error);return;
                }
                c->hidWaits=0;
                bool countChanged=frame.count!=c->previous.count;
                c->previous=frame;c->reports++;
                if(c->reports<=10||countChanged||c->reports%32==0){
                    const elden_nvt_contact &p=frame.contacts[0];
                    log("\nELDEN_TOUCH reports=%u contacts=%u hid=%u id=%u raw=%u,%u normalized=%u,%u\n",
                        c->reports,frame.count,native_touch_hid_reports,p.id,p.raw_x,p.raw_y,p.x,p.y);
                }
            }
        }
        // Periodic report reads also cover short IRQ pulses. No missing IRQ
        // is treated as a lift; a valid empty frame or explicit error releases.
        IOSleep(8);
    }
    log("\nELDEN_TOUCH stopping frames=%u reports=%u\n",c->frames,c->reports);
    finish(c,0);
}
static void worker(void *argument,wait_result_t){
    if(elden_touch_run_enter(&run_control)){
        for(unsigned n=0;n<12000&&elden_touch_run_state(&run_control)==EldenTouchRunning&&
            (!elden_recovery_armed()||native_owner_data_status!=0||!elden_init_exec_accepted);n++)IOSleep(10);
        if(elden_touch_run_state(&run_control)==EldenTouchRunning&&elden_recovery_armed()&&native_owner_data_status==0&&elden_init_exec_accepted)run((unsigned)(uintptr_t)argument);
        else log("\nELDEN_TOUCH skipped: stopped or baseline not ready\n");
    }
    elden_touch_run_complete(&run_control,__atomic_load_n(&lease,__ATOMIC_ACQUIRE)||__atomic_load_n(&input_release_error,__ATOMIC_ACQUIRE));
    thread_terminate(current_thread());
}
}
extern "C" void elden_touch_runtime_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-touch",&mode,sizeof(mode));
    if(!mode||mode>7||!elden_touch_run_queue(&run_control))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,(void *)(uintptr_t)mode,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else{elden_touch_run_complete(&run_control,false);log("\nELDEN_TOUCH refused worker allocation\n");}
}
extern "C" int elden_touch_request_stop(void){
    unsigned state=elden_touch_run_request_stop(&run_control);
    int hidStopped=elden_hid_request_stop();
    for(unsigned n=0;n<300&&(state==EldenTouchStopping||!hidStopped);n++){
        IOSleep(10);state=elden_touch_run_state(&run_control);hidStopped=elden_hid_stop_ready();
    }
    return state==EldenTouchStopped&&hidStopped?1:0;
}
