// SPDX-License-Identifier: BSD-2-Clause-Patent
// Elden lifecycle for catalog ShengHidInput: qualified keyboard/consumer source
// and an independently enabled board digitizer. Physical SPI has its own owner.
#include "OriginalHidDeviceABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSBoolean.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
extern volatile int native_owner_data_status,elden_init_exec_accepted;
int elden_recovery_armed(void);
int elden_usb_preview_line(const char *,unsigned);
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void native_hid_static_init(void) __asm__("__GLOBAL__sub_I_NativeHidInput.cpp");
IOService *native_hid_input_allocate(void);
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
#include "PointReport.h"
IOService *native_hid_touch_allocate(void);
#endif
void elden_preview_after_input(void) __attribute__((weak_import));
// Idle contact producer until the separate NT36536 driver is qualified.
volatile uint32_t native_touch_seq,native_touch_count,native_touch_hid_reports;
volatile int native_touch_xs[12],native_touch_ys[12],native_touch_ids[12];
}
static IOService *active;
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
static IOService *activeTouch,*startingTouch;
static unsigned touchReady;
static OSObject *touchObject;
static unsigned touchSinkGeneration;
static unsigned digitizerPublished;
static unsigned hidStopRequested,guardRunning,tapPending,tapAttempted,swipeAttempted;
#endif
static IOLock *sinkLock;
static void (*touchSink)(void *);
static void *touchContext;
static void (*buttonSink)(void *,uint32_t);
static void *buttonContext;
static unsigned attempted,ready,pending,events[8],lastDriver;
static void say(const char *text){elden_usb_preview_line(text,(unsigned)strlen(text));}
// Registration for later qualified board producers. The current virtual-only
// mode supplies no physical reports; setters and producers use worker context.
extern "C" void native_touch_set_sink(void (*fn)(void *),void *context){
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    OSObject *owned=fn?static_cast<OSObject *>(context):nullptr;if(owned)owned->retain();
    if(!sinkLock){if(owned)owned->release();return;}
    IOLockLock(sinkLock);OSObject *old=touchObject;touchObject=owned;touchSink=fn;touchContext=context;IOLockUnlock(sinkLock);
    unsigned generation=__atomic_add_fetch(&touchSinkGeneration,1u,__ATOMIC_RELAXED);
    char line[140];snprintf(line,sizeof(line),"\nELDEN_TOUCH_HID sink generation=%u registered=%u object=%u\n",generation,fn!=nullptr,owned!=nullptr);say(line);
    if(old)old->release();
#else
    if(!sinkLock)return;IOLockLock(sinkLock);touchSink=fn;touchContext=context;IOLockUnlock(sinkLock);
#endif
}
extern "C" void native_buttons_set_sink(void (*fn)(void *,uint32_t),void *context){
    if(!sinkLock)return;IOLockLock(sinkLock);buttonSink=fn;buttonContext=context;IOLockUnlock(sinkLock);
}
static bool eventDriver(IORegistryEntry *parent,unsigned depth){
    if(!depth)return false;
    OSIterator *it=parent->getChildIterator(gIOServicePlane);if(!it)return false;
    bool found=false;
    while(OSObject *o=it->getNextObject()){
        IORegistryEntry *entry=OSDynamicCast(IORegistryEntry,o);
        if(entry && (entry->metaCast("IOHIDEventDriver") || eventDriver(entry,depth-1))){found=true;break;}
    }
    it->release();return found;
}
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
// Adapted from sheng HidInputProbe.cpp@a68ee4ee: backboardd classifies the
// event service/provider, not only the HID device. Return retained nodes.
static IORegistryEntry *copyEventDriver(IORegistryEntry *parent,unsigned depth){
    if(!parent||!depth)return nullptr;
    OSIterator *it=parent->getChildIterator(gIOServicePlane);if(!it)return nullptr;
    IORegistryEntry *found=nullptr;
    while(OSObject *o=it->getNextObject()){
        IORegistryEntry *entry=OSDynamicCast(IORegistryEntry,o);if(!entry)continue;
        if(entry->metaCast("IOHIDEventDriver")){entry->retain();found=entry;}
        else found=copyEventDriver(entry,depth-1);
        if(found)break;
    }
    it->release();return found;
}
static unsigned numberProperty(IORegistryEntry *entry,const char *key){
    OSObject *value=entry->copyProperty(key);OSNumber *n=OSDynamicCast(OSNumber,value);
    unsigned result=n?n->unsigned32BitValue():0xffffffffu;if(value)value->release();return result;
}
static bool trueProperty(IORegistryEntry *entry,const char *key){
    OSObject *value=entry->copyProperty(key);bool result=value==kOSBooleanTrue;if(value)value->release();return result;
}
static bool digitizerProperties(IORegistryEntry *entry){
    return numberProperty(entry,"PrimaryUsagePage")==0x0d&&numberProperty(entry,"PrimaryUsage")==4&&
        trueProperty(entry,"Built-In")&&trueProperty(entry,"DisplayIntegrated");
}
static bool ensureDigitizerProperties(IORegistryEntry *entry){
    if(digitizerProperties(entry))return true;
    bool ok=entry->setProperty("Built-In",kOSBooleanTrue)&&entry->setProperty("DisplayIntegrated",kOSBooleanTrue)&&
        entry->setProperty("PrimaryUsagePage",(unsigned long long)0x0d,32)&&entry->setProperty("PrimaryUsage",(unsigned long long)4,32);
    return ok&&digitizerProperties(entry);
}
static bool publishDigitizer(IORegistryEntry *driver){
    unsigned oldPage=numberProperty(driver,"PrimaryUsagePage"),oldUsage=numberProperty(driver,"PrimaryUsage");
    bool changed=!digitizerProperties(driver);bool ok=ensureDigitizerProperties(driver);
    OSIterator *parents=driver->getParentIterator(gIOServicePlane);IORegistryEntry *parent=nullptr;
    if(parents){parent=OSDynamicCast(IORegistryEntry,parents->getNextObject());if(parent)parent->retain();parents->release();}
    if(parent){changed|=!digitizerProperties(parent);ok=ensureDigitizerProperties(parent)&&ok;parent->release();}
    else ok=false;
    __atomic_store_n(&digitizerPublished,(unsigned)ok,__ATOMIC_RELEASE);
    if(changed){char line[180];snprintf(line,sizeof(line),"\nELDEN_TOUCH_HID service-publish=%u before=%u,%u usage=13,4 built-in=1 integrated=1\n",ok,oldPage,oldUsage);say(line);}
    return ok;
}
static void digitizerGuard(void *context,wait_result_t){
    IOService *device=static_cast<IOService *>(context);unsigned stopAt=180000;
    for(unsigned ms=0;ms<stopAt&&!__atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE);ms+=2){
        IORegistryEntry *driver=copyEventDriver(device,4);
        if(driver){
            publishDigitizer(driver);IOService *service=OSDynamicCast(IOService,driver);
            if(stopAt==180000&&service&&(service->getState()&kIOServiceRegisteredState)&&ms+10000<stopAt)stopAt=ms+10000;
            driver->release();
        }
        IOSleep(2);
    }
    device->release();__atomic_sub_fetch(&guardRunning,1u,__ATOMIC_RELEASE);thread_terminate(current_thread());
}
static void startDigitizerGuard(IOService *device){
    if(__atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE))return;
    __atomic_add_fetch(&guardRunning,1u,__ATOMIC_ACQ_REL);
    device->retain();thread_t t=nullptr;
    if(kernel_thread_start(digitizerGuard,device,&t)==KERN_SUCCESS)thread_deallocate(t);
    else{device->release();__atomic_sub_fetch(&guardRunning,1u,__ATOMIC_RELEASE);say("\nELDEN_TOUCH_HID property guard unavailable\n");}
}
static void describeTouchBranch(IORegistryEntry *entry,unsigned depth,unsigned &remaining){
    if(!entry||depth>4||!remaining)return;--remaining;
    IOService *service=OSDynamicCast(IOService,entry);char line[220];
    snprintf(line,sizeof(line),"\nELDEN_TOUCH_TREE depth=%u name=%.64s class=%.64s state=%lx\n",depth,
        entry->getName(),entry->getMetaClass()->getClassName(),service?(unsigned long)service->getState():0ul);say(line);
    OSIterator *it=entry->getChildIterator(gIOServicePlane);if(!it)return;
    while(remaining){OSObject *o=it->getNextObject();if(!o)break;IORegistryEntry *child=OSDynamicCast(IORegistryEntry,o);if(child)describeTouchBranch(child,depth+1,remaining);}
    it->release();
}
#endif
extern "C" void native_hid_live_checkpoint(IOService *device){
    bool found=device && eventDriver(device,4);
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    if(device==__atomic_load_n(&activeTouch,__ATOMIC_ACQUIRE)||
       device==__atomic_load_n(&startingTouch,__ATOMIC_ACQUIRE)){
        if(!__atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE)){
            IORegistryEntry *driver=copyEventDriver(device,4);
            if(driver){publishDigitizer(driver);driver->release();}
            else __atomic_store_n(&digitizerPublished,0u,__ATOMIC_RELEASE);
        }
        if(found!=__atomic_exchange_n(&touchReady,(unsigned)found,__ATOMIC_ACQ_REL)){
            char line[100];snprintf(line,sizeof(line),"\nELDEN_TOUCH_HID event-driver=%u separate-digitizer=%u\n",found,device!=active);say(line);
            unsigned remaining=16;describeTouchBranch(device,0,remaining);
        }
        if(device!=active)return;
    }
