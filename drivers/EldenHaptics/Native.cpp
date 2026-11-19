/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* One boot-lifetime worker owns SE6 and both AW86937 chips. User clients only
 * manipulate a bounded mailbox under its IOLock; no MMIO or gains are exposed.
 * The generated calibration remains absent by default. No driver/map/worker
 * starts unless explicit elden-haptics=1 is selected and recovery/root admit it.
 */
#include "Native.h"
#include "Platform.h"
#include "AwChip.h"
#include "Broker.h"
#include "EldenHapticsFirmware.h" // generated from exact original input, not in Git
#include "EldenHapticsCalibration.h" // default disabled; reviewed external limits only
#include "EldenHapticsCatalog.h" // immutable measured effect clips, default empty
#include "ActuatorLive.h"
#include "EldenActuatorCalibration.h" // absent measured live PCM profile is default-off
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <kern/task.h>
#include <sys/proc.h>
#include <sys/codesign.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
extern kmod_info_t g_kernel_kmod_info;
extern "C" {
__attribute__((weak)) volatile uint32_t sm8750_gcc_vote_owner;
uint32_t elden_touch_shared_gcc_contract(void);
int elden_recovery_armed(void);
extern volatile int native_owner_data_status,elden_init_exec_accepted;
void elden_usb_log_putc(char);
bool elden_battery_temperature_get(int *,uint32_t *,uint32_t *);
const EldenActuatorLimits *elden_actuator_profile_get(void);
// The captured headers place getBytesNoCopy at a different vtable slot from
// OS17's original IOBuffer class. Bind its exact exported method directly;
// an explicitly qualified C++ virtual still uses the captured table in kext mode.
void *elden_actuator_buffer_bytes(IOBufferMemoryDescriptor *) __asm__("__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv");
boolean_t ml_at_interrupt_context(void);
void elden_haptics_static_init(void) __asm__("__GLOBAL__sub_I_EldenHapticsNative.cpp");
}
namespace {
constexpr uint32_t GccOwner=0x48415036u;
struct Window {
    IOMemoryMap *keep=nullptr;uintptr_t va=0;unsigned bytes=0;
    // Every map is retained to physical reset, including partial failures.
    bool map(uint64_t pa,unsigned size){
        if(keep)return false;IODeviceMemory *m=IODeviceMemory::withRange(pa,size);if(!m)return false;
        keep=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        va=keep?(uintptr_t)keep->getVirtualAddress():0;bytes=size;return va!=0;
    }
    int read(uint32_t reg,uint32_t *out){
        if(!out||!va||bytes<4||(reg&3)||reg>bytes-4)return -14;uintptr_t p=va+reg;
        __asm__ volatile("ldr %w0,[%1]":"=r"(*out):"r"(p):"memory");return 0;
    }
    int write(uint32_t reg,uint32_t value){
        if(!va||bytes<4||(reg&3)||reg>bytes-4)return -14;uintptr_t p=va+reg;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(p):"memory");return 0;
    }
};
struct ActuatorSession;
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Window windows[7];IOLock *lock=nullptr;IOService *service=nullptr;
    EldenHapticsPlatform platform={};EldenHapticsBus bus={};EldenAwChip chips[2]={};
    EldenHapticsBroker broker={};
    struct ActuatorSession *actuator=nullptr,*retired_actuators=nullptr;
    const EldenActuatorLimits *actuator_profile=nullptr;
    uint32_t actuator_enabled=0,actuator_opens=0;
    uint64_t cleanup_deadline=0;
    uint32_t phase=0,ready_mask=0,queue_ready=0,stopped=0,thermal_ready=0;
    int error=0;bool maps_ready=false;
    bool map(){
        return windows[EldenHapticsGcc].map(ELDEN_HAPTICS_GCC_BASE,ELDEN_HAPTICS_GCC_BYTES)&&
            windows[EldenHapticsSe].map(ELDEN_HAPTICS_SE_BASE,ELDEN_HAPTICS_SE_BYTES)&&
            windows[EldenHapticsPins].map(ELDEN_HAPTICS_TLMM_BASE+ELDEN_HAPTICS_SDA*ELDEN_HAPTICS_GPIO_STRIDE,0x2000)&&
            windows[EldenHapticsLeftReset].map(ELDEN_HAPTICS_TLMM_BASE+ELDEN_HAPTICS_RESET_LEFT*ELDEN_HAPTICS_GPIO_STRIDE,0x1000)&&
            windows[EldenHapticsRightReset].map(ELDEN_HAPTICS_TLMM_BASE+ELDEN_HAPTICS_RESET_RIGHT*ELDEN_HAPTICS_GPIO_STRIDE,0x1000)&&
            windows[EldenHapticsLeftIrq].map(ELDEN_HAPTICS_TLMM_BASE+ELDEN_HAPTICS_IRQ_LEFT*ELDEN_HAPTICS_GPIO_STRIDE,0x1000)&&
            windows[EldenHapticsRightIrq].map(ELDEN_HAPTICS_TLMM_BASE+ELDEN_HAPTICS_IRQ_RIGHT*ELDEN_HAPTICS_GPIO_STRIDE,0x1000);
    }
};
static Context *active;
static uint32_t started,stop_requested,worker_finished;
static int startup_error;
static void finish_without_hardware(int error){
    __atomic_store_n(&startup_error,error,__ATOMIC_RELEASE);__atomic_store_n(&worker_finished,1u,__ATOMIC_RELEASE);
}
static void log(const char *text){for(const char *p=text;*p;p++)elden_usb_log_putc(*p);}
static uint64_t now_us(void *){
    uint64_t t;__asm__ volatile("mrs %0,cntpct_el0":"=r"(t));return t/96u*5u+t%96u*5u/96u;
}
static int delay_us(void *,unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);return 0;}
static bool thermal(Context *c){
    int temp;uint32_t epoch,cycle;
    bool ready=(ELDEN_HAPTICS_HAS_LIMITS||ELDEN_HAPTICS_HAS_CATALOG||ELDEN_HAPTICS_HAS_ACTUATOR)&&elden_battery_temperature_get(&temp,&epoch,&cycle)&&epoch&&cycle&&
        temp>=ELDEN_HAPTICS_MIN_TEMP_CENTI&&temp<=ELDEN_HAPTICS_MAX_TEMP_CENTI;
    __atomic_store_n(&c->thermal_ready,ready?1u:0u,__ATOMIC_RELEASE);return ready;
}
static int platform_admit(void *v,int cleanup){
    Context *c=static_cast<Context *>(v);(void)cleanup;
    // Only this original worker calls bus/chip IO. A requested stop closes
    // public admission, but permits its in-flight transfer and real cleanup.
    if(!c->maps_ready||!elden_recovery_armed()||ml_at_interrupt_context())return -13;
    if(c->cleanup_deadline&&now_us(c)>=c->cleanup_deadline)return -110;
    return 0;
}
static int read(void *v,EldenHapticsSpace s,uint32_t reg,uint32_t *out){
    Context *c=static_cast<Context *>(v);return (unsigned)s<7?c->windows[s].read(reg,out):-22;
}
static int write(void *v,EldenHapticsSpace s,uint32_t reg,uint32_t value){
    Context *c=static_cast<Context *>(v);
    // The platform has no reset/IRQ/PMIC writer. All motor registers use GENI.
    return (unsigned)s<=EldenHapticsPins?c->windows[s].write(reg,value):-22;
}
static int gcc_lock(void *,int acquire){
    if(!acquire){uint32_t owner=GccOwner;return __atomic_compare_exchange_n(&sm8750_gcc_vote_owner,&owner,0u,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)?0:-71;}
    for(unsigned i=0;i<100;i++){uint32_t empty=0;
        if(__atomic_compare_exchange_n(&sm8750_gcc_vote_owner,&empty,GccOwner,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;IODelay(1);
    }
    return -16;
}
static uint32_t touch_contract(void *){return elden_touch_shared_gcc_contract();}
static int chip_read(void *v,uint8_t addr,uint8_t reg,uint8_t *out,size_t n){return elden_haptics_bus_read(&static_cast<Context *>(v)->bus,addr,reg,out,n);}
static int chip_write(void *v,uint8_t addr,uint8_t reg,const uint8_t *in,size_t n){return elden_haptics_bus_write(&static_cast<Context *>(v)->bus,addr,reg,in,n);}
static int chip_admit(void *v,uint8_t addr,EldenAwOperation op){
    Context *c=static_cast<Context *>(v);if(addr!=0x5a&&addr!=0x5b)return -22;
    int rc=platform_admit(c,op==EldenAwStop);if(rc)return rc;
    if(op==EldenAwPlay&&(__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||!thermal(c)||
       elden_broker_output_admit(&c->broker)))return -13;
    return 0;
}
static int firmware(void *,const uint8_t *p,size_t n){
    if(!p||n!=sizeof(elden_haptics_firmware))return -84;
    for(size_t i=0;i<n;i++)if(p[i]!=elden_haptics_firmware[i])return -84;return 0;
}
static int chip_delay(void *v,unsigned low,unsigned high){if(low>high||high>500000)return -22;return delay_us(v,low);}
static int broker_admit(void *v,int cleanup){
    Context *c=static_cast<Context *>(v);int rc=platform_admit(c,cleanup);if(rc)return rc;
    if(!cleanup&&(__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||!thermal(c)))return -13;return 0;
}
static void broker_lock(void *v,int acquire){Context *c=static_cast<Context *>(v);if(acquire)IOLockLock(c->lock);else IOLockUnlock(c->lock);}
static void *broker_allocate(void *,size_t bytes){return bytes&&bytes<=1440000?IOMalloc(bytes):nullptr;}
static void broker_release(void *,void *p,size_t bytes){if(p&&bytes)IOFree(p,bytes);}
#include "NativeActuator.inc"
static int prepare(Context *c){
    c->maps_ready=c->map();if(!c->maps_ready)return -12;
    EldenHapticsPlatformIo io={c,read,write,platform_admit,gcc_lock,touch_contract,now_us,delay_us};
    int rc=elden_haptics_platform_acquire(&c->platform,&io,ELDEN_HAPTICS_BUS_HZ);if(rc)return rc;
    EldenHapticsBusIo bus_io;
    if((rc=elden_haptics_platform_bus_io(&c->platform,&bus_io))||(rc=elden_haptics_bus_acquire(&c->bus,&bus_io)))return rc;
    EldenAwIo chip_io={c,chip_read,chip_write,chip_admit,firmware,now_us,chip_delay};
    for(unsigned m=0;m<2;m++){
        if((rc=elden_aw_bind(&c->chips[m],&chip_io,(uint8_t)(0x5a+m)))||
           (rc=elden_aw_initialize(&c->chips[m],elden_haptics_firmware,sizeof(elden_haptics_firmware),elden_haptics_max_gain[m])))return rc;
        __atomic_fetch_or(&c->ready_mask,1u<<m,__ATOMIC_RELEASE);
    }
    EldenHapticsBrokerIo bio={c,broker_admit,broker_lock,now_us,broker_allocate,broker_release};
    EldenAwChip *chips[]={&c->chips[0],&c->chips[1]};
    rc=elden_broker_init(&c->broker,&bio,chips,ELDEN_HAPTICS_HAS_LIMITS?&elden_haptics_limits:nullptr,
        ELDEN_HAPTICS_HAS_LIMITS||ELDEN_HAPTICS_HAS_CATALOG||ELDEN_HAPTICS_HAS_ACTUATOR?&elden_haptics_shared_limits:nullptr,
        ELDEN_HAPTICS_HAS_CATALOG?&elden_haptics_rtp_limits:nullptr,elden_haptics_effects,ELDEN_HAPTICS_EFFECT_COUNT);
    if(rc)return rc;__atomic_store_n(&c->queue_ready,c->broker.ram_ready?1u:0u,__ATOMIC_RELEASE);
    actuator_mode(c);
    return 0;
}
static int stop(Context *c){
    c->cleanup_deadline=now_us(c)+1000000;c->platform.cleanup=1;
    int error=0;
    error=actuator_shutdown(c);
    if(c->broker.initialized){int rc=elden_broker_stop(&c->broker);if(rc&&!error)error=rc;}
    // Even partial init or failed GO may leave a physical chip active.
    for(unsigned m=0;m<2;m++)if(c->chips[m].io.admit){int rc=elden_aw_stop(&c->chips[m]);if(rc&&!error)error=rc;}
    if(error||c->bus.held||c->platform.held)return error?error:-125;
    if(c->bus.acquired){int rc=elden_haptics_bus_release(&c->bus);if(rc)return rc;}
    if(c->platform.owned){int rc=elden_haptics_platform_release(&c->platform,!c->bus.acquired);if(rc)return rc;}
    return 0;
}
static bool process(Context *c){
    int actuator_rc=actuator_round(c);
    if(actuator_rc&&c->broker.held){__atomic_store_n(&c->error,actuator_rc,__ATOMIC_RELEASE);return false;}
    int rc=elden_broker_step(&c->broker);
    if(c->broker.held||c->broker.stopping){__atomic_store_n(&c->error,rc?rc:c->broker.error,__ATOMIC_RELEASE);return false;}return true;
}
static bool publish(Context *);
static void worker(void *,wait_result_t){
    for(unsigned i=0;i<3000&&!__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)&&
        (!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted);i++)IOSleep(10);
    if(__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted){finish_without_hardware(-19);return;}
    uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));if(hz!=19200000){finish_without_hardware(-71);return;}
    Context *c=new Context;if(!c){finish_without_hardware(-12);return;}c->lock=IOLockAlloc();if(!c->lock){finish_without_hardware(-12);return;}
    __atomic_store_n(&active,c,__ATOMIC_RELEASE);__atomic_store_n(&c->phase,1u,__ATOMIC_RELEASE);
    int rc=prepare(c);if(!rc&&!publish(c))rc=-19;
    if(!rc){__atomic_store_n(&c->phase,2u,__ATOMIC_RELEASE);log("\nELDEN_HAPTICS service ready; raw register/gain access disabled\n");}
    else{__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);log("\nELDEN_HAPTICS initialization refused; retained to reset\n");}
    while(!rc&&!__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)){
        if(!process(c))break;
        bool work=elden_broker_work(&c->broker)!=0||__atomic_load_n(&c->actuator,__ATOMIC_ACQUIRE)!=nullptr;
        IOSleep(work?1:10);
    }
    IOLockLock(c->lock);__atomic_store_n(&c->phase,3u,__ATOMIC_RELEASE);IOLockUnlock(c->lock);
    int stopped=stop(c);
    if(stopped)__atomic_store_n(&c->error,stopped,__ATOMIC_RELEASE);
    __atomic_store_n(&c->stopped,stopped?2u:1u,__ATOMIC_RELEASE);
    __atomic_store_n(&c->phase,4u,__ATOMIC_RELEASE);log(stopped?"\nELDEN_HAPTICS stop unconfirmed; maps retained\n":"\nELDEN_HAPTICS stopped; maps retained\n");
    __atomic_store_n(&worker_finished,1u,__ATOMIC_RELEASE);
}
}
extern "C" void elden_haptics_native_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-haptics",&mode,sizeof(mode));if(mode!=1)return;
    uint32_t empty=0;if(!__atomic_compare_exchange_n(&started,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    thread_t t=nullptr;if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
    else{__atomic_store_n(&stop_requested,1u,__ATOMIC_RELEASE);finish_without_hardware(-12);}
}
extern "C" int elden_haptics_stop_for_reboot(void){
    __atomic_store_n(&stop_requested,1u,__ATOMIC_RELEASE);
    if(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))return 0;
    if(ml_at_interrupt_context())return -16;
    for(unsigned i=0;i<1200;i++){
        Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
        if(c){uint32_t state=__atomic_load_n(&c->stopped,__ATOMIC_ACQUIRE);if(state)return state==1?0:-125;}
        else if(__atomic_load_n(&worker_finished,__ATOMIC_ACQUIRE))return 0;
        IOSleep(1);
    }
    return -16;
}
extern "C" int elden_haptics_native_open(uint64_t *owner,uint64_t *epoch){
    if(!owner||!epoch)return -22;Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if(!c||__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))return -19;
    return elden_broker_open(&c->broker,owner,epoch);
}
extern "C" int elden_haptics_native_close(uint64_t owner,uint64_t epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    return elden_broker_close(&c->broker,owner,epoch);
}
extern "C" int elden_haptics_native_submit(uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay,uint32_t duration,uint8_t mask,uint8_t wave,uint16_t intensity){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if(!c||__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))return -13;
    return elden_broker_submit_ram(&c->broker,owner,epoch,id,delay,duration,mask,wave,intensity);
}
extern "C" int elden_haptics_native_submit_effect(uint64_t owner,uint64_t epoch,uint64_t id,uint32_t effect,uint64_t delay,uint16_t intensity){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if(!c||__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))return -13;
    return elden_broker_submit_effect(&c->broker,owner,epoch,id,effect,delay,intensity);
}
extern "C" int elden_haptics_native_effect_info(uint32_t id,uint64_t output[4]){
    if(!output)return -22;
    const EldenHapticsEffect *e=elden_haptics_catalog_find(elden_haptics_effects,ELDEN_HAPTICS_EFFECT_COUNT,id);
    if(!e)return -116;output[0]=e->id;output[1]=e->frames;output[2]=e->mask;output[3]=24000;return 0;
}
extern "C" int elden_haptics_native_cancel(uint64_t owner,uint64_t epoch,uint64_t id){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    return elden_broker_cancel(&c->broker,owner,epoch,id);
}
extern "C" int elden_haptics_native_result(uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *result){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    return elden_broker_result(&c->broker,owner,epoch,id,result);
}
extern "C" void elden_haptics_native_info(uint64_t output[8]){
    if(!output)return;for(unsigned i=0;i<8;i++)output[i]=0;output[0]=2;
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c){output[5]=(uint64_t)(int64_t)__atomic_load_n(&startup_error,__ATOMIC_ACQUIRE);return;}
    output[1]=__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE);output[2]=__atomic_load_n(&c->ready_mask,__ATOMIC_ACQUIRE);
    output[3]=(ELDEN_HAPTICS_HAS_LIMITS?1u:0u)|(ELDEN_HAPTICS_HAS_CATALOG?2u:0u);output[4]=__atomic_load_n(&c->thermal_ready,__ATOMIC_ACQUIRE);
    output[5]=(uint64_t)(int64_t)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    output[6]=__atomic_load_n(&c->stopped,__ATOMIC_ACQUIRE);output[7]=__atomic_load_n(&c->queue_ready,__ATOMIC_ACQUIRE);
}
extern "C" int elden_haptics_actuator_allowed(void *task){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if(!c||!__atomic_load_n(&c->actuator_enabled,__ATOMIC_ACQUIRE)||!task)return 0;
    proc_t process=(proc_t)get_bsdtask_info((task_t)task);if(!process||!csproc_get_platform_binary(process))return 0;
    char name[32]={0};proc_name(proc_pid(process),name,sizeof(name));
    return !strcmp(name,"audiomxd")||!strcmp(name,"mediaserverd");
}
extern "C" int elden_haptics_actuator_open(uint64_t *owner,uint64_t *epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?actuator_open(c,owner,epoch):-19;
}
extern "C" int elden_haptics_actuator_close(uint64_t owner,uint64_t epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?actuator_close(c,owner,epoch):-19;
}
extern "C" int elden_haptics_actuator_request(uint64_t owner,uint64_t epoch,int start){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?actuator_request(c,owner,epoch,start):-19;
}
extern "C" int elden_haptics_actuator_info(uint64_t owner,uint64_t epoch,uint64_t output[8]){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?actuator_info(c,owner,epoch,output):-19;
}
extern "C" IOMemoryDescriptor *elden_haptics_actuator_memory(uint64_t owner,uint64_t epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);return c?actuator_memory(c,owner,epoch):nullptr;
}
#include "ClientMethods.inc"
namespace {
static bool publish(Context *c){
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!handle)return false;
    elden_haptics_static_init();if(OSMetaClass::postModLoad(handle))return false;
    EldenHapticsService *service=OSTypeAlloc(EldenHapticsService);if(!service)return false;
    // Retain the service to reset, including failed partial registration.
    c->service=service;
    if(!service->init()||!service->attach(IOService::getPlatform())||!service->start(IOService::getPlatform()))return false;
    if(__atomic_load_n(&c->actuator_enabled,__ATOMIC_ACQUIRE)){
        service->setProperty("NativeSandboxGrantProcess","audiomxd,mediaserverd");
        service->setProperty("EldenActuatorABI",ELDEN_ACTUATOR_VERSION,32);
    }
    // Matching notifications may open the service immediately. Publish the
    // admitted phase before registerService, after broker/chips are ready.
    IOLockLock(c->lock);
    bool admitted=!__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE);
    if(admitted)__atomic_store_n(&c->phase,2u,__ATOMIC_RELEASE);
    IOLockUnlock(c->lock);if(!admitted)return false;
    service->registerService();return true;
}
}
