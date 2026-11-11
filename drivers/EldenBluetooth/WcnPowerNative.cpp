// SPDX-License-Identifier: BSD-2-Clause-Patent
// Explicit WCN power owner. No physical operation in mode zero.
#include "WcnPower.h"
#include "UartBackend.h"
#include "Wcn7861Hci.h"
#if defined(ELDEN_BT_NATIVE_BUILD)
#include "NativeHost.h"
#endif
#include "EldenBluetoothBoard.h"
#include "EldenRebootResources.h"
extern "C" {
#include "Sm8750Rpmh.h"
#include "Sm8750AossQmp.h"
#include "BtPmic.h"
#include "BtTlv.h"
}
#include "EldenWcnFirmware.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
extern "C" {
void elden_usb_log_putc(char);
int elden_recovery_armed(void);
extern volatile int native_owner_data_status,elden_init_exec_accepted;
__attribute__((weak)) volatile uint32_t sm8750_aoss_qmp_owner;
}
namespace {
constexpr uint32_t AossOwner=0x57434e50u;
static unsigned attempted,published_state;
static uint64_t published_epoch,next_epoch;
static void log(const char *format,...){
    char line[320];va_list a;va_start(a,format);vsnprintf(line,sizeof(line),format,a);va_end(a);
    for(const char *p=line;*p;p++)elden_usb_log_putc(*p);
}
struct Region{
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned bytes=0;
    ~Region(){if(keep)keep->release();}
    bool map(uint64_t pa,unsigned length){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,length);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        va=keep?(uintptr_t)keep->getVirtualAddress():0;bytes=length;return va!=0;
    }
    uint32_t read(unsigned offset){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return 0xffffffffu;
        uint32_t value;uintptr_t p=va+offset;
        __asm__ volatile("ldr %w0,[%1]":"=r"(value):"r"(p):"memory");return value;
    }
    void write(unsigned offset,uint32_t value){
        if(!va||bytes<4||(offset&3)||offset>bytes-4)return;
        uintptr_t p=va+offset;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");
    }
};
struct Context{
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Region gcc,core,owner,observer,writer,aoss,ipcc,rsc,wlan;
    Sm8750CmdDb db={};uint32_t vrm[EldenWcnRailCount]={};
    PbSpmiIo spmi={};Sm8750RpmhIo rpmh={};
    Sm8750AossQmpIo aoss_io={},ipcc_io={};Sm8750AossQmp qmp={};
    EldenWcnPower power={};EldenWcnStartup startup={};EldenWcnHci hci={};
    uint8_t address[6]={};int bt_apid=-1;
    bool spmi_uncertain=false,aoss_uncertain=false,rsc_uncertain=false;
    bool map(){
        return gcc.map(ELDEN_BT_GCC_BASE,ELDEN_BT_GCC_BYTES)&&
            core.map(PB_SPMI_CORE,PB_SPMI_CORE_BYTES)&&owner.map(PB_SPMI_CNFG,PB_SPMI_CNFG_BYTES)&&
            observer.map(PB_SPMI_OBS,PB_SPMI_OBS_BYTES)&&
            aoss.map(0x0c300000,0x400)&&ipcc.map(0x00406000,0x1000)&&
            rsc.map(0x16520000,0x10000)&&
            wlan.map(ELDEN_BT_TLMM_BASE+ELDEN_BT_PIN_WLAN_EN*ELDEN_BT_GPIO_STRIDE,0x1000);
    }
};
static Context *active;
static uint64_t now_us(void *){
    uint64_t ticks;__asm__ volatile("mrs %0,cntpct_el0":"=r"(ticks));
    return ticks/96*5+(ticks%96)*5/96;
}
static void delay(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
static uint32_t core_read(void *p,uint32_t reg){return static_cast<Context *>(p)->core.read(reg);}
static uint32_t owner_read(void *p,uint32_t reg){return static_cast<Context *>(p)->owner.read(reg);}
static uint32_t obs_read(void *p,uint32_t reg){return static_cast<Context *>(p)->observer.read(reg);}
static void obs_write(void *p,uint32_t reg,uint32_t v){static_cast<Context *>(p)->observer.write(reg,v);}
static uint32_t chan_read(void *p,unsigned apid,uint32_t reg){
    Context *c=static_cast<Context *>(p);return apid==(unsigned)c->bt_apid?c->writer.read(reg):0xffffffffu;
}
static void chan_write(void *p,unsigned apid,uint32_t reg,uint32_t v){
    Context *c=static_cast<Context *>(p);if(apid==(unsigned)c->bt_apid)c->writer.write(reg,v);
}
static int gpio_read(Context *c,uint8_t out[8]){
    static const uint8_t regs[]={4,5,0x40,0x41,0x42,0x44,0x45,0x46};
    if(c->spmi_uncertain||c->bt_apid<0)return -71;
    for(unsigned i=0;i<8;i++){
        int rc=pb_spmi_read8(&c->spmi,c->bt_apid,regs[i],&out[i]);
        if(rc){c->spmi_uncertain=true;return rc;}
    }
    return 0;
}
static int bt_get(void *p,bool *high){
    Context *c=static_cast<Context *>(p);uint8_t r[8];
    int rc=gpio_read(c,r);if(rc)return rc;
    if(r[0]!=0x10||r[1]<0x10||r[1]>0x13)return -95;
    *high=(r[7]&0x80)&&(r[2]&3)==1&&(r[5]&0x80);return 0;
}
static int bt_set(void *p,bool high){
    Context *c=static_cast<Context *>(p);uint8_t before[8];int rc=gpio_read(c,before);if(rc)return rc;
    // Full reads must succeed before the canonical GPIO writer is called.
    if(before[0]!=0x10||before[1]<0x10||before[1]>0x13)return -95;
    if((c->owner.read(4*(unsigned)c->bt_apid)&7)!=0)return -1;
    const uint8_t regs[]={0x42,0x41,0x45,0x44,0x40,0x46};
    const uint8_t values[]={(uint8_t)((before[4]&~7u)|5u),(uint8_t)((before[3]&~7u)|1u),
        (uint8_t)(before[6]&~0x30u),(uint8_t)((before[5]&~0x8fu)|(high?0x80u:0)),
        (uint8_t)((before[2]&~3u)|1u),(uint8_t)(before[7]|0x80u)};
    for(unsigned i=0;i<6;i++){
        c->writer.write(0x10,values[i]);c->writer.write(0,(uint32_t)regs[i]<<4);
        bool done=false;
        for(unsigned n=0;n<1000;n++){
            uint32_t status=c->writer.read(8);
            if(status&0xeu){c->spmi_uncertain=true;return -5;}
            if(status&1u){done=true;break;}
            IODelay(1);
        }
        if(!done){c->spmi_uncertain=true;return -110;}
    }
    uint8_t after[8];if((rc=gpio_read(c,after)))return rc;
    if((after[2]&3)!=1||(after[3]&7)!=1||(after[4]&7)!=5||(after[5]&15)!=0||
       (after[6]&0x30)||(after[7]&0x80)==0||((after[5]&0x80)!=0)!=high){
        c->spmi_uncertain=true;return -71;
    }
    return 0;
}
static int wlan_get(void *p,bool *high){
    uint32_t value=static_cast<Context *>(p)->wlan.read(4);
    if(value==0xffffffffu)return -5;*high=value&1u;return 0;
}
static int parents(void *p,uint32_t *main,uint32_t *even){
    Context *c=static_cast<Context *>(p);
    uint32_t mode=c->gcc.read(0),op=c->gcc.read(4),l=c->gcc.read(0x10);
    uint32_t alpha=c->gcc.read(0x14),user=c->gcc.read(0x18);
    if(mode!=c->gcc.read(0)||op!=c->gcc.read(4)||l!=c->gcc.read(0x10)||
       alpha!=c->gcc.read(0x14)||user!=c->gcc.read(0x18))return -16;
    return elden_wcn_parent_decode(mode,op,l,alpha,user,main,even);
}
static int rail_read(void *p,EldenWcnRail rail,EldenWcnRailState *out){
    Context *c=static_cast<Context *>(p);if(rail>=EldenWcnRailCount)return -22;
    const auto &spec=elden_wcn_rail_specs[rail];int owner_id=-1;
    int apid=pb_spmi_find(&c->spmi,spec.ppid,&owner_id);if(apid<0)return apid;
    const uint8_t regs[]={8,0x46,0x45,0x40,0x41};uint8_t values[5];
    for(unsigned i=0;i<5;i++){
        int rc=pb_spmi_read8(&c->spmi,apid,regs[i],&values[i]);if(rc)return rc;
    }
    *out={(uint16_t)(values[3]|(uint16_t)values[4]<<8),values[0],values[1],values[2]};return 0;
}
static uint32_t rsc_read(void *p,uint32_t reg){return static_cast<Context *>(p)->rsc.read(reg);}
static void rsc_write(void *p,uint32_t reg,uint32_t value){static_cast<Context *>(p)->rsc.write(reg,value);}
static int rail_vote(void *p,EldenWcnRail rail,uint16_t mv,uint8_t mode,bool enable){
    Context *c=static_cast<Context *>(p);
    if(c->rsc_uncertain||rail>=EldenWcnRailCount||!enable||mode!=7||
       mv<elden_wcn_rail_specs[rail].minimum||mv>elden_wcn_rail_specs[rail].maximum)return -71;
    uint32_t base=c->vrm[rail];
    const Sm8750RpmhCmd commands[]={{base,mv},{base+8,mode},{base+4,1}};
    Sm8750RpmhResult result={};int rc=Sm8750RpmhWriteActive(&c->rpmh,commands,3,7,&result);
    log("\nELDEN_WCN rail=%u mv=%u result=%d tcs=%u step=%u\n",rail,mv,rc,result.tcs,result.step);
    if(rc&&result.step>=Sm8750RpmhStepProgram)c->rsc_uncertain=true;
    return rc;
}
static uint32_t aoss_read(void *p,uint32_t reg){return static_cast<Context *>(p)->aoss.read(reg);}
static void aoss_write(void *p,uint32_t reg,uint32_t v){static_cast<Context *>(p)->aoss.write(reg,v);}
static uint32_t ipcc_read(void *p,uint32_t reg){return static_cast<Context *>(p)->ipcc.read(reg);}
static void ipcc_write(void *p,uint32_t reg,uint32_t v){static_cast<Context *>(p)->ipcc.write(reg,v);}
static int qmp_send(void *p,const char *text){
    Context *c=static_cast<Context *>(p);if(c->aoss_uncertain)return -71;
    uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&sm8750_aoss_qmp_owner,&empty,AossOwner,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    int rc=Sm8750AossQmpSend(&c->qmp,text);
    log("\nELDEN_WCN qmp=%d step=%u text=%s\n",rc,c->qmp.step,text);
    if(rc){c->aoss_uncertain=true;return rc;} // retain the shared mailbox owner on uncertainty
    __atomic_store_n(&sm8750_aoss_qmp_owner,0u,__ATOMIC_RELEASE);return 0;
}
static uint32_t db_read(void *p,uint32_t reg){return static_cast<Region *>(p)->read(reg);}
static int preflight(void *p){
    Context *c=static_cast<Context *>(p);
    if(c->core.read(0)!=ELDEN_REBOOT_VERSION||(c->core.read(4)&2047)!=ELDEN_REBOOT_APID_COUNT)return -95;
    int owner_id=-1;int apid=pb_spmi_find(&c->spmi,ELDEN_BT_ENABLE_PPID,&owner_id);
    unsigned matches=0;
    for(unsigned i=0;i<ELDEN_REBOOT_APID_COUNT;i++){
        uint32_t value=c->core.read(0x2000+4*i);
        if(value&&((value>>8)&4095)==ELDEN_BT_ENABLE_PPID&&(c->owner.read(4*i)&7)==0)matches++;
    }
    if(apid<0||owner_id||matches!=1)return -1;
    if(c->bt_apid<0){
        if(!c->writer.map(PB_SPMI_CHAN+PB_SPMI_CHAN_STRIDE*(uint64_t)apid,0x1000))return -12;
        c->bt_apid=apid;
    }
    uint8_t gpio[8];int rc=gpio_read(c,gpio);
    if(rc||gpio[0]!=0x10||gpio[1]<0x10||gpio[1]>0x13)return rc?rc:-95;
    Region db;
    if(!db.map(0x81c60000,0x20000))return -12;
    Sm8750RpmhIo reader={&db,db_read,nullptr,nullptr};
    if((rc=Sm8750CmdDbCopy(&reader,&c->db)))return rc;
    for(unsigned i=0;i<EldenWcnRailCount;i++)
        if((rc=Sm8750CmdDbVrm(&c->db,elden_wcn_rail_specs[i].resource,&c->vrm[i])))return rc;
    uint32_t offset=c->aoss.read(0x38),size=c->aoss.read(0x34);
    if(c->aoss.read(0)!=Sm8750AossQmpMagic||c->aoss.read(4)!=1||(offset&3)||offset<0x3c||
       size<68||offset>0x400||size>0x400-offset||c->aoss.read(offset)!=0)return -71;
    return 0;
}
static void publish(Context *c){
    __atomic_store_n(&published_epoch,c->power.epoch,__ATOMIC_RELEASE);
    __atomic_store_n(&published_state,c->power.state,__ATOMIC_RELEASE);
}
static bool prepare_firmware(Context *c,unsigned mode){
    EldenWcnPeripheralPlan plan;
    if(elden_wcn_peripheral_prepare(EldenWcnPatch,elden_wcn_patch,sizeof(elden_wcn_patch),&plan)||
       elden_wcn_peripheral_prepare(EldenWcnNvm,elden_wcn_nvm,sizeof(elden_wcn_nvm),&plan)||
       elden_wcn_peripheral_prepare(EldenWcnTmel,elden_wcn_tmel,sizeof(elden_wcn_tmel),&plan))return false;
    EldenWcnHciFirmware hci;
    if(elden_wcn_hci_firmware_prepare(&hci,elden_wcn_bt_patch,sizeof(elden_wcn_bt_patch),
                                     elden_wcn_bt_nvm,sizeof(elden_wcn_bt_nvm)))return false;
    if(mode<3)return true;
    IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);if(!chosen)return false;
    OSData *data=OSDynamicCast(OSData,chosen->getProperty("unique-chip-id"));uint64_t seed=0;
    if(data&&data->getLength()==8)memcpy(&seed,data->getBytesNoCopy(),8);chosen->release();
    return seed&&pb_bdaddr_from_seed(seed,c->address)&&memcmp(c->address,hci.nvm_address,6);
}
static void worker(void *arg,wait_result_t){
    unsigned mode=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<12000&&(!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted);i++)IOSleep(10);
    if(!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted)goto done;
    {
        Context *c=new Context;
        if(!c||!prepare_firmware(c,mode)){if(c)delete c;log("\nELDEN_WCN refused firmware/address preflight\n");goto done;}
        if(!c->map()){delete c;log("\nELDEN_WCN refused mapping\n");goto done;}
        c->spmi={c,core_read,owner_read,obs_read,obs_write,chan_read,chan_write,delay};
        c->rpmh={c,rsc_read,rsc_write,delay};
        c->aoss_io={c,aoss_read,aoss_write,delay};c->ipcc_io={c,ipcc_read,ipcc_write,delay};
        Sm8750AossQmpInit(&c->qmp,&c->aoss_io,0x400,&c->ipcc_io);
        c->power.io={c,preflight,rail_read,rail_vote,qmp_send,bt_set,bt_get,wlan_get,parents,now_us,delay};
        int rc=preflight(c);uint32_t main=0,even=0;
        if(!rc)rc=parents(c,&main,&even);
        log("\nELDEN_WCN survey=%d parent=%u/%u rf-clk1=off apid=%d mode=%u\n",rc,main,even,c->bt_apid,mode);
        log("\nELDEN_WCN pll mode=%08x op=%08x l=%08x alpha=%08x user=%08x\n",
            c->gcc.read(0),c->gcc.read(4),c->gcc.read(0x10),c->gcc.read(0x14),c->gcc.read(0x18));
        for(unsigned i=0;!rc&&i<EldenWcnRailCount;i++){
            EldenWcnRailState s={};rc=rail_read(c,(EldenWcnRail)i,&s);
            log("\nELDEN_WCN supply=%u rc=%d mv=%u enable=%02x mode=%02x status=%02x\n",i,rc,s.mv,s.enable,s.mode,s.status);
        }
        if(rc||mode==1){delete c;goto done;}
        active=c;uint64_t epoch=__atomic_add_fetch(&next_epoch,1ULL,__ATOMIC_ACQ_REL);
        rc=elden_wcn_power_acquire(&c->power,epoch);publish(c);
        log("\nELDEN_WCN prepare=%d stage=%u epoch=%llu\n",rc,c->power.stage,(unsigned long long)epoch);
        if(rc)goto done; // maps and uncertain shared votes remain held
        void *uart=nullptr;EldenWcnStartupIo io={};
        if(mode>=3){
            rc=elden_wcn_uart_backend_open(epoch,&io,&uart);
            if(!rc)rc=elden_wcn_startup_prepare(&c->startup,&io,elden_wcn_patch,sizeof(elden_wcn_patch),
                elden_wcn_nvm,sizeof(elden_wcn_nvm),elden_wcn_tmel,sizeof(elden_wcn_tmel));
            if(!rc)rc=elden_wcn_startup_run(&c->startup);
            if(!rc)rc=elden_wcn_hci_prepare(&c->hci,&c->startup,elden_wcn_bt_patch,sizeof(elden_wcn_bt_patch),
                elden_wcn_bt_nvm,sizeof(elden_wcn_bt_nvm),c->address);
            if(!rc)rc=elden_wcn_hci_run(&c->hci);
            log("\nELDEN_WCN hci=%d ready=%u patch=%04x segments=%u\n",rc,c->hci.ready,c->hci.reported_patch,c->hci.segments);
#if defined(ELDEN_BT_NATIVE_BUILD)
            if(!rc&&mode==4){
                rc=elden_bt_host_publish(&io,c->address,&c->startup.h4,
                    c->startup.receive+c->startup.position,c->startup.received-c->startup.position);
                log("\nELDEN_WCN native-host=%d\n",rc);
                if(!rc)elden_bt_host_run();
            }
#endif
        }
        int stopped=elden_wcn_power_stop(&c->power,epoch);publish(c);
        int closed=stopped?stopped:uart?elden_wcn_uart_backend_close(uart):0;
        if(!stopped&&!closed){elden_wcn_power_finish(&c->power,epoch);publish(c);}
        log("\nELDEN_WCN stop=%d uart-close=%d state=%u boot-shared-votes-retained=1\n",stopped,closed,c->power.state);
        // Context remains the inherited/shared vote ledger until platform reset.
    }
