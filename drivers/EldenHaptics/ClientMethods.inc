/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Exact actual native client/service methods, shared with host ABI peers. */
class EldenHapticsClient final : public IOUserClient {
    OSDeclareDefaultStructors(EldenHapticsClient)
    uint64_t owner=0,epoch=0;
public:
    bool start(IOService *provider) override{
        if(!IOUserClient::start(provider))return false;
        if(elden_haptics_native_open(&owner,&epoch)){IOUserClient::stop(provider);return false;}return true;
    }
    IOReturn clientClose() override{uint64_t who=__atomic_exchange_n(&owner,0u,__ATOMIC_ACQ_REL);if(who)(void)elden_haptics_native_close(who,epoch);terminate();return kIOReturnSuccess;}
    void free() override{uint64_t who=__atomic_exchange_n(&owner,0u,__ATOMIC_ACQ_REL);if(who)(void)elden_haptics_native_close(who,epoch);IOUserClient::free();}
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) override{
        uint64_t who=__atomic_load_n(&owner,__ATOMIC_ACQUIRE);
        if(!a||!who||a->structureInputSize||a->structureOutputSize||a->structureInputDescriptor||a->structureOutputDescriptor)return kIOReturnBadArgument;
        int rc=-22;
        if(selector==0&&a->scalarInputCount==0&&a->scalarOutputCount==8&&a->scalarOutput){elden_haptics_native_info(a->scalarOutput);return kIOReturnSuccess;}
        if(selector==1&&a->scalarInputCount==6&&a->scalarInput&&a->scalarOutputCount==0){
            const uint64_t *in=a->scalarInput;
            if(in[2]>UINT32_MAX||in[3]>3||in[4]>127||in[5]>32767)return kIOReturnBadArgument;
            rc=elden_haptics_native_submit(who,epoch,in[0],in[1],(uint32_t)in[2],(uint8_t)in[3],(uint8_t)in[4],(uint16_t)in[5]);
        }else if(selector==2&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==0)rc=elden_haptics_native_cancel(who,epoch,a->scalarInput[0]);
        else if(selector==3&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==3&&a->scalarOutput){
            EldenHapticsResult result;rc=elden_haptics_native_result(who,epoch,a->scalarInput[0],&result);
            if(!rc){a->scalarOutput[0]=result.state;a->scalarOutput[1]=(uint64_t)(int64_t)result.error;a->scalarOutput[2]=result.id;}
        }
        else if(selector==4&&a->scalarInputCount==4&&a->scalarInput&&a->scalarOutputCount==0){
            const uint64_t *in=a->scalarInput;
            if(in[1]>UINT32_MAX||in[3]>32767)return kIOReturnBadArgument;
            rc=elden_haptics_native_submit_effect(who,epoch,in[0],(uint32_t)in[1],in[2],(uint16_t)in[3]);
        }
        else if(selector==5&&a->scalarInputCount==1&&a->scalarInput&&a->scalarOutputCount==4&&a->scalarOutput){
            if(a->scalarInput[0]>UINT32_MAX)return kIOReturnBadArgument;
            rc=elden_haptics_native_effect_info((uint32_t)a->scalarInput[0],a->scalarOutput);
        }
        return rc==0?kIOReturnSuccess:rc==-16?kIOReturnBusy:rc==-13||rc==-125?kIOReturnNotPermitted:rc==-116?kIOReturnNotFound:
            rc==-19?kIOReturnNotReady:rc==-12?kIOReturnNoMemory:rc==-110?kIOReturnTimeout:rc==-22?kIOReturnBadArgument:kIOReturnIOError;
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
        if(!setProperty("EldenHapticsProtocolVersion",2u,32)||!setProperty("CalibrationAvailable",(bool)ELDEN_HAPTICS_HAS_LIMITS)||
           !setProperty("EffectCatalogCount",(unsigned)ELDEN_HAPTICS_EFFECT_COUNT,32)){
            IOService::stop(provider);return false;}return true;
    }
    IOReturn newUserClient(task_t task,void *s,UInt32 t,OSDictionary *p,IOUserClient **out) override{return client(task,s,t,p,out);}
    IOReturn newUserClient(task_t task,void *s,UInt32 t,IOUserClient **out) override{return client(task,s,t,nullptr,out);}
};
OSDefineMetaClassAndStructors(EldenHapticsService,IOService)