#endif
    if(found!=lastDriver){lastDriver=found;char line[100];snprintf(line,sizeof(line),"\nELDEN_HID event-driver=%u virtual-source=1\n",found);say(line);}
    __atomic_store_n(&ready,found,__ATOMIC_RELEASE);
}
extern "C" void native_hid_input_observed(unsigned event,int result){
    if(event>=8)return;
    unsigned n=__atomic_add_fetch(&events[event],1,__ATOMIC_RELAXED);
    if(event!=3 || n==1){char line[128];snprintf(line,sizeof(line),"\nELDEN_HID event=%u result=%x count=%u\n",event,(unsigned)result,n);say(line);}
    if(event==5){
        __atomic_store_n(&pending,0,__ATOMIC_RELEASE);
        if(result==0 && elden_preview_after_input)elden_preview_after_input();
    }
}
extern "C" bool native_hid_input_requested(void){
    return __sync_bool_compare_and_swap(&pending,1,2);
}
extern "C" int elden_hid_home_ready(void){
    return active && __atomic_load_n(&ready,__ATOMIC_ACQUIRE) &&
        native_owner_data_status==0 && elden_init_exec_accepted==1 &&
        __atomic_load_n(&pending,__ATOMIC_ACQUIRE)==0;
}
extern "C" int elden_hid_request_home(void){
    if(!elden_hid_home_ready())return -1;
    return __sync_bool_compare_and_swap(&pending,0,1)?0:-2;
}
static void worker(void *,wait_result_t){
    for(unsigned n=0;n<12000 && (!elden_recovery_armed() || native_owner_data_status!=0);++n)IOSleep(10);
    if(!elden_recovery_armed() || native_owner_data_status!=0){say("\nELDEN_HID refused: baseline not ready\n");thread_terminate(current_thread());return;}
    int loaded=OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily");
    int matching=loaded?loaded:OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver");
    const OSSymbol *name=OSSymbol::withCString("IOHIDDevice");
    const OSMetaClass *meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;OSSafeReleaseNULL(name);
    char line[160];snprintf(line,sizeof(line),"\nELDEN_HID family=%d matching=%d original-size=%u\n",loaded,matching,meta?meta->getClassSize():0);say(line);
    if(loaded || matching || !meta || meta->getClassSize()!=208 || meta!=&IOHIDDevice::gMetaClass){thread_terminate(current_thread());return;}
    OSDictionary *match=IOService::serviceMatching("IOHIDResource");
    IOService *resource=match?IOService::waitForMatchingService(match,5000000000ULL):nullptr;OSSafeReleaseNULL(match);
    if(!resource){say("\nELDEN_HID resource absent\n");thread_terminate(current_thread());return;}
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){resource->release();thread_terminate(current_thread());return;}
    native_hid_static_init();int status=OSMetaClass::postModLoad(handle);
    snprintf(line,sizeof(line),"\nELDEN_HID metaclass=%d\n",status);say(line);
    if(!status)sinkLock=IOLockAlloc();
    IOService *device=(status || !sinkLock)?nullptr:native_hid_input_allocate();bool attached=false;
    if(device && device->metaCast("IOHIDDevice") && device->init() &&
       (attached=device->attach(resource)) && device->start(resource)){
        active=device;say("\nELDEN_HID started virtual keyboard/consumer source; physical-touch=unqualified\n");
    }else{if(attached)device->detach(resource);OSSafeReleaseNULL(device);say("\nELDEN_HID start failed\n");}
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    unsigned enableTouch=0,combined=0;PE_parse_boot_argn("elden-touch-hid",&enableTouch,sizeof(enableTouch));
    PE_parse_boot_argn("sheng-touch-hid",&combined,sizeof(combined));
    if(active&&enableTouch==1&&combined==1){
        __atomic_store_n(&activeTouch,active,__ATOMIC_RELEASE);
        startDigitizerGuard(active);
        say("\nELDEN_TOUCH_HID combined keyboard/digitizer selected\n");
    }else if(active&&enableTouch==1){
        IOService *touch=native_hid_touch_allocate();bool attachedTouch=false;
        __atomic_store_n(&startingTouch,touch,__ATOMIC_RELEASE);
        if(touch&&touch->metaCast("IOHIDDevice")&&touch->init()&&
           (attachedTouch=touch->attach(resource))&&touch->start(resource)){
            __atomic_store_n(&activeTouch,touch,__ATOMIC_RELEASE);
            startDigitizerGuard(touch);
            say("\nELDEN_TOUCH_HID started separate NT36536 digitizer, normalized coordinates\n");
        }else{
            __atomic_store_n(&touchReady,0u,__ATOMIC_RELEASE);
            if(attachedTouch)touch->detach(resource);OSSafeReleaseNULL(touch);say("\nELDEN_TOUCH_HID start failed\n");
        }
        __atomic_store_n(&startingTouch,(IOService *)nullptr,__ATOMIC_RELEASE);
    }
