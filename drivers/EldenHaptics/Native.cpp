/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* One boot-lifetime worker owns SE6 and both AW86937 chips. User clients only
 * manipulate a bounded mailbox under its IOLock; no MMIO or gains are exposed.
 * The generated calibration remains absent by default. No driver/map/worker
 * starts unless explicit elden-haptics=1 is selected and recovery/root admit it.
 */
#include "Native.h"
#include "Platform.h"
#include "AwChip.h"
#include "Mailbox.h"
#include "EldenHapticsFirmware.h" // generated from exact original input, not in Git
#include "EldenHapticsCalibration.h" // default disabled; reviewed external limits only
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
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
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Window windows[7];IOLock *lock=nullptr;IOService *service=nullptr;
    EldenHapticsPlatform platform={};EldenHapticsBus bus={};EldenAwChip chips[2]={};
    EldenHapticsMailbox mailbox={};EldenHapticsQueue queue={};
    uint64_t owner=0,epoch=1,next_owner=1,cleanup_deadline=0;
    uint32_t client_live=0,phase=0,ready_mask=0,queue_ready=0,stopped=0,thermal_ready=0;
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
    bool ready=ELDEN_HAPTICS_HAS_LIMITS&&elden_battery_temperature_get(&temp,&epoch,&cycle)&&epoch&&cycle&&
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
       !__atomic_load_n(&c->client_live,__ATOMIC_ACQUIRE)))return -13;
    return 0;
}
static int firmware(void *,const uint8_t *p,size_t n){
    if(!p||n!=sizeof(elden_haptics_firmware))return -84;
    for(size_t i=0;i<n;i++)if(p[i]!=elden_haptics_firmware[i])return -84;return 0;
}
static int chip_delay(void *v,unsigned low,unsigned high){if(low>high||high>500000)return -22;return delay_us(v,low);}
static int queue_admit(void *v,uint64_t owner,uint64_t epoch){
    Context *c=static_cast<Context *>(v);
    if(__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE)||!__atomic_load_n(&c->client_live,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&c->owner,__ATOMIC_ACQUIRE)!=owner||c->epoch!=epoch)return -125;
    return thermal(c)?0:-13;
}
static int queue_start(void *v,unsigned motor,uint8_t wave,uint8_t gain,uint32_t duration){
    Context *c=static_cast<Context *>(v);return motor<2?elden_aw_ram_start(&c->chips[motor],wave,gain,duration):-22;
}
static int queue_poll(void *v,unsigned motor){return motor<2?elden_aw_poll(&static_cast<Context *>(v)->chips[motor]):-22;}
static int queue_stop(void *v,unsigned motor){return motor<2?elden_aw_stop(&static_cast<Context *>(v)->chips[motor]):-22;}
static void completed(void *v,const EldenHapticsResult *r){
    Context *c=static_cast<Context *>(v);IOLockLock(c->lock);int rc=elden_haptics_mail_finish(&c->mailbox,r);IOLockUnlock(c->lock);
    if(rc)__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);
}
static int prepare(Context *c){
    c->maps_ready=c->map();if(!c->maps_ready)return -12;
    EldenHapticsPlatformIo io={c,read,write,platform_admit,gcc_lock,touch_contract,now_us,delay_us};
    int rc=elden_haptics_platform_acquire(&c->platform,&io,400000);if(rc)return rc;
    EldenHapticsBusIo bus_io;
    if((rc=elden_haptics_platform_bus_io(&c->platform,&bus_io))||(rc=elden_haptics_bus_acquire(&c->bus,&bus_io)))return rc;
    EldenAwIo chip_io={c,chip_read,chip_write,chip_admit,firmware,now_us,chip_delay};
    for(unsigned m=0;m<2;m++){
        if((rc=elden_aw_bind(&c->chips[m],&chip_io,(uint8_t)(0x5a+m)))||
           (rc=elden_aw_initialize(&c->chips[m],elden_haptics_firmware,sizeof(elden_haptics_firmware),elden_haptics_max_gain[m])))return rc;
        __atomic_fetch_or(&c->ready_mask,1u<<m,__ATOMIC_RELEASE);
    }
    if(ELDEN_HAPTICS_HAS_LIMITS){
        EldenHapticsQueueIo qio={c,queue_admit,queue_start,queue_poll,queue_stop,completed};
        rc=elden_haptics_queue_init(&c->queue,&elden_haptics_limits,&qio);if(rc)return rc;
        __atomic_store_n(&c->queue_ready,1u,__ATOMIC_RELEASE);
    }
    return 0;
}
static int stop(Context *c){
    __atomic_store_n(&c->client_live,0u,__ATOMIC_RELEASE);
    IOLockLock(c->lock);elden_haptics_mail_stop(&c->mailbox);IOLockUnlock(c->lock);
    c->cleanup_deadline=now_us(c)+1000000;c->platform.cleanup=1;
    int error=0;
    if(c->queue_ready){
        int rc=elden_haptics_queue_stop(&c->queue,now_us(c));if(rc)error=rc;
        if(!elden_haptics_queue_terminal(&c->queue)&&!error)error=-16;
    }
    // Even partial init or failed GO may leave a physical chip active.
    for(unsigned m=0;m<2;m++)if(c->chips[m].io.admit){int rc=elden_aw_stop(&c->chips[m]);if(rc&&!error)error=rc;}
    if(error||c->bus.held||c->platform.held)return error?error:-125;
    if(c->bus.acquired){int rc=elden_haptics_bus_release(&c->bus);if(rc)return rc;}
    if(c->platform.owned){int rc=elden_haptics_platform_release(&c->platform,!c->bus.acquired);if(rc)return rc;}
    return 0;
}
static bool process(Context *c){
    EldenHapticsRequest r;uint16_t intensity=0;
    IOLockLock(c->lock);int cancel=elden_haptics_mail_next_cancel(&c->mailbox,&r);IOLockUnlock(c->lock);
    if(!cancel)(void)elden_haptics_queue_cancel(&c->queue,r.owner,r.epoch,r.id);
    IOLockLock(c->lock);int take=elden_haptics_mail_take(&c->mailbox,&r,&intensity);IOLockUnlock(c->lock);
    if(!take){
        int rc=elden_haptics_queue_submit(&c->queue,r.owner,r.epoch,r.id,r.not_before_us,r.duration_us,r.mask,r.wave,intensity);
        if(rc){EldenHapticsResult result={r.owner,r.epoch,r.id,EldenHapticsFailed,rc};completed(c,&result);}
    }
    if(c->queue_ready){int rc=elden_haptics_queue_step(&c->queue,now_us(c));
        if(c->queue.held||c->queue.stopping){__atomic_store_n(&c->error,rc?rc:c->queue.error,__ATOMIC_RELEASE);return false;}}
    return true;
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
        IOLockLock(c->lock);bool work=elden_haptics_mail_work(&c->mailbox);IOLockUnlock(c->lock);
        IOSleep(work?1:10);
    }
    __atomic_store_n(&c->phase,3u,__ATOMIC_RELEASE);int stopped=stop(c);
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
    IOLockLock(c->lock);int rc=-16;
    if(c->next_owner!=UINT64_MAX){uint64_t next=c->next_owner;rc=elden_haptics_mail_open(&c->mailbox,next,c->epoch);
        if(!rc){++c->next_owner;__atomic_store_n(&c->owner,next,__ATOMIC_RELEASE);__atomic_store_n(&c->client_live,1u,__ATOMIC_RELEASE);*owner=next;*epoch=c->epoch;}}
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_haptics_native_close(uint64_t owner,uint64_t epoch){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    IOLockLock(c->lock);int rc=elden_haptics_mail_close(&c->mailbox,owner,epoch);
    if(!rc)__atomic_store_n(&c->client_live,0u,__ATOMIC_RELEASE);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_haptics_native_submit(uint64_t owner,uint64_t epoch,uint64_t id,uint64_t delay,uint32_t duration,uint8_t mask,uint8_t wave,uint16_t intensity){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);
    if(!c||!__atomic_load_n(&c->queue_ready,__ATOMIC_ACQUIRE)||__atomic_load_n(&stop_requested,__ATOMIC_ACQUIRE))return -13;
    if(!id||delay>1000000||!duration||duration>elden_haptics_limits.max_duration_us||!mask||mask&~3u||!wave||wave>c->chips[0].waves||
       intensity>32767||!thermal(c))return -13;
    uint64_t now=now_us(c);if(now>UINT64_MAX-delay)return -22;
    EldenHapticsRequest r={owner,epoch,id,now+delay,duration,mask,wave,{0,0},0};
    IOLockLock(c->lock);int rc=elden_haptics_mail_submit(&c->mailbox,&r,intensity);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_haptics_native_cancel(uint64_t owner,uint64_t epoch,uint64_t id){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    IOLockLock(c->lock);int rc=elden_haptics_mail_cancel(&c->mailbox,owner,epoch,id);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_haptics_native_result(uint64_t owner,uint64_t epoch,uint64_t id,EldenHapticsResult *result){
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c)return -19;
    IOLockLock(c->lock);int rc=elden_haptics_mail_result(&c->mailbox,owner,epoch,id,result);IOLockUnlock(c->lock);return rc;
}
extern "C" void elden_haptics_native_info(uint64_t output[8]){
    if(!output)return;for(unsigned i=0;i<8;i++)output[i]=0;output[0]=1;
    Context *c=__atomic_load_n(&active,__ATOMIC_ACQUIRE);if(!c){output[5]=(uint64_t)(int64_t)__atomic_load_n(&startup_error,__ATOMIC_ACQUIRE);return;}
    output[1]=__atomic_load_n(&c->phase,__ATOMIC_ACQUIRE);output[2]=__atomic_load_n(&c->ready_mask,__ATOMIC_ACQUIRE);
    output[3]=ELDEN_HAPTICS_HAS_LIMITS;output[4]=__atomic_load_n(&c->thermal_ready,__ATOMIC_ACQUIRE);
    output[5]=(uint64_t)(int64_t)__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    output[6]=__atomic_load_n(&c->stopped,__ATOMIC_ACQUIRE);output[7]=__atomic_load_n(&c->queue_ready,__ATOMIC_ACQUIRE);
}
class EldenHapticsClient final : public IOUserClient {
    OSDeclareDefaultStructors(EldenHapticsClient)
    uint64_t owner=0,epoch=0;
public:
    bool start(IOService *provider) override{
        if(!IOUserClient::start(provider))return false;
        if(elden_haptics_native_open(&owner,&epoch)){IOUserClient::stop(provider);return false;}return true;
    }
    IOReturn clientClose() override{if(owner){(void)elden_haptics_native_close(owner,epoch);owner=0;}terminate();return kIOReturnSuccess;}
    void free() override{if(owner){(void)elden_haptics_native_close(owner,epoch);owner=0;}IOUserClient::free();}
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) override{
        if(!a||!owner||a->structureInputSize||a->structureOutputSize||a->structureInputDescriptor||a->structureOutputDescriptor)return kIOReturnBadArgument;
        int rc=-22;
        if(selector==0&&a->scalarInputCount==0&&a->scalarOutputCount==8&&a->scalarOutput){elden_haptics_native_info(a->scalarOutput);return kIOReturnSuccess;}
        if(selector==1&&a->scalarInputCount==6&&a->scalarInput&&a->scalarOutputCount==0){
            const uint64_t *in=a->scalarInput;
            if(in[2]>UINT32_MAX||in[3]>3||in[4]>127||in[5]>32767)return kIOReturnBadArgument;
            rc=elden_haptics_native_submit(owner,epoch,in[0],in[1],(uint32_t)in[2],(uint8_t)in[3],(uint8_t)in[4],(uint16_t)in[5]);
        }else if(selector==2&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==0)rc=elden_haptics_native_cancel(owner,epoch,a->scalarInput[0]);
        else if(selector==3&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==3&&a->scalarOutput){
            EldenHapticsResult result;rc=elden_haptics_native_result(owner,epoch,a->scalarInput[0],&result);
            if(!rc){a->scalarOutput[0]=result.state;a->scalarOutput[1]=(uint64_t)(int64_t)result.error;a->scalarOutput[2]=result.id;}
        }
        return rc==0?kIOReturnSuccess:rc==-16?kIOReturnBusy:rc==-13||rc==-125?kIOReturnNotPermitted:rc==-116?kIOReturnNotFound:rc==-19?kIOReturnNotReady:kIOReturnBadArgument;
    }
};
OSDefineMetaClassAndStructors(EldenHapticsClient,IOUserClient)
class EldenHapticsService final : public IOService {
    OSDeclareDefaultStructors(EldenHapticsService)
    IOReturn client(task_t task,void *security,UInt32 type,OSDictionary *properties,IOUserClient **out){
        if(out)*out=nullptr;if(!out||type!=0)return kIOReturnBadArgument;
        if(IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPermitted;
        EldenHapticsClient *c=OSTypeAlloc(EldenHapticsClient);if(!c)return kIOReturnNoMemory;
        if(!c->initWithTask(task,security,type,properties)||!c->attach(this)){c->release();return kIOReturnError;}
        if(!c->start(this)){c->detach(this);c->release();return kIOReturnNotReady;}*out=c;return kIOReturnSuccess;
    }
public:
    bool start(IOService *provider) override{
        if(!IOService::start(provider))return false;setName("EldenHaptics");
        if(!setProperty("EldenHapticsProtocolVersion",1u,32)||!setProperty("CalibrationAvailable",(bool)ELDEN_HAPTICS_HAS_LIMITS)){
            IOService::stop(provider);return false;}return true;
    }
    IOReturn newUserClient(task_t task,void *s,UInt32 t,OSDictionary *p,IOUserClient **out) override{return client(task,s,t,p,out);}
    IOReturn newUserClient(task_t task,void *s,UInt32 t,IOUserClient **out) override{return client(task,s,t,nullptr,out);}
};
OSDefineMetaClassAndStructors(EldenHapticsService,IOService)
namespace {
static bool publish(Context *c){
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!handle)return false;
    elden_haptics_static_init();if(OSMetaClass::postModLoad(handle))return false;
    EldenHapticsService *service=OSTypeAlloc(EldenHapticsService);if(!service)return false;
    // Retain the service to reset, including failed partial registration.
    c->service=service;
    if(!service->init()||!service->attach(IOService::getPlatform())||!service->start(IOService::getPlatform()))return false;
    service->registerService();return true;
}
}
