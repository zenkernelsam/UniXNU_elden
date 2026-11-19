/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native methods shared by compilation and host OS peers. */
class EldenMotionHid;
namespace {
using MotionEvent=OSObject *(*)(uint64_t,int32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t,uint32_t);
using DispatchEvent=void (*)(IOService *,OSObject *,uint32_t);
using MotionSetProperties=IOReturn (*)(IOService *,OSObject *);
static EldenMotionNativeProducer producer={};
static EldenMotionHid *devices[2];static uint32_t attempted,stopping,ready;static uintptr_t hook_table[222];
static int qualified(unsigned kind,int cleanup){
    if(!producer.admit||kind<1||kind>2||ml_at_interrupt_context())return -13;
    if(!cleanup&&__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return -125;
    return producer.admit(producer.context,producer.owner,producer.epoch,kind,cleanup);
}
static IOService *findDriver(IORegistryEntry *p,unsigned depth){
    if(!p||!depth)return nullptr;OSIterator *it=p->getChildIterator(gIOServicePlane);if(!it)return nullptr;
    IOService *result=nullptr;unsigned count=0;
    while(OSObject *o=it->getNextObject()){
        if(++count>32)break;auto *e=OSDynamicCast(IORegistryEntry,o);if(!e)continue;
        if(e->metaCast("IOHIDEventDriver")){result=OSDynamicCast(IOService,e);if(result)result->retain();}
        else result=findDriver(e,depth-1);if(result)break;
    }
    it->release();return result;
}
static bool numberIs(IORegistryEntry *e,const char *key,unsigned expected){
    OSObject *v=e->copyProperty(key);OSNumber *n=OSDynamicCast(OSNumber,v);bool ok=n&&n->unsigned32BitValue()==expected;if(v)v->release();return ok;
}
static IOReturn intervalHook(IOService *,OSObject *);
static bool originalTable(uintptr_t *v){
    uintptr_t *original=(uintptr_t *)ELDEN_MOTION_DRIVER_VPTR;
    for(unsigned i=0;i<220;i++)if(original[i]!=elden_motion_driver_table[i])return false;
    if(v==hook_table+2){for(unsigned i=0;i<220;i++)if(v[i]!=(i==44?(uintptr_t)intervalHook:original[i]))return false;return true;}
    if(v!=original)return false;return true;
}
static bool personality(unsigned kind){
    const EldenMotionHidKind *k=elden_motion_hid_kind(kind);if(!k||!gIOCatalogue)return false;
    char xml[1024];int n=snprintf(xml,sizeof(xml),"<array><dict><key>IOClass</key><string>IOHIDEventDriver</string>"
        "<key>IOProviderClass</key><string>IOHIDInterface</string><key>CFBundleIdentifier</key><string>com.apple.iokit.IOHIDFamily</string>"
        "<key>HIDDefaultBehavior</key><string></string><key>IOProbeScore</key><integer>6000</integer>"
        "<key>IOPropertyMatch</key><dict><key>Product</key><string>%s</string></dict>"
        "<key>DeviceUsagePairs</key><array><dict><key>DeviceUsagePage</key><integer>%u</integer><key>DeviceUsage</key><integer>%u</integer></dict></array>"
        "<key>PrimaryUsagePage</key><integer>%u</integer><key>PrimaryUsage</key><integer>%u</integer><key>Built-In</key><true/></dict></array>",
        k->product,k->page,k->usage,k->page,k->usage);
    if(n<0||(size_t)n>=sizeof(xml))return false;OSObject *o=OSUnserializeXML(xml);auto *a=OSDynamicCast(OSArray,o);
    bool ok=a&&elden_motion_catalogue_add(gIOCatalogue,a,false);if(o)o->release();return ok;
}
}
class EldenMotionHid final : public IOHIDDevice {
    OSDeclareDefaultStructors(EldenMotionHid)
public:
    unsigned kind=0;IOWorkLoop *loop=nullptr;IOService *driver=nullptr;
    uint64_t last_ticks=0;uint32_t sequence=0,closed=0,last_hz=0;
    IOWorkLoop *getWorkLoop() const override{return loop;}
    OSString *newTransportString() const override{return OSString::withCString("SSC");}
    OSString *newProductString() const override{const EldenMotionHidKind *k=elden_motion_hid_kind(kind);return k?OSString::withCString(k->product):nullptr;}
    IOReturn newReportDescriptor(IOMemoryDescriptor **out) const override{
        if(!out)return kIOReturnBadArgument;*out=nullptr;uint32_t size=0;const uint8_t *p=elden_motion_hid_descriptor(kind,&size);
        if(!p)return kIOReturnBadArgument;*out=IOBufferMemoryDescriptor::withBytes(p,size,kIODirectionNone);return *out?kIOReturnSuccess:kIOReturnNoMemory;
    }
    bool start(IOService *provider) override{
        if(qualified(kind,0))return false;loop=IOWorkLoop::workLoop();if(!loop)return false;
        const EldenMotionHidKind *k=elden_motion_hid_kind(kind);
        if(!k||!setProperty("Built-In",kOSBooleanTrue)||!setProperty("HIDDefaultBehavior",kOSBooleanTrue)||
           !setProperty("PrimaryUsagePage",k->page,32)||!setProperty("PrimaryUsage",k->usage,32))return false;
        return IOHIDDevice::start(provider);
    }
    bool bind(){
        IOService *bound=__atomic_load_n(&driver,__ATOMIC_ACQUIRE);if(bound)return originalTable(*(uintptr_t **)bound);
        IOService *found=findDriver(this,4);if(!found)return false;
        const EldenMotionHidKind *k=elden_motion_hid_kind(kind);
        if(!originalTable(*(uintptr_t **)found)||!numberIs(found,"PrimaryUsagePage",k->page)||!numberIs(found,"PrimaryUsage",k->usage)||
           found->getProperty("Built-In")!=kOSBooleanTrue){found->release();return false;}
        uintptr_t *original=(uintptr_t *)ELDEN_MOTION_DRIVER_VPTR;
        for(unsigned i=0;i<220;i++)if(original[i]!=elden_motion_driver_table[i]){found->release();return false;}
        __atomic_store_n(&driver,found,__ATOMIC_RELEASE);__atomic_store_n((uintptr_t **)found,hook_table+2,__ATOMIC_RELEASE);return true;
    }
    static IOReturn dispatch(OSObject *object,void *argument,void *,void *,void *){
        auto *self=OSDynamicCast(EldenMotionHid,object);auto *s=(EldenMotionSample *)argument;
        if(!self||!s||self->closed||qualified(self->kind,0)||s->kind!=self->kind)return kIOReturnNotReady;
        unsigned at=self->kind-1;
        if(s->suid.low!=producer.suid[at].low||s->suid.high!=producer.suid[at].high)return kIOReturnNotPermitted;
        if(!s->timestamp_ticks||s->timestamp_ticks<=self->last_ticks||self->sequence==UINT32_MAX)return kIOReturnNotPermitted;
        for(unsigned i=0;i<3;i++){int64_t v=s->q16[i];if(v<0)v=-v;if(v>producer.max_abs_q16[at])return kIOReturnBadArgument;}
        uint64_t now=mach_absolute_time(),age=((uint64_t)producer.max_age_us*96+4)/5,future=((uint64_t)producer.max_future_us*96+4)/5;
        if((s->timestamp_ticks<=now&&now-s->timestamp_ticks>age)||(s->timestamp_ticks>now&&s->timestamp_ticks-now>future))return kIOReturnNotPermitted;
        if(!self->bind())return kIOReturnNotReady;
        EldenMotionHidEvent e;uint32_t sequence=self->sequence+1;
        if(elden_motion_hid_event(s,producer.negate_axes[at],sequence,&e))return kIOReturnBadArgument;
        MotionEvent make=(MotionEvent)(self->kind==1?ELDEN_MOTION_ACCEL_EVENT:ELDEN_MOTION_GYRO_EVENT);
        OSObject *event=make(e.timestamp_ticks,e.q16[0],e.q16[1],e.q16[2],0,0,e.sequence,0);if(!event)return kIOReturnNoMemory;
        ((DispatchEvent)ELDEN_MOTION_DRIVER_DISPATCH)(self->driver,event,0);event->release();
        self->sequence=sequence;self->last_ticks=s->timestamp_ticks;return kIOReturnSuccess;
    }
    int submit(const EldenMotionSample *s){return loop?(int)loop->runAction(dispatch,this,const_cast<EldenMotionSample *>(s)):int(kIOReturnNotReady);}
    static IOReturn closeAction(OSObject *object,void *,void *,void *,void *){
        auto *self=OSDynamicCast(EldenMotionHid,object);if(!self)return kIOReturnBadArgument;self->closed=1;
        if(qualified(self->kind,1))return kIOReturnNotReady;
        return producer.demand?(IOReturn)producer.demand(producer.context,producer.owner,producer.epoch,self->kind,0):kIOReturnNotReady;
    }
    static IOReturn demandAction(OSObject *,void *driver,void *properties,void *,void *);
    static IOReturn bindAction(OSObject *object,void *,void *,void *,void *){
        auto *self=OSDynamicCast(EldenMotionHid,object);if(!self||self->closed||qualified(self->kind,0))return kIOReturnNotReady;
        if(!self->bind())return kIOReturnNotReady;
        // The event driver can have accepted an interval before our hook was
        // installed. Catch that actual existing request once; no fake demand.
        OSObject *value=self->driver->copyProperty("ReportInterval");OSNumber *n=OSDynamicCast(OSNumber,value);
        uint32_t hz=0;bool valid=n&&elden_motion_hid_interval(n->unsigned64BitValue(),&hz)==0;
        if(value)value->release();if(valid&&hz!=self->last_hz){
            int rc=producer.demand(producer.context,producer.owner,producer.epoch,self->kind,hz);
            if(rc)return kIOReturnNotReady;self->last_hz=hz;
        }
        return kIOReturnSuccess;
    }
    int bindBeforeSamples(){return loop?(int)loop->runAction(bindAction,this):int(kIOReturnNotReady);}
    int quiesce(){return loop?(int)loop->runAction(closeAction,this):int(kIOReturnNotReady);}
    void stop(IOService *provider) override{closed=1;IOHIDDevice::stop(provider);}
    void free() override{OSSafeReleaseNULL(driver);OSSafeReleaseNULL(loop);IOHIDDevice::free();}
};
OSDefineMetaClassAndStructors(EldenMotionHid,IOHIDDevice)
namespace {
static IOReturn intervalHook(IOService *driver,OSObject *object){
    EldenMotionHid *device=nullptr;
    for(unsigned i=0;i<2;i++){auto *p=__atomic_load_n(&devices[i],__ATOMIC_ACQUIRE);
        if(p&&__atomic_load_n(&p->driver,__ATOMIC_ACQUIRE)==driver){device=p;break;}}
    return device&&device->loop?device->loop->runAction(EldenMotionHid::demandAction,device,driver,object):kIOReturnNotReady;
}
}
IOReturn EldenMotionHid::demandAction(OSObject *self,void *driver_object,void *properties,void *,void *){
    auto *device=OSDynamicCast(EldenMotionHid,self);auto *driver=(IOService *)driver_object;auto *object=(OSObject *)properties;
    if(!device||device->driver!=driver||device->closed||qualified(device->kind,0))return kIOReturnNotReady;
    OSDictionary *dict=OSDynamicCast(OSDictionary,object);OSObject *value=dict?dict->getObject("ReportInterval"):nullptr;
    uint32_t hz=device->last_hz;
    if(value){auto *number=OSDynamicCast(OSNumber,value);if(!number||elden_motion_hid_interval(number->unsigned64BitValue(),&hz))return kIOReturnBadArgument;
        int rc=producer.demand(producer.context,producer.owner,producer.epoch,device->kind,hz);if(rc)return kIOReturnNotReady;}
    IOReturn rc=((MotionSetProperties)ELDEN_MOTION_DRIVER_SET_PROPERTIES)(driver,object);
    if(rc==kIOReturnSuccess)device->last_hz=hz;
    else if(value){int undo=producer.demand(producer.context,producer.owner,producer.epoch,device->kind,device->last_hz);
        if(undo)__atomic_store_n(&stopping,1u,__ATOMIC_RELEASE);}
    return rc;
}
namespace {
static int failedStart(int rc){
    __atomic_store_n(&ready,0u,__ATOMIC_RELEASE);__atomic_store_n(&stopping,1u,__ATOMIC_RELEASE);
    // A failed second publication may leave the first device/driver alive.
    // Close actual producer demands before retaining its service to reset.
    for(unsigned i=0;i<2;i++){auto *d=__atomic_load_n(&devices[i],__ATOMIC_ACQUIRE);if(d)(void)d->quiesce();}
    return rc;
}
}
extern "C" int elden_motion_native_hid_start(const EldenMotionNativeProducer *p){
    if(!p||!p->owner||!p->epoch||!p->admit||!p->demand||!p->max_age_us||p->max_age_us>1000000||p->max_future_us>50000||ml_at_interrupt_context())return -22;
    for(unsigned i=0;i<2;i++)if((!p->suid[i].low&&!p->suid[i].high)||!p->max_abs_q16[i]||p->max_abs_q16[i]>1000*65536u||p->negate_axes[i]>7)return -22;
    if(p->suid[0].low==p->suid[1].low&&p->suid[0].high==p->suid[1].high)return -22;
    int rc=p->admit(p->context,p->owner,p->epoch,1,0);if(rc)return rc;
    rc=p->admit(p->context,p->owner,p->epoch,2,0);if(rc)return rc;
    uint32_t empty=0;if(!__atomic_compare_exchange_n(&attempted,&empty,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    producer=*p;mach_timebase_info_data_t tb;clock_timebase_info(&tb);
    if(!tb.numer||!tb.denom||(uint64_t)tb.numer*19200000!=UINT64_C(1000000000)*tb.denom)return failedStart(-95);
    if(OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily")||OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver"))return failedStart(-19);
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!handle)return failedStart(-19);
    elden_motion_hid_static_init();if(OSMetaClass::postModLoad(handle))return failedStart(-19);
    uintptr_t *original=(uintptr_t *)ELDEN_MOTION_DRIVER_VPTR;for(unsigned i=0;i<220;i++)if(original[i]!=elden_motion_driver_table[i])return failedStart(-84);
    for(unsigned i=0;i<222;i++)hook_table[i]=original[(int)i-2];hook_table[46]=(uintptr_t)intervalHook;
    for(unsigned i=0;i<2;i++){
        if(!personality(i+1))return failedStart(-19);IOService *resource=IOService::getResourceService();if(!resource)return failedStart(-19);
        auto *d=new EldenMotionHid;if(!d)return failedStart(-12);d->kind=i+1;
        __atomic_store_n(&devices[i],d,__ATOMIC_RELEASE);
        if(!d->init()||!d->attach(resource)||!d->start(resource))return failedStart(-19);
    }
    if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return failedStart(-125);
    __atomic_store_n(&ready,1u,__ATOMIC_RELEASE);
    return 0;
}
extern "C" int elden_motion_native_hid_packet(const EldenMotionSample *s){
    if(!s||s->kind<1||s->kind>2||ml_at_interrupt_context())return -22;
    if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))return -19;
    auto *device=__atomic_load_n(&devices[s->kind-1],__ATOMIC_ACQUIRE);return device?device->submit(s):-19;
}
extern "C" int elden_motion_native_hid_bind(uint64_t owner,uint64_t epoch){
    if(owner!=producer.owner||epoch!=producer.epoch||ml_at_interrupt_context())return -116;
    if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))return -19;
    for(unsigned i=0;i<2;i++){auto *d=__atomic_load_n(&devices[i],__ATOMIC_ACQUIRE);if(!d)return -19;
        int rc=d->bindBeforeSamples();if(rc)return rc==(int)kIOReturnNotReady?0:-13;}
    return 1;
}
extern "C" int elden_motion_native_hid_stop(uint64_t owner,uint64_t epoch){
    if(owner!=producer.owner||epoch!=producer.epoch)return -116;if(ml_at_interrupt_context())return -16;
    __atomic_store_n(&ready,0u,__ATOMIC_RELEASE);__atomic_store_n(&stopping,1u,__ATOMIC_RELEASE);int error=0;
    for(unsigned i=0;i<2;i++){auto *d=__atomic_load_n(&devices[i],__ATOMIC_ACQUIRE);if(d){int rc=d->quiesce();if(rc&&!error)error=rc;}}
    return error; // native services/maps retained to reset; producer must separately drain transport
}
