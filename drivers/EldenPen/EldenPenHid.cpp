/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "OriginalHidDeviceABI.h"
#include "EldenPenOriginalAbi.h"
#include "EldenPenNative.h"
#include "NativeRoute.h"
#include "HidReport.h"
#include "Tilt.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <mach/kmod.h>
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void elden_pen_static_init(void) __asm__("__GLOBAL__sub_I_EldenPenHid.cpp");
}
#include "PressureHook.inc"
static IORegistryEntry *copyDriver(IORegistryEntry *parent,unsigned depth){
    if(!parent||!depth)return nullptr;
    OSIterator *it=parent->getChildIterator(gIOServicePlane);if(!it)return nullptr;
    IORegistryEntry *found=nullptr;unsigned count=0;
    while(OSObject *o=it->getNextObject()){
        if(++count>32)break;
        IORegistryEntry *e=OSDynamicCast(IORegistryEntry,o);if(!e)continue;
        if(e->metaCast("IOHIDEventDriver")){e->retain();found=e;}
        else found=copyDriver(e,depth-1);
        if(found)break;
    }
    it->release();return found;
}
static bool numberIs(IORegistryEntry *e,const char *name,uint32_t expected){
    OSObject *value=e->copyProperty(name);OSNumber *number=OSDynamicCast(OSNumber,value);
    bool ok=number&&number->unsigned32BitValue()==expected;if(value)value->release();return ok;
}
static bool flagIsTrue(IORegistryEntry *e,const char *name){
    OSObject *value=e->copyProperty(name);bool ok=value==kOSBooleanTrue;if(value)value->release();return ok;
}
static bool stamp(IORegistryEntry *e){
    bool ok=e&&e->setProperty("Built-In",kOSBooleanTrue)&&e->setProperty("DisplayIntegrated",kOSBooleanTrue)&&
        e->setProperty("PrimaryUsagePage",0x0dULL,32)&&e->setProperty("PrimaryUsage",0x20ULL,32)&&
        e->setProperty("AccurateMaxDigitizerPressureValue",(unsigned long long)EldenPenPressureScale,32);
    return ok&&flagIsTrue(e,"Built-In")&&flagIsTrue(e,"DisplayIntegrated")&&numberIs(e,"PrimaryUsagePage",0x0d)&&
        numberIs(e,"PrimaryUsage",0x20)&&numberIs(e,"AccurateMaxDigitizerPressureValue",EldenPenPressureScale);
}
static IORegistryEntry *copyParent(IORegistryEntry *entry){
    OSIterator *parents=entry->getParentIterator(gIOServicePlane);if(!parents)return nullptr;
    IORegistryEntry *parent=OSDynamicCast(IORegistryEntry,parents->getNextObject());
    if(parent)parent->retain();parents->release();return parent;
}
class EldenPenHid : public IOHIDDevice {
    OSDeclareDefaultStructors(EldenPenHid)
    IOWorkLoop *loop=nullptr;
    IORegistryEntry *eventDriver=nullptr;
    uint32_t stopping=0;
public:
    uint16_t pressureMax=4095;
    IOWorkLoop *getWorkLoop() const override{return loop;}
    OSString *newTransportString() const override{return OSString::withCString("Virtual");}
    OSString *newProductString() const override{return OSString::withCString("Lenovo AP501U Pen Digitizer");}
    IOReturn newReportDescriptor(IOMemoryDescriptor **out) const override{
        if(!out)return kIOReturnBadArgument;*out=nullptr;
        uint8_t bytes[EldenPenDescriptorCapacity];size_t count=0;
        if(elden_pen_hid_descriptor(pressureMax,bytes,sizeof(bytes),&count))return kIOReturnBadArgument;
        *out=IOBufferMemoryDescriptor::withBytes(bytes,count,kIODirectionNone);
        return *out?kIOReturnSuccess:kIOReturnNoMemory;
    }
    bool start(IOService *provider) override{
        loop=IOWorkLoop::workLoop();if(!loop)return false;
        if(!stamp(this)||!setProperty("DigitizerCollectionDispatch",kOSBooleanTrue))return false;
        return IOHIDDevice::start(provider);
    }
    bool prepare(){
        if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return false;
        if(eventDriver)return PenPressure::install(eventDriver);
        IORegistryEntry *driver=copyDriver(this,4);if(!driver)return false;
        IORegistryEntry *parent=copyParent(driver);
        bool ok=stamp(driver)&&stamp(parent)&&PenPressure::install(driver);
        if(parent)parent->release();
        if(ok){eventDriver=driver;IOLog("ELDEN_PEN HID ready usage=13,32 max-force=1000; original pressure hook owned\n");}
        else driver->release();return ok;
    }
    static IOReturn action(OSObject *owner,void *data,void *,void *,void *){
        EldenPenHid *self=OSDynamicCast(EldenPenHid,owner);
        if(!self||!data)return kIOReturnBadArgument;
        if(!self->prepare())return kIOReturnNotReady;
        uint8_t bytes[EldenPenHidBytes];
        if(elden_pen_hid_report(static_cast<EldenPenFrame *>(data),self->pressureMax,bytes))return kIOReturnBadArgument;
        IOBufferMemoryDescriptor *memory=IOBufferMemoryDescriptor::withBytes(bytes,sizeof(bytes),kIODirectionNone);
        if(!memory)return kIOReturnNoMemory;
        IOReturn result=self->handleReport(memory,0,0);memory->release();return result;
    }
    int submit(const EldenPenFrame *f){
        return loop?(int)loop->runAction(action,this,const_cast<EldenPenFrame *>(f)):int(kIOReturnNotReady);
    }
    void stop(IOService *provider) override{__atomic_store_n(&stopping,1u,__ATOMIC_RELEASE);IOHIDDevice::stop(provider);}
    void free() override{OSSafeReleaseNULL(eventDriver);OSSafeReleaseNULL(loop);IOHIDDevice::free();}
};
OSDefineMetaClassAndStructors(EldenPenHid,IOHIDDevice)
static EldenPenHid *active;
static EldenPenNativeRoute route;
static EldenPenLimits limits={19040,30400,4095,60};
static unsigned attempted,enabled,stopped;
static uint64_t nowUs(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int sendPen(void *,const EldenPenFrame *f){return active?active->submit(f):int(kIOReturnNotReady);}
static int released(){EldenPenOwnerStatus st;return elden_pen_route_status(&route,&st)?0:st.delivery_released;}
extern "C" int elden_pen_native_start(uintptr_t frame_owner){
    unsigned mode=0;PE_parse_boot_argn("elden-pen",&mode,sizeof(mode));
    if(mode!=1)return 0;
    if(!frame_owner)return int(kIOReturnBadArgument);
    if(!enabled){
        if(attempted)return int(kIOReturnBusy);attempted=1;
        unsigned maximum=4095;PE_parse_boot_argn("elden-pen-pressure",&maximum,sizeof(maximum));
        if(maximum!=4095&&maximum!=8191)return int(kIOReturnBadArgument);
        limits.max_pressure=(uint16_t)maximum;
        if(OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily")||
           OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver"))return int(kIOReturnNotReady);
        void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!handle)return int(kIOReturnNotReady);
        elden_pen_static_init();if(OSMetaClass::postModLoad(handle))return int(kIOReturnNotReady);
        IOService *resource=IOService::getResourceService();if(!resource)return int(kIOReturnNotReady);
        EldenPenHid *device=new EldenPenHid;if(!device)return int(kIOReturnNoMemory);
        device->pressureMax=(uint16_t)maximum;
        if(!device->init()||!device->attach(resource)){device->release();return int(kIOReturnError);}
        if(!device->start(resource)){device->detach(resource);device->release();return int(kIOReturnError);}
        active=device;enabled=1;
        /* Lifetime retained to reset. Publication/pressure readiness is checked
         * before each first successful send; handleReport success is not UI proof. */
        IOLog("ELDEN_PEN native service started; pressure-max=%u; no SPI writes\n",maximum);
    }
    /* frame_owner is the worker's live serialized Context — the real
     * touch-frame owner this session's packets are sampled under. It is
     * declared at bind and rechecked against the actual packet caller by the
     * guarded owner path; a reused or foreign context is refused there. The
     * named provider is the NT36536 sampler itself: the worker Context is
     * both its provider owner and this session's provider epoch. */
    int rc=elden_pen_route_begin(&route,&limits,sendPen,nullptr,"EldenNvt36536",
                               (uint64_t)frame_owner,(uint64_t)frame_owner);
    if(rc)return rc;
    stopped=0;
    IOLog("ELDEN_PEN worker route armed generation=%llu provider=EldenNvt36536\n",
          (unsigned long long)route.gen);return 0;
}
extern "C" int elden_pen_native_packet(const uint8_t *packet,size_t bytes,uintptr_t frame_owner){
    if(!enabled||stopped)return 0;
    return elden_pen_route_packet(&route,packet,bytes,(uint64_t)frame_owner,nowUs());
}
extern "C" int elden_pen_native_fault(void){
    return enabled&&route.armed?elden_pen_route_fault(&route,nowUs()):0;
}
extern "C" int elden_pen_native_pause(void){
    if(!enabled)return 0;if(stopped)return EldenPenStopped;
    if(!route.armed)return 0;
    int rc=elden_pen_route_pause(&route,nowUs());
    for(unsigned n=0;n<30&&!released();++n){IOSleep(8);rc=elden_pen_route_tick(&route,nowUs());}
    if(rc)return rc;
    return released()?0:int(kIOReturnNotReady);
}
extern "C" int elden_pen_native_resume(void){
    return enabled&&route.armed?elden_pen_route_resume(&route,nowUs()):0;
}
extern "C" int elden_pen_native_stop(void){
    if(!enabled)return 0;stopped=1;
    if(!route.armed)return 0;
    int rc=elden_pen_route_stop(&route,nowUs());
    for(unsigned n=0;n<30&&!released();++n){IOSleep(8);int rr=elden_pen_route_release(&route);if(rr)rc=rr;}
    /* Released state and counters are read before retire — retire re-arms
     * the delivery for a later session, zeroing failures and always
     * reporting a fresh released state. */
    EldenPenOwnerStatus st={};elden_pen_route_status(&route,&st);
    int rel_flag=st.delivery_released?1:0;
    int trc=elden_pen_route_retire(&route);
    IOLog("ELDEN_PEN stop released=%u result=%x retire=%x sent=%u failures=%u; service retained\n",
          unsigned(rel_flag),unsigned(rc),unsigned(trc),st.transport_delivered,st.transport_failures);
    IOLog("ELDEN_PEN pose converted=%u missing=%u rejected=%u; degrees contract, live mounting unverified\n",
          __atomic_load_n(&PenPressure::poseConverted,__ATOMIC_RELAXED),__atomic_load_n(&PenPressure::poseMissing,__ATOMIC_RELAXED),
          __atomic_load_n(&PenPressure::poseRejected,__ATOMIC_RELAXED));
    return rel_flag?(trc?trc:0):(rc?rc:int(kIOReturnNotReady));
}