done:
    thread_terminate(current_thread());
}
}
extern "C" int elden_wcn_power_lease_check(uint64_t epoch,unsigned operation){
    unsigned state=__atomic_load_n(&published_state,__ATOMIC_ACQUIRE);
    if(!active||epoch!=__atomic_load_n(&published_epoch,__ATOMIC_ACQUIRE))return -11;
    if(operation==3)return state==EldenWcnPowerStopped?0:-11;
    if(operation==1||operation==2)return state==EldenWcnPowerReady?0:-11;
    return -22;
}
extern "C" uint32_t elden_wcn_power_parent_hz(uint64_t epoch,unsigned source){
    if(!active||epoch!=__atomic_load_n(&published_epoch,__ATOMIC_ACQUIRE))return 0;
    unsigned state=__atomic_load_n(&published_state,__ATOMIC_ACQUIRE);
    if(state!=EldenWcnPowerReady&&state!=EldenWcnPowerStopped)return 0;
    uint32_t main=0,even=0;
    if(parents(active,&main,&even)){__atomic_store_n(&published_state,EldenWcnPowerFailed,__ATOMIC_RELEASE);return 0;}
    return source==1?main:source==6?even:0;
}
extern "C" void elden_wcn_power_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-wcn-power",&mode,sizeof(mode));
    if(!mode||mode>4||__atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
#if !defined(ELDEN_BT_NATIVE_BUILD)
    if(mode==4)return;
#endif
    thread_t t=nullptr;
    if(kernel_thread_start(worker,(void *)(uintptr_t)mode,&t)==KERN_SUCCESS)thread_deallocate(t);
}
