/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "OriginalHidDeviceABI.h"
#include "EldenPenOriginalAbi.h"
#include "EldenPenNative.h"
#include "Delivery.h"
#include "HidReport.h"
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
static EldenPenDelivery delivery;
static EldenPenLimits limits={19040,30400,4095,60};
static unsigned attempted,enabled,stopped;
static uint64_t nowUs(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int sendPen(void *,const EldenPenFrame *f){return active?active->submit(f):int(kIOReturnNotReady);}
extern "C" int elden_pen_native_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-pen",&mode,sizeof(mode));
    if(mode!=1)return 0;
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
    active=device;elden_pen_delivery_init(&delivery,100000);enabled=1;
    /* Lifetime retained to reset. Publication/pressure readiness is checked
     * before each first successful send; handleReport success is not UI proof. */
    IOLog("ELDEN_PEN native service started; pressure-max=%u; no SPI writes\n",maximum);return 0;
}
extern "C" int elden_pen_native_packet(const uint8_t *packet,size_t bytes){
    if(!enabled||stopped)return 0;
    EldenPenFrame f={};int rc=elden_pen_decode(packet,bytes,&limits,&f);
    if(rc){int release=elden_pen_delivery_fault(&delivery,nowUs(),sendPen,nullptr);return release?release:rc;}
    return elden_pen_delivery_feed(&delivery,&f,nowUs(),sendPen,nullptr);
}
extern "C" int elden_pen_native_fault(void){
    return enabled?elden_pen_delivery_fault(&delivery,nowUs(),sendPen,nullptr):0;
}
extern "C" int elden_pen_native_stop(void){
    if(!enabled)return 0;stopped=1;
    int rc=elden_pen_delivery_stop(&delivery,nowUs(),sendPen,nullptr);
    for(unsigned n=0;n<30&&!elden_pen_delivery_released(&delivery);++n){IOSleep(8);rc=elden_pen_delivery_tick(&delivery,nowUs(),sendPen,nullptr);}
    IOLog("ELDEN_PEN stop released=%u result=%x sent=%u failures=%u; service retained\n",
          unsigned(elden_pen_delivery_released(&delivery)),unsigned(rc),delivery.sent,delivery.failures);
    return elden_pen_delivery_released(&delivery)?0:(rc?rc:int(kIOReturnNotReady));
}
