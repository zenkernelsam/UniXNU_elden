/* SPDX-License-Identifier: BSD-2-Clause-Patent */
namespace {
static EldenCameraFrameOutput *outputs[2];static uint32_t withdrawn[2],opened[2],classes_registered;
static int service_status(unsigned sensor,EldenCameraOutputStatus *s){
    if(sensor>1||!s||ml_at_interrupt_context()||__atomic_load_n(&withdrawn[sensor],__ATOMIC_ACQUIRE))return -19;
    auto *p=__atomic_load_n(&outputs[sensor],__ATOMIC_ACQUIRE);if(!p)return -19;
    int rc=elden_camera_output_status(p,s);return rc?rc:s->error?s->error:!s->prepared||s->stopping||s->retired?-19:0;
}
static IOReturn service_result(int rc){return !rc?kIOReturnSuccess:rc==-11||rc==-16?kIOReturnBusy:rc==-22?kIOReturnBadArgument:
    rc==-13||rc==-125?kIOReturnNotPermitted:rc==-116?kIOReturnNotFound:rc==-19?kIOReturnNotReady:rc==-12?kIOReturnNoMemory:kIOReturnIOError;}
}
class EldenCameraClient final : public IOUserClient {
    OSDeclareDefaultStructors(EldenCameraClient)
    unsigned sensor=2;uint64_t held=0;unsigned slot=4;IOLock *serial=nullptr;
public:
    void use(unsigned s){sensor=s;}
    bool start(IOService *provider) override{
        if(sensor>1||!IOUserClient::start(provider))return false;
        serial=IOLockAlloc();if(!serial){IOUserClient::stop(provider);return false;}
        return true;
    }
    IOReturn clientClose() override{
        if(sensor<2){__atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);(void)elden_camera_output_close(outputs[sensor]);}
        if(serial){IOLockLock(serial);uint64_t ticket=held;held=0;slot=4;
            if(ticket)(void)elden_camera_output_release(outputs[sensor],ticket);IOLockUnlock(serial);}
        if(sensor<2)__atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);
        terminate();return kIOReturnSuccess;
    }
    void free() override{
        if(sensor<2){__atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);if(outputs[sensor])(void)elden_camera_output_close(outputs[sensor]);}
        if(serial){IOLockLock(serial);uint64_t ticket=held;held=0;
            if(ticket&&sensor<2)(void)elden_camera_output_release(outputs[sensor],ticket);IOLockUnlock(serial);IOLockFree(serial);serial=nullptr;}
        IOUserClient::free();
    }
    IOReturn clientMemoryForType(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory) override{
        if(memory)*memory=nullptr;if(!memory||!options||!serial||type>=4||sensor>1)return kIOReturnBadArgument;
        IOLockLock(serial);void *p=held&&type==slot?elden_camera_output_memory(outputs[sensor],held,slot):nullptr;
        IOLockUnlock(serial);if(!p)return kIOReturnNotReady;*options=kIOMapReadOnly;*memory=(IOMemoryDescriptor *)p;return kIOReturnSuccess;
    }
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) override{
        if(!a||!serial||sensor>1||a->structureInputSize||a->structureOutputSize||a->structureInputDescriptor||a->structureOutputDescriptor)return kIOReturnBadArgument;
        IOLockLock(serial);int rc=-22;
        if(selector==EldenCameraServiceInfo&&a->scalarInputCount==0&&a->scalarOutputCount==8&&a->scalarOutput){
            EldenCameraOutputStatus status={};rc=service_status(sensor,&status);
            if(!rc)rc=elden_camera_output_info(outputs[sensor],a->scalarOutput);
        }else if(selector==EldenCameraServiceAcquire&&a->scalarInputCount==0&&a->scalarOutputCount==12&&a->scalarOutput){
            EldenCameraOutputStatus status={};rc=held?-16:service_status(sensor,&status);EldenCameraOutputFrame frame={};
            if(!rc)rc=elden_camera_output_acquire(outputs[sensor],&frame);
            if(!rc){held=frame.ticket;slot=frame.slot;uint64_t values[]={frame.ticket,frame.slot,frame.sequence,frame.timestamp_ns,frame.bytes,
                frame.width,frame.height,frame.stride,frame.bayer,frame.rotation,frame.sensor,1};for(unsigned i=0;i<12;i++)a->scalarOutput[i]=values[i];}
        }else if(selector==EldenCameraServiceRelease&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==0){
            rc=held&&a->scalarInput[0]==held?elden_camera_output_release(outputs[sensor],held):-116;
            if(!rc){held=0;slot=4;}
        }
        IOLockUnlock(serial);return service_result(rc);
    }
};
OSDefineMetaClassAndStructors(EldenCameraClient,IOUserClient)
class EldenCameraService final : public IOService {
    OSDeclareDefaultStructors(EldenCameraService)
    unsigned sensor=2;
    IOReturn client(task_t task,void *security,UInt32 type,OSDictionary *properties,IOUserClient **out){
        if(out)*out=nullptr;if(!out||sensor>1||type)return kIOReturnBadArgument;
        if(IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPermitted;
        EldenCameraOutputStatus status={};int rc=service_status(sensor,&status);if(rc)return service_result(rc);
        uint32_t zero=0;if(!__atomic_compare_exchange_n(&opened[sensor],&zero,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return kIOReturnBusy;
        auto *c=OSTypeAlloc(EldenCameraClient);if(!c)return kIOReturnNoMemory;c->use(sensor);
        if(!c->initWithTask(task,security,type,properties)||!c->attach(this)){c->release();return kIOReturnError;}
        if(!c->start(this)){c->detach(this);c->release();return kIOReturnNotReady;}*out=c;return kIOReturnSuccess;
    }
public:
    void use(unsigned s){sensor=s;}
    bool start(IOService *provider) override{
        if(sensor>1||!IOService::start(provider))return false;setName(sensor?"EldenCameraFront":"EldenCameraRear");
        // Grants only service access to the named camera provider process;
        // TCC/AVFoundation camera authorization remains its real OS boundary.
        if(!setProperty("EldenCameraProtocolVersion",1u,32)||!setProperty("NativeSandboxGrantProcess","eldencamerad")){
            IOService::stop(provider);return false;}return true;
    }
    IOReturn newUserClient(task_t task,void *s,UInt32 t,OSDictionary *p,IOUserClient **out) override{return client(task,s,t,p,out);}
    IOReturn newUserClient(task_t task,void *s,UInt32 t,IOUserClient **out) override{return client(task,s,t,nullptr,out);}
};
OSDefineMetaClassAndStructors(EldenCameraService,IOService)
extern "C" int elden_camera_service_publish(EldenCameraFrameOutput *output,unsigned sensor){
    if(!output||sensor>1||ml_at_interrupt_context())return -22;EldenCameraOutputStatus status={};int rc=elden_camera_output_status(output,&status);
    if(rc||!status.prepared||status.error||status.stopping||status.retired)return rc?rc:-19;
    uint64_t info[8]={};rc=elden_camera_output_info(output,info);if(rc||info[1]!=sensor)return rc?rc:-116;
    uint32_t zero=0;if(__atomic_compare_exchange_n(&classes_registered,&zero,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        void *h=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!h)return -19;elden_camera_service_static_init();
        if(OSMetaClass::postModLoad(h))return -19;__atomic_store_n(&classes_registered,2u,__ATOMIC_RELEASE);
    }else if(__atomic_load_n(&classes_registered,__ATOMIC_ACQUIRE)!=2)return -16;
    EldenCameraFrameOutput *empty=nullptr;if(!__atomic_compare_exchange_n(&outputs[sensor],&empty,output,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    auto *service=OSTypeAlloc(EldenCameraService);if(!service)return -12;service->use(sensor);
    if(!service->init()||!service->attach(IOService::getPlatform())||!service->start(IOService::getPlatform())){__atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);return -19;}
    service->registerService();return 0; // retain service/output to reset
}
extern "C" int elden_camera_service_withdraw(unsigned sensor){if(sensor>1||ml_at_interrupt_context())return -22;
    __atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);auto *out=__atomic_load_n(&outputs[sensor],__ATOMIC_ACQUIRE);
    return out?elden_camera_output_stop(out):0;
}
/* Read-only registry peek so the capture owner can compare the published
 * slot to its own output before any sensor-only operation. */
extern "C" EldenCameraFrameOutput *elden_camera_service_output(unsigned sensor){
    return sensor>1?nullptr:__atomic_load_n(&outputs[sensor],__ATOMIC_ACQUIRE);
}
/* Withdraw only when the published slot is exactly the caller's own output.
 * A partially published service (slot committed, start failed) is covered
 * since the CAS already placed this output in the slot. A slot belonging to
 * a different session's output is refused instead of withdrawn. */
extern "C" int elden_camera_service_withdraw_output(unsigned sensor,EldenCameraFrameOutput *output){
    if(sensor>1||ml_at_interrupt_context())return -22;
    auto *out=__atomic_load_n(&outputs[sensor],__ATOMIC_ACQUIRE);if(out!=output)return -16;
    __atomic_store_n(&withdrawn[sensor],1u,__ATOMIC_RELEASE);return out?elden_camera_output_stop(out):0;
}
