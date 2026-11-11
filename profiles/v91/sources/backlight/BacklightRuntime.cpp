/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Genuine original AppleARMBacklight ancestry, with an owned IOService
 * lifecycle. ABI header/bindings are generated only after exact 21H461 checks.
 * Board transactions reuse sheng's GENI core, never its SM8550 addresses. */
#include "OriginalBacklightABI.h"
#include "Platform.h"
#include "Aw99706.h"
#include "Brightness.h"
#include "SettledTrial.h"
#include "Lifecycle.h"
#include "EldenBacklightResources.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSDictionary.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
extern kmod_info_t g_kernel_kmod_info;
extern "C" {
void elden_usb_log_putc(char);
void elden_backlight_static_init(void) __asm__("__GLOBAL__sub_I_BacklightRuntime.cpp");
__attribute__((weak)) volatile uint32_t sm8750_gcc_vote_owner;
unsigned elden_recovery_deadline_remaining(void) __attribute__((weak_import));
}
namespace {
static unsigned lease;
static EldenBlLifecycle lifecycle;
static volatile unsigned restoreConfirmed=1;
static bool stopping(){return elden_bl_worker_stopping(&lifecycle);}
static bool sleepChecked(unsigned ms){
    while(ms&&!stopping()){unsigned step=ms>100?100:ms;IOSleep(step);ms-=step;}
    return !stopping();
}
constexpr uint32_t GccOwner=0x424c4932;
static void report(const char *format,...){
    char text[352];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);
    for(const char *p=text;*p;++p)elden_usb_log_putc(*p);
}
struct Region {
    IOMemoryMap *mapping=nullptr;uintptr_t base=0;unsigned bytes=0;
    ~Region(){if(mapping)mapping->release();}
    bool map(uint64_t pa,unsigned size){
        IODeviceMemory *m=IODeviceMemory::withRange(pa,size);if(!m)return false;
        mapping=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
        base=mapping?(uintptr_t)mapping->getVirtualAddress():0;bytes=size;return base!=0;
    }
    uint32_t read(uint32_t offset){
        if(!base||bytes<4||(offset&3)||offset>bytes-4)return 0xffffffffu;
        uint32_t v;__asm__ volatile("ldr %w0,[%1]":"=r"(v):"r"(base+offset):"memory");return v;
    }
    void write(uint32_t offset,uint32_t value){
        if(!base||bytes<4||(offset&3)||offset>bytes-4)return;
        __asm__ volatile("str %w0,[%1]\n\tdsb sy"::"r"(value),"r"(base+offset):"memory");
    }
};
struct Context {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Context));}
    Region region[4];EldenBlPlatform platform={};EldenAw99706 chip={};
    unsigned original=0;bool originalValid=false;
    bool map(){
        return region[EldenBlGcc].map(ELDEN_BL_GCC_BASE,ELDEN_BL_GCC_BYTES)&&
            region[EldenBlPins].map(ELDEN_BL_TLMM_BASE+ELDEN_BL_SDA*ELDEN_BL_GPIO_STRIDE,0x2000)&&
            region[EldenBlEnable].map(ELDEN_BL_TLMM_BASE+ELDEN_BL_HWEN*ELDEN_BL_GPIO_STRIDE,0x1000)&&
            region[EldenBlSe].map(ELDEN_BL_SE_BASE,ELDEN_BL_SE_BYTES);
    }
};
static uint32_t read(void *p,EldenBlSpace s,uint32_t r){return static_cast<Context *>(p)->region[s].read(r);}
static void write(void *p,EldenBlSpace s,uint32_t r,uint32_t v){static_cast<Context *>(p)->region[s].write(r,v);}
static uint64_t now_us(void *){
    uint64_t t;__asm__ volatile("mrs %0,cntpct_el0":"=r"(t));return t/96*5+t%96*5/96;
}
static void delay_us(void *,unsigned us){IODelay(us);}
static int gcc_lock(void *,bool acquire){
    if(!acquire){
        if(__atomic_load_n(&sm8750_gcc_vote_owner,__ATOMIC_ACQUIRE)!=GccOwner)return EldenBlBusy;
        __atomic_store_n(&sm8750_gcc_vote_owner,0u,__ATOMIC_RELEASE);return 0;
    }
    for(unsigned n=0;n<100;n++){
        uint32_t empty=0;
        if(__atomic_compare_exchange_n(&sm8750_gcc_vote_owner,&empty,GccOwner,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
        IODelay(1);
    }
    return EldenBlBusy;
}
static int applyLevel(Context *c,unsigned level){
    if(level!=c->chip.level)__atomic_store_n(&restoreConfirmed,0u,__ATOMIC_RELEASE);
    int rc=elden_aw99706_set_level(&c->chip,level);
    if(!rc&&c->originalValid&&level==c->original){
        rc=elden_aw99706_check_health(&c->chip);
        if(!rc)__atomic_store_n(&restoreConfirmed,1u,__ATOMIC_RELEASE);
    }
    return rc;
}
static int restoreOriginal(Context *c){
    if(!c->originalValid||(__atomic_load_n(&restoreConfirmed,__ATOMIC_ACQUIRE)&&c->chip.level==c->original))return 0;
    int rc=applyLevel(c,c->original);
    report("\nELDEN_BACKLIGHT restore=%d original=%u actual=%u confirmed=%u\n",rc,c->original,c->chip.level,__atomic_load_n(&restoreConfirmed,__ATOMIC_ACQUIRE));
    return rc;
}
static void dispose(Context *c){
    int rc=elden_bl_platform_release(&c->platform);
    report("\nELDEN_BACKLIGHT release=%d quarantine=%u transfers=%u chip-writes=%u\n",
        rc,c->platform.quarantined,c->platform.transfers,c->chip.writes);
    if(!rc){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}
    /* Uncertain hardware holds its maps and clocks until the full reset. */
}
}
class EldenBacklight : public AppleARMBacklight {
    OSDeclareDefaultStructors(EldenBacklight)
    Context *context=nullptr;
    IOWorkLoop *loop=nullptr;
    IOTimerEventSource *timer=nullptr;
    IOSimpleLock *armLock=nullptr;
    bool added=false;volatile bool running=false;
    volatile uint64_t request=0;
    volatile unsigned requests=0;
    EldenBlFade fade={};
    void arm(unsigned ms){
        IOSimpleLockLock(armLock);
        if(__atomic_load_n(&running,__ATOMIC_ACQUIRE)&&timer)timer->setTimeoutMS(ms);
        IOSimpleLockUnlock(armLock);
    }
    bool publish(){
        auto b=OSDictionary::withCapacity(3),p=OSDictionary::withCapacity(1);
        auto lo=OSNumber::withNumber(0ULL,32),hi=OSNumber::withNumber(4095ULL,32),v=OSNumber::withNumber(context->chip.level,32);
        bool ok=b&&p&&lo&&hi&&v;
        if(ok){b->setObject("min",lo);b->setObject("max",hi);b->setObject("value",v);p->setObject("brightness",b);ok=setProperty("IODisplayParameters",p);}
        OSSafeReleaseNULL(b);OSSafeReleaseNULL(p);OSSafeReleaseNULL(lo);OSSafeReleaseNULL(hi);OSSafeReleaseNULL(v);
        if(ok)setProperty("brightness",context->chip.level,32);
        return ok;
    }
    static IOReturn quiesce(OSObject *owner,void *,void *,void *,void *){
        auto self=static_cast<EldenBacklight *>(owner);
        if(self->context)restoreOriginal(self->context);
        return kIOReturnSuccess;
    }
    void cleanup(){
        if(armLock){IOSimpleLockLock(armLock);__atomic_store_n(&running,false,__ATOMIC_RELEASE);IOSimpleLockUnlock(armLock);}
        if(timer)timer->cancelTimeout();
        /* Synchronize with an in-flight tick before restoring or releasing
         * Context. Late property setters see running=0 and cannot re-arm. */
        if(loop&&added)loop->runAction(quiesce,this);
        else if(context)restoreOriginal(context);
        if(added){loop->removeEventSource(timer);added=false;}
        OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);
        if(context){dispose(context);context=nullptr;}
        /* armLock remains until free(): a late setProperties sees running=0. */
    }
    static void tick(OSObject *owner,IOTimerEventSource *){
        auto self=static_cast<EldenBacklight *>(owner);
        if(!__atomic_load_n(&self->running,__ATOMIC_ACQUIRE))return;
        uint64_t r=__atomic_exchange_n(&self->request,0ull,__ATOMIC_ACQ_REL),now=now_us(nullptr)/1000;
        if(r>>63)elden_bl_fade_begin(&self->fade,r,self->context->chip.level,now);
        unsigned level=elden_bl_fade_level(&self->fade,now);
        bool changed=level!=self->context->chip.level;
        int rc=applyLevel(self->context,level);
        if(rc){
            __atomic_store_n(&self->running,false,__ATOMIC_RELEASE);
            self->setProperty("EldenBacklightError",(uint32_t)rc,32);
            report("\nELDEN_BACKLIGHT brightness failed=%d platform=%d quarantine=%u last-confirmed=%u\n",
                rc,self->context->platform.error,self->context->platform.quarantined,self->context->chip.level);return;
        }
        if(changed)self->publish();
        if(r>>63)report("\nELDEN_BACKLIGHT request=%u goal=%u fade-ms=%u actual=%u\n",
            __atomic_load_n(&self->requests,__ATOMIC_RELAXED),self->fade.to,self->fade.ms,self->context->chip.level);
        if(level!=self->fade.to)self->arm(16);
    }
public:
    void adopt(Context *c){context=c;}
    bool init(OSDictionary *properties=nullptr) override{return IOService::init(properties);}
    IOWorkLoop *getWorkLoop() const override{return loop;}
    bool start(IOService *provider) override{
        if(!context||!context->chip.ready||!IOService::start(provider))return false;
        if(!metaCast("AppleARMBacklight")){report("\nELDEN_BACKLIGHT refused original-class conformance\n");IOService::stop(provider);return false;}
        loop=IOWorkLoop::workLoop();armLock=IOSimpleLockAlloc();
        timer=IOTimerEventSource::timerEventSource(kIOTimerEventSourceOptionsPriorityUser,this,tick);
        added=loop&&armLock&&timer&&loop->addEventSource(timer)==kIOReturnSuccess;
        if(!added||!publish()){cleanup();IOService::stop(provider);return false;}
        fade.from=fade.to=context->chip.level;
        setName("EldenBacklight");
        /* Narrow marker understood by the existing NativeSandbox component;
         * sheng requires FadePeriod alongside brightness for backboardd. */
        if(!setProperty("Built-In",true)||!setProperty("NativeSandboxGrantProcess","backboardd")||
           !setProperty("backlight-control",true)){cleanup();IOService::stop(provider);return false;}
        __atomic_store_n(&running,true,__ATOMIC_RELEASE);
        registerService();report("\nELDEN_BACKLIGHT published AppleARMBacklight level=%u range=0..4095\n",context->chip.level);
        return true;
    }
    IOReturn setProperties(OSObject *properties) override{
        auto d=OSDynamicCast(OSDictionary,properties);if(!d)return kIOReturnBadArgument;
        auto level=OSDynamicCast(OSNumber,d->getObject("brightness"));if(!level)return kIOReturnSuccess;
        if(stopping()||!__atomic_load_n(&running,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
        auto period=OSDynamicCast(OSNumber,d->getObject("FadePeriod"));
        __atomic_add_fetch(&requests,1u,__ATOMIC_RELAXED);
        /* iPadOS17 brightness is signed32. A negative/off request must not
         * become maximum brightness through unsigned extension. */
        int32_t requested=(int32_t)level->unsigned32BitValue();
        __atomic_store_n(&request,elden_bl_request(requested,period?period->unsigned32BitValue():0),__ATOMIC_RELEASE);
        arm(1);return kIOReturnSuccess;
    }
    void stop(IOService *provider) override{cleanup();IOService::stop(provider);}
    void free() override{cleanup();if(armLock){IOSimpleLockFree(armLock);armLock=nullptr;}IOService::free();}
    void unqualifiedBacklightMethod() override{panic("EldenBacklight: unexpected original private slot 168");}
};
OSDefineMetaClassAndStructors(EldenBacklight,AppleARMBacklight)
namespace {
static EldenBacklight *service;
static int hold(Context *c,const char *phase,unsigned ms){
    uint64_t begin=now_us(nullptr),next=begin;
    report("\nELDEN_BACKLIGHT load-phase=%s time-us=%llu level=%u hold-ms=%u\n",phase,begin,c->chip.level,ms);
    while(now_us(nullptr)-begin<(uint64_t)ms*1000){
        if(!sleepChecked(100))return -7;
        uint64_t now=now_us(nullptr);
        if(now>=next){int rc=elden_aw99706_check_health(&c->chip);if(rc)return rc;next=now+1000000;}
    }
    return 0;
}
static void powerTrial(Context *c){
    unsigned left=elden_recovery_deadline_remaining?elden_recovery_deadline_remaining():0;
    if(left<300||left>900){report("\nELDEN_BACKLIGHT refused power deadline=%u\n",left);return;}
    int rc=0;
    report("\nELDEN_BACKLIGHT power-settle left=%u level=%u\n",left,c->chip.level);
    /* Begin measurement after the startup load; keep 60s of final headroom.
     * Same schedule in the PD and non-PD controls; PD success is assessed
     * separately from its original logs, never inferred from this timer. */
    while(!rc&&left>240){
        rc=hold(c,"settle",1000);
        left=elden_recovery_deadline_remaining();
        if(!left)rc=-7;
    }
    if(!rc)rc=hold(c,"baseline",60000);
    if(!rc&&!stopping())rc=applyLevel(c,c->original/2);
    else if(!rc)rc=-7;
    if(!rc)rc=hold(c,"dim",60000);
    int restored=restoreOriginal(c);
    if(!rc)rc=restored;
    if(!rc)rc=hold(c,"restored",60000);
    report("\nELDEN_BACKLIGHT power-finished=%d restore=%d confirmed=%u actual=%u writes=%u stop=%u\n",
        rc,restored,__atomic_load_n(&restoreConfirmed,__ATOMIC_ACQUIRE),c->chip.level,c->chip.writes,stopping());
}
static void run(unsigned mode){
    if(stopping())return;
    uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&lease,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    Context *c=new Context;
    if(!c){__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);return;}
    if(!c->map()){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);report("\nELDEN_BACKLIGHT refused mapping\n");return;}
    c->platform.io={c,read,write,now_us,delay_us,gcc_lock};
    int rc=elden_bl_platform_acquire(&c->platform);
    report("\nELDEN_BACKLIGHT acquire=%d stage=%u vote=%x rcg=%x/%x pins=%x/%x hwen=%x/%x irq=%x config=%x\n",
        rc,c->platform.stage,c->platform.vote,c->platform.rcg,c->platform.rcg_cfg,c->platform.pins[0],
        c->platform.pins[1],c->platform.pins[2],c->platform.hwen,c->platform.last_status,c->platform.config);
    report("\nELDEN_BACKLIGHT entry status=%x gsi=%x dma=%x tx=%x rx=%x secondary=%x interface=%x revision=%x clock=%x scl=%x\n",
        c->platform.entry_status,c->platform.entry_gsi,c->platform.entry_dma,c->platform.entry_tx,c->platform.entry_rx,
        c->platform.entry_secondary,c->platform.entry_interface,c->platform.entry_revision,c->platform.entry_clock,c->platform.entry_scl);
    EldenBacklightIo io={&c->platform,elden_bl_platform_read,elden_bl_platform_write};
    unsigned delay=0;
    if(mode==1||mode>=4){
        if(mode>=4)delay=5000;
        else PE_parse_boot_argn("elden-backlight-delay-ms",&delay,sizeof(delay));
        if(delay>10000){report("\nELDEN_BACKLIGHT refused initial read delay=%u\n",delay);dispose(c);return;}
        if(!sleepChecked(delay)){dispose(c);return;}
        report("\nELDEN_BACKLIGHT first-read delay-ms=%u\n",delay);
    }
    if(!rc)rc=elden_aw99706_inspect(&c->chip,&io);
    report("\nELDEN_BACKLIGHT inspect=%d mode=%u ready=%u enabled=%u level=%u config=%x chip-writes=%u\n",
        rc,mode,c->chip.ready,c->chip.enabled,c->chip.level,c->chip.led_config,c->chip.writes);
    report("\nELDEN_BACKLIGHT registers id=%02x mode=%02x high=%02x low=%02x config=%02x open=%02x short=%02x\n",
        c->chip.observed_id,c->chip.observed_mode,c->chip.observed_hi,c->chip.observed_lo,c->chip.observed_config,
        c->chip.observed_open,c->chip.observed_short);
    report("\nELDEN_BACKLIGHT protection-flags=%02x\n",c->chip.observed_flags);
    report("\nELDEN_BACKLIGHT cfg7=%02x brightness-mode=%u\n",c->chip.observed_cfg7,c->chip.observed_cfg7&3);
    EldenBlSettledTrial trial={};
    if(mode>=4)elden_bl_trial_begin(&trial,&c->chip);
    if((!rc||rc==EldenBlFault)&&(mode==1||mode>=4)){
        /* Observe only. Neither a clear later sample nor the initial-read delay
         * requalifies a failed chip. Preserve all protection and enable bits. */
        const unsigned times[]={100,1000,2000,4000,8000};unsigned previous=0;
        for(unsigned n=0;n<sizeof(times)/sizeof(times[0]);++n){
            if(!sleepChecked(times[n]-previous)){dispose(c);return;}previous=times[n];
            uint8_t open=0,shorted=0,flags=0,config=0,hi=0,lo=0;
            int diagnostic=elden_bl_platform_read(&c->platform,0x12,&open);
            if(!diagnostic)diagnostic=elden_bl_platform_read(&c->platform,0x13,&shorted);
            if(!diagnostic)diagnostic=elden_bl_platform_read(&c->platform,0x10,&flags);
            if(!diagnostic)diagnostic=elden_bl_platform_read(&c->platform,0x0d,&config);
            if(!diagnostic)diagnostic=elden_bl_platform_read(&c->platform,4,&hi);
            if(!diagnostic)diagnostic=elden_bl_platform_read(&c->platform,5,&lo);
            report("\nELDEN_BACKLIGHT observation=%u ms=%u result=%d open=%02x short=%02x flags=%02x config=%02x high=%02x low=%02x writes=%u ready=%u\n",
                n+1,times[n],diagnostic,open,shorted,flags,config,hi,lo,c->chip.writes,c->chip.ready);
            if(mode>=4)elden_bl_trial_sample(&trial,times[n],diagnostic,open,shorted,flags,config,hi,lo);
            if(diagnostic)break;
        }
    }
    if(mode>=4&&!stopping()){
        /* Separate opt-in experiment; never revive the failed production
         * instance. A fresh complete inspection must still pass immediately. */
        EldenAw99706 fresh={};
        int qualified=elden_bl_trial_admitted(&trial)?elden_aw99706_inspect(&fresh,&io):EldenBlFault;
        if(!qualified&&(fresh.observed_mode!=0x65||fresh.observed_config!=0xf8||fresh.level!=0x63d))qualified=EldenBlConfiguration;
        report("\nELDEN_BACKLIGHT trial-qualified=%d samples=%u level=%u writes=%u\n",qualified,trial.samples,fresh.level,c->chip.writes);
        rc=qualified;if(!rc)c->chip=fresh;
    }
    if(stopping()){dispose(c);return;}
    if(!rc){c->original=c->chip.level;c->originalValid=true;}
    if(!rc&&(mode==2||mode==4)){
        unsigned before=c->chip.level,dim=before>1?before/2:before;
        rc=applyLevel(c,dim);
        if(!rc){sleepChecked(500);rc=restoreOriginal(c);}
        if(!rc)rc=elden_aw99706_check_health(&c->chip);
        report("\nELDEN_BACKLIGHT roundtrip=%d before=%u dim=%u restored=%u chip-writes=%u\n",rc,before,dim,c->chip.level,c->chip.writes);
    }
    if(!rc&&mode==5)powerTrial(c);
    if(!rc&&(mode==3||mode==6)){
        auto existing=IOService::copyMatchingService(IOService::serviceMatching("AppleARMBacklight"));
        if(existing){existing->release();report("\nELDEN_BACKLIGHT refused existing service\n");dispose(c);return;}
        void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
        if(!handle){report("\nELDEN_BACKLIGHT refused class-registration handle\n");dispose(c);return;}
        report("\nELDEN_BACKLIGHT registering class; qualified mode=%u\n",mode);
        elden_backlight_static_init();
        OSReturn registration=OSMetaClass::postModLoad(handle);
        if(registration!=kOSReturnSuccess){report("\nELDEN_BACKLIGHT refused class-registration status=%x\n",registration);dispose(c);return;}
        service=new EldenBacklight;
        if(!service){dispose(c);return;}
        service->adopt(c);IOService *provider=IOService::getPlatform();
        if(service->init()&&provider&&service->attach(provider)&&service->start(provider)){
            while(sleepChecked(100)){}
            service->stop(provider);service->detach(provider);service->release();service=nullptr;return;
        }
        report("\nELDEN_BACKLIGHT service start refused\n");
        if(provider&&service->getProvider()==provider)service->detach(provider);
        service->release();service=nullptr;return;
    }
    dispose(c);
}
static void worker(void *parameter,wait_result_t){
    if(!stopping())run((unsigned)(uintptr_t)parameter);
    elden_bl_worker_finish(&lifecycle);thread_terminate(current_thread());
}
}
extern "C" void elden_backlight_runtime_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-backlight",&mode,sizeof(mode));
    if(mode<1||mode>6||!elden_bl_worker_claim(&lifecycle))return;
    thread_t t=nullptr;
    if(kernel_thread_start(worker,(void *)(uintptr_t)mode,&t)==KERN_SUCCESS)thread_deallocate(t);
    else {elden_bl_worker_finish(&lifecycle);report("\nELDEN_BACKLIGHT refused worker allocation\n");}
}
extern "C" int elden_backlight_request_stop(void){
    elden_bl_worker_stop(&lifecycle);
    for(unsigned i=0;i<300&&!elden_bl_worker_done(&lifecycle);++i)IOSleep(10);
    unsigned done=elden_bl_worker_done(&lifecycle),held=__atomic_load_n(&lease,__ATOMIC_ACQUIRE),restored=__atomic_load_n(&restoreConfirmed,__ATOMIC_ACQUIRE);
    report("\nELDEN_BACKLIGHT stop=%u worker-done=%u held=%u restored=%u\n",done&&!held&&restored,done,held,restored);
    return done&&!held&&restored;
}