#endif
    resource->release();thread_terminate(current_thread());
}
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
extern "C" int elden_touch_hid_ready(void){
    return __atomic_load_n(&activeTouch,__ATOMIC_ACQUIRE)&&__atomic_load_n(&touchReady,__ATOMIC_ACQUIRE);
}
extern "C" int elden_touch_hid_publish(const struct elden_nvt_frame *frame){
    if(!frame||frame->count>10)return -1001;
    if(!sinkLock)return -1002;
    if(!__atomic_load_n(&activeTouch,__ATOMIC_ACQUIRE))return -1003;
    if(!__atomic_load_n(&touchReady,__ATOMIC_ACQUIRE))return -1004;
    unsigned ids=0;
    for(unsigned i=0;i<frame->count;++i){
        const auto &c=frame->contacts[i];
        if(c.id>=10||c.x>32767||c.y>32767||(ids&(1u<<c.id)))return -2;
        ids|=1u<<c.id;
    }
    IOLockLock(sinkLock);
    void (*fn)(void *)=touchSink;void *context=touchContext;OSObject *object=touchObject;
    if(!fn||!object){
        unsigned missing=(!fn?1u:0u)|(!object?2u:0u);IOLockUnlock(sinkLock);
        char line[120];snprintf(line,sizeof(line),"\nELDEN_TOUCH_HID missing-sink=%u generation=%u\n",missing,__atomic_load_n(&touchSinkGeneration,__ATOMIC_ACQUIRE));say(line);
        return -1010-(int)missing;
    }
    object->retain();
    __atomic_add_fetch(&native_touch_seq,1u,__ATOMIC_ACQ_REL);
    // Internal slots are zero-based; the original Novatek producer publishes
    // the controller's one-based Contact Identifier. Keep this comparison
    // optional until the actual event/UI path is measured on Elden.
    unsigned idBase=0;PE_parse_boot_argn("elden-touch-id-base",&idBase,sizeof(idBase));
    if(idBase>1)idBase=0;
    for(unsigned i=0;i<frame->count;++i){native_touch_ids[i]=frame->contacts[i].id+idBase;native_touch_xs[i]=frame->contacts[i].x;native_touch_ys[i]=frame->contacts[i].y;}
    native_touch_count=frame->count;__atomic_add_fetch(&native_touch_seq,1u,__ATOMIC_RELEASE);
    IOLockUnlock(sinkLock);
    fn(context);object->release();return 0;
}
// One explicitly gated UI diagnostic per boot, replaying the owner's recorded
// Settings location. This is software input, never physical-touch evidence.
extern "C" int elden_ui_dispatch_begin(void) __attribute__((weak_import));
static void settingsTapWorker(void *,wait_result_t){
    int observation=elden_ui_dispatch_begin?elden_ui_dispatch_begin():0;
    char armLine[100];snprintf(armLine,sizeof(armLine),"\nELDEN_UI_DISPATCH arm=%d window-seconds=30\n",observation);say(armLine);
    int down=-1,up=-1;
    uint64_t downStart=0,downEnd=0,upStart=0,upEnd=0;
    if(!__atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE)){
        elden_nvt_frame f={};f.count=1;f.contacts[0].id=0;
        f.contacts[0].x=27285;f.contacts[0].y=12934;f.contacts[0].width=1;
        clock_get_uptime(&downStart);down=elden_touch_hid_publish(&f);clock_get_uptime(&downEnd);IOSleep(100);
        elden_nvt_frame empty={};clock_get_uptime(&upStart);up=elden_touch_hid_publish(&empty);clock_get_uptime(&upEnd);
        if(!down&&!up&&elden_preview_after_input)elden_preview_after_input();
    }
    char line[160];snprintf(line,sizeof(line),"\nELDEN_UI_REPLAY settings down=%d up=%d normalized=27285,12934 source=software\n",down,up);say(line);
    if(downStart && downEnd>=downStart && upStart>=downEnd && upEnd>=upStart){
        uint64_t downNs,intervalNs,upNs;
        absolutetime_to_nanoseconds(downEnd-downStart,&downNs);
        absolutetime_to_nanoseconds(upStart-downStart,&intervalNs);
        absolutetime_to_nanoseconds(upEnd-upStart,&upNs);
        snprintf(line,sizeof(line),"\nELDEN_UI_TIMING down-call-us=%llu down-to-up-us=%llu up-call-us=%llu\n",downNs/1000,intervalNs/1000,upNs/1000);say(line);
    }
    __atomic_store_n(&tapPending,0u,__ATOMIC_RELEASE);thread_terminate(current_thread());
}
// One bounded bottom-edge swipe through the same normalized producer as the
// panel. It is diagnostic software input; stop/error always sends a release.
static void swipeWorker(void *,wait_result_t){
    int error=0;unsigned sent=0;
    for(unsigned step=0;step<25;++step){
        if(__atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE)){error=-4;break;}
        elden_nvt_frame f={};f.count=1;f.contacts[0].id=0;
        f.contacts[0].x=16384;f.contacts[0].y=(uint16_t)(32700-step*(32700-19660)/24);
        f.contacts[0].width=1;error=elden_touch_hid_publish(&f);
        if(error)break;++sent;IOSleep(16);
    }
    elden_nvt_frame empty={};int release=elden_touch_hid_publish(&empty);
    char line[180];snprintf(line,sizeof(line),"\nELDEN_UI_REPLAY swipe-up sent=%u error=%d release=%d source=software\n",sent,error,release);say(line);
    if(!error&&!release&&elden_preview_after_input)elden_preview_after_input();
    __atomic_store_n(&tapPending,0u,__ATOMIC_RELEASE);thread_terminate(current_thread());
}
#endif
extern "C" int elden_hid_request_settings_tap(void){
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    unsigned enabled=0;PE_parse_boot_argn("elden-ui-replay",&enabled,sizeof(enabled));
    if(enabled!=1||!elden_touch_hid_ready()||!__atomic_load_n(&digitizerPublished,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE)||native_touch_count||
       native_owner_data_status||!elden_init_exec_accepted)return -1;
    if(!__sync_bool_compare_and_swap(&tapAttempted,0u,1u))return -2;
    if(!__sync_bool_compare_and_swap(&tapPending,0u,1u))return -4;
    thread_t thread=nullptr;
    if(kernel_thread_start(settingsTapWorker,nullptr,&thread)!=KERN_SUCCESS){__atomic_store_n(&tapPending,0u,__ATOMIC_RELEASE);return -3;}
    thread_deallocate(thread);return 0;
#else
    return -1;
#endif
}
extern "C" int elden_hid_request_swipe_up(void){
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    unsigned enabled=0;PE_parse_boot_argn("elden-ui-replay",&enabled,sizeof(enabled));
    if(enabled!=1||!elden_touch_hid_ready()||!__atomic_load_n(&digitizerPublished,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&hidStopRequested,__ATOMIC_ACQUIRE)||native_touch_count||
       native_owner_data_status||!elden_init_exec_accepted)return -1;
    if(!__sync_bool_compare_and_swap(&swipeAttempted,0u,1u))return -2;
    if(!__sync_bool_compare_and_swap(&tapPending,0u,1u))return -4;
    thread_t thread=nullptr;
    if(kernel_thread_start(swipeWorker,nullptr,&thread)!=KERN_SUCCESS){__atomic_store_n(&tapPending,0u,__ATOMIC_RELEASE);return -3;}
    thread_deallocate(thread);return 0;
#else
    return -1;
#endif
}
extern "C" int elden_hid_request_stop(void){
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    __atomic_store_n(&hidStopRequested,1u,__ATOMIC_RELEASE);
    return !__atomic_load_n(&tapPending,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&guardRunning,__ATOMIC_ACQUIRE);
#else
    return 1;
#endif
}
extern "C" int elden_hid_stop_ready(void){
#if defined(ELDEN_TOUCH_HID_BUILD) && ELDEN_TOUCH_HID_BUILD
    return !__atomic_load_n(&tapPending,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&guardRunning,__ATOMIC_ACQUIRE);
#else
    return 1;
#endif
}
extern "C" void elden_hid_input_start(void){
    unsigned enabled=0;PE_parse_boot_argn("elden-hid",&enabled,sizeof(enabled));
    if(enabled!=1 || __atomic_exchange_n(&attempted,1,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else say("\nELDEN_HID worker failed\n");
}
