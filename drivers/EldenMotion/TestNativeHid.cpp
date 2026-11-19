/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Executes NativeHidMethods.inc. IOKit/IOHID/runtime clocks and physical SSC
 * admission/demand are explicit peers; no hardware producer or UI claim. */
#include "NativeHid.h"
#include "Hid.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
static unsigned checks,scenarios,dispatched,created,demand_calls,forwards,catalogue_calls;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using IOReturn=int;using IOOptionBits=unsigned;using UInt32=unsigned;using task_t=void *;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,kIOReturnNotPermitted=3,kIOReturnNoMemory=4,kIODirectionNone=0};
static uint64_t clock_ticks=19200000;static bool interrupt_context,fail_event,fail_start,fail_catalogue;
static unsigned start_calls;static int admission_error,demand_error,set_error;static uint32_t last_demand;
struct OSObject;static std::vector<OSObject *> allocated;
struct OSObject {
    static void *operator new(size_t n){void *p=std::malloc(n);CHECK(p);allocated.push_back((OSObject *)p);return p;}
    static void operator delete(void *p){std::free(p);}
    virtual ~OSObject()=default;unsigned refs=1;void retain(){refs++;}virtual void release(){CHECK(refs);refs--;}
};
static OSObject boolean_true;static OSObject *kOSBooleanTrue=&boolean_true;
struct OSString:OSObject {std::string text;explicit OSString(const char *s):text(s){}static OSString *withCString(const char *s){return new OSString(s);}};
struct OSNumber:OSObject {uint64_t value;explicit OSNumber(uint64_t n):value(n){}uint32_t unsigned32BitValue(){return (uint32_t)value;}uint64_t unsigned64BitValue(){return value;}};
struct OSDictionary:OSObject {std::map<std::string,OSObject *> values;OSObject *getObject(const char *s){return values[s];}};
struct OSArray:OSObject {};
struct OSIterator:OSObject {std::vector<OSObject *> values;unsigned at=0;OSObject *getNextObject(){return at<values.size()?values[at++]:nullptr;}};
class IOService;
struct IORegistryEntry:OSObject {
    std::map<std::string,OSObject *> properties;std::vector<OSObject *> children;
    virtual bool metaCast(const char *){return false;}
    OSObject *getProperty(const char *key){return properties[key];}
    OSObject *copyProperty(const char *key){OSObject *o=getProperty(key);if(o)o->retain();return o;}
    bool setProperty(const char *key,OSObject *o){properties[key]=o;return true;}
    bool setProperty(const char *key,unsigned value,unsigned){properties[key]=new OSNumber(value);return true;}
    OSIterator *getChildIterator(const void *){auto *i=new OSIterator;i->values=children;return i;}
};
class IOService:public IORegistryEntry {public:
    virtual bool start(IOService *){return true;}virtual void stop(IOService *){}virtual void free(){}
    virtual bool init(){return true;}bool attach(IOService *){return true;}
    static IOService *getResourceService();
};
struct IOMemoryDescriptor:OSObject {};
struct IOBufferMemoryDescriptor:IOMemoryDescriptor {
    static IOBufferMemoryDescriptor *withBytes(const void *,size_t,unsigned){return new IOBufferMemoryDescriptor;}
};
struct IOWorkLoop:OSObject {
    std::recursive_mutex mutex;
    static IOWorkLoop *workLoop(){return new IOWorkLoop;}
    IOReturn runAction(IOReturn (*fn)(OSObject *,void *,void *,void *,void *),OSObject *self,void *a=nullptr,void *b=nullptr,void *c=nullptr,void *d=nullptr){
        std::lock_guard<std::recursive_mutex> hold(mutex);return fn(self,a,b,c,d);
    }
};
struct PeerDriver final:IOService {bool metaCast(const char *s) override{return !std::strcmp(s,"IOHIDEventDriver");}};
static uintptr_t original_driver_storage[222],original_driver_table[220];static PeerDriver driver_peers[2];static IOService resource;
IOService *IOService::getResourceService(){return &resource;}
class IOHIDDevice:public IOService {public:
    virtual IOWorkLoop *getWorkLoop() const{return nullptr;}
    virtual OSString *newTransportString() const{return nullptr;}
    virtual OSString *newProductString() const{return nullptr;}
    virtual IOReturn newReportDescriptor(IOMemoryDescriptor **) const{return 0;}
    bool start(IOService *) override {
        start_calls++;if(fail_start&&start_calls==2)return false;
        auto *n=dynamic_cast<OSNumber *>(getProperty("PrimaryUsage"));CHECK(n);
        unsigned i=n->value==3?0:1;
        // Host-only original220slot table carries this peer's correct RTTI
        // and host virtual prefix, then the exact slots touched by the methods.
        *(uintptr_t **)&driver_peers[i]=original_driver_storage+2;
        driver_peers[i].properties=properties;children.push_back(&driver_peers[i]);return true;
    }
};
static const void *gIOServicePlane=(void *)1;
class IOCatalogue {};static IOCatalogue catalogue;static IOCatalogue *gIOCatalogue=&catalogue;
static bool elden_motion_catalogue_add(IOCatalogue *p,OSArray *a,bool match){CHECK(p==gIOCatalogue&&a&&!match);catalogue_calls++;return !fail_catalogue;}
static OSObject *OSUnserializeXML(const char *xml){CHECK(std::strstr(xml,"IOHIDEventDriver")&&!std::strstr(xml,"Orientation"));return new OSArray;}
struct OSMetaClass {static void *preModLoad(const char *){return (void *)1;}static int postModLoad(void *){return 0;}};
static struct {const char *name="kernel";} g_kernel_kmod_info;
static int OSKextLoadKextWithIdentifier(const char *){return 0;}
static void elden_motion_hid_static_init(){}
static bool ml_at_interrupt_context(){return interrupt_context;}
static uint64_t mach_absolute_time(){return clock_ticks;}
struct mach_timebase_info_data_t {uint32_t numer,denom;};
static void clock_timebase_info(mach_timebase_info_data_t *p){p->numer=625;p->denom=12;}
#define OSDeclareDefaultStructors(c)
#define OSDefineMetaClassAndStructors(c,p)
#define OSDynamicCast(T,o) dynamic_cast<T *>(o)
#define OSSafeReleaseNULL(p) do{if(p){p->release();p=nullptr;}}while(0)
struct Event:OSObject {uint64_t timestamp;int32_t value[3];uint32_t seq,kind;};
static OSObject *make(unsigned kind,uint64_t time,int32_t x,int32_t y,int32_t z,unsigned type,unsigned sub,unsigned seq,unsigned options){
    CHECK(!type&&!sub&&!options&&seq);created++;if(fail_event)return nullptr;auto *e=new Event;e->timestamp=time;e->value[0]=x;e->value[1]=y;e->value[2]=z;e->seq=seq;e->kind=kind;return e;
}
static OSObject *accel(uint64_t t,int32_t x,int32_t y,int32_t z,unsigned type,unsigned sub,unsigned seq,unsigned options){return make(1,t,x,y,z,type,sub,seq,options);}
static OSObject *gyro(uint64_t t,int32_t x,int32_t y,int32_t z,unsigned type,unsigned sub,unsigned seq,unsigned options){return make(2,t,x,y,z,type,sub,seq,options);}
static void peerDispatch(IOService *,OSObject *o,unsigned options){CHECK(!options&&dynamic_cast<Event *>(o));dispatched++;}
static int set_properties(IOService *,OSObject *){forwards++;return set_error;}
#define ELDEN_MOTION_DRIVER_VPTR ((uintptr_t)(original_driver_storage+2))
#define ELDEN_MOTION_DRIVER_DISPATCH ((uintptr_t)&peerDispatch)
#define ELDEN_MOTION_DRIVER_SET_PROPERTIES ((uintptr_t)&set_properties)
#define ELDEN_MOTION_ACCEL_EVENT ((uintptr_t)&accel)
#define ELDEN_MOTION_GYRO_EVENT ((uintptr_t)&gyro)
static const uintptr_t *elden_motion_driver_table=original_driver_table;
#include "NativeHidMethods.inc"
static int admit(void *,uint64_t owner,uint64_t epoch,unsigned kind,int cleanup){CHECK(owner==11&&epoch==7&&(kind==1||kind==2));return cleanup?0:admission_error;}
static int demand(void *,uint64_t owner,uint64_t epoch,unsigned kind,unsigned hz){CHECK(owner==11&&epoch==7&&(kind==1||kind==2)&&hz<=400);demand_calls++;last_demand=hz;return demand_error;}
static EldenMotionNativeProducer config(){EldenMotionNativeProducer p={};p.owner=11;p.epoch=7;p.admit=admit;p.demand=demand;p.suid[0]={1,2};p.suid[1]={3,4};
    p.max_abs_q16[0]=p.max_abs_q16[1]=16*65536;p.max_age_us=100000;p.max_future_us=1000;p.negate_axes[0]=7;return p;}
static void setup(){scenarios++;producer={};devices[0]=devices[1]=nullptr;attempted=stopping=ready=0;clock_ticks=19200000;
    interrupt_context=fail_event=fail_start=fail_catalogue=false;admission_error=demand_error=set_error=0;
    start_calls=dispatched=created=demand_calls=forwards=catalogue_calls=0;
    for(unsigned i=0;i<220;i++)original_driver_table[i]=i+1;
    PeerDriver actual;uintptr_t *actual_table=*(uintptr_t **)&actual;
    for(unsigned i=0;i<8;i++)original_driver_table[i]=actual_table[i]; // host virtual prefix and RTTI remain valid
    original_driver_table[44]=(uintptr_t)&set_properties;original_driver_table[198]=(uintptr_t)&peerDispatch;
    for(unsigned i=0;i<222;i++)original_driver_storage[i]=i<2?actual_table[(int)i-2]:original_driver_table[i-2];
}
int main(){
    setup();CHECK(elden_motion_native_hid_start(nullptr)==-22&&!start_calls);auto p=config();admission_error=-13;
    CHECK(elden_motion_native_hid_start(&p)==-13&&!attempted&&!catalogue_calls);
    setup();p=config();fail_start=true;CHECK(elden_motion_native_hid_start(&p)==-19&&stopping&&devices[0]->closed&&demand_calls==2);
    setup();p=config();CHECK(!elden_motion_native_hid_start(&p)&&start_calls==2&&catalogue_calls==2&&ready);
    CHECK(elden_motion_native_hid_start(&p)==-16&&start_calls==2);
    CHECK(elden_motion_native_hid_bind(11,7)==1&&devices[0]->driver==&driver_peers[0]&&devices[1]->driver==&driver_peers[1]);
    CHECK(*(uintptr_t **)&driver_peers[0]==hook_table+2&&hook_table[46]==(uintptr_t)intervalHook);
    // Dispatch action refuses all pre-construction faults without changing the
    // previous accepted state or calling an original event constructor.
    EldenMotionSample s={{1,2},clock_ticks,{0,65536,0},1,EldenMotionG,3};
    s.suid.low=99;CHECK(elden_motion_native_hid_packet(&s)==kIOReturnNotPermitted&&!created);s.suid.low=1;
    s.q16[0]=17*65536;CHECK(elden_motion_native_hid_packet(&s)==kIOReturnBadArgument&&!created);s.q16[0]=0;
    s.timestamp_ticks=clock_ticks-1920001;CHECK(elden_motion_native_hid_packet(&s)==kIOReturnNotPermitted&&!created);s.timestamp_ticks=clock_ticks;
    CHECK(!elden_motion_native_hid_packet(&s)&&created==1&&dispatched==1&&devices[0]->sequence==1);
    CHECK(elden_motion_native_hid_packet(&s)==kIOReturnNotPermitted&&created==1);
    s.timestamp_ticks++;fail_event=true;
    CHECK(elden_motion_native_hid_packet(&s)==kIOReturnNoMemory&&devices[0]->sequence==1&&dispatched==1);fail_event=false;
    CHECK(!elden_motion_native_hid_packet(&s)&&devices[0]->sequence==2&&dispatched==2);
    // Real scoped hook goes through the device workloop, forwards the original
    // method and rolls demand back if that original method rejects properties.
    OSNumber interval(40000);OSDictionary dict;dict.values["ReportInterval"]=&interval;
    CHECK(intervalHook(&driver_peers[0],&dict)==0&&demand_calls==1&&last_demand==25&&devices[0]->last_hz==25);
    interval.value=UINT64_MAX;unsigned before=demand_calls;CHECK(EldenMotionHid::demandAction(devices[0],&driver_peers[0],&dict,nullptr,nullptr)==kIOReturnBadArgument&&demand_calls==before);
    interval.value=10000;set_error=9;CHECK(EldenMotionHid::demandAction(devices[0],&driver_peers[0],&dict,nullptr,nullptr)==9&&devices[0]->last_hz==25&&last_demand==25);
    set_error=0;demand_error=-5;CHECK(EldenMotionHid::demandAction(devices[0],&driver_peers[0],&dict,nullptr,nullptr)==kIOReturnNotReady);
    demand_error=0;CHECK(elden_motion_native_hid_stop(12,7)==-116);CHECK(!elden_motion_native_hid_stop(11,7)&&devices[0]->closed&&devices[1]->closed);
    unsigned previous=created;CHECK(elden_motion_native_hid_packet(&s)==-19&&created==previous&&!ready);
    setup();p=config();p.demand=nullptr;CHECK(elden_motion_native_hid_start(&p)==-22&&!attempted);
    setup();p=config();CHECK(!elden_motion_native_hid_start(&p));CHECK(elden_motion_native_hid_bind(11,7)==1);
    uintptr_t saved=hook_table[9];hook_table[9]^=1;
    s={{1,2},clock_ticks,{0,65536,0},1,EldenMotionG,3};CHECK(elden_motion_native_hid_packet(&s)==kIOReturnNotReady&&!created);hook_table[9]=saved;
    demand_error=-5;CHECK(elden_motion_native_hid_stop(11,7)==-5&&!ready&&stopping);
    CHECK(elden_motion_native_hid_packet(&s)==-19&&!created);
    for(auto *o:allocated)delete o;
    std::printf("PASS: %u actual motion native HID scenarios/%u checks; producer gating/publication/bind/events/refusal/demand rollback/stop; OS and original methods modeled\n",scenarios,checks);return 0;
}
