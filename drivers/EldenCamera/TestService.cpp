/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute actual IOUserClient service methods; IOKit/provider peers modeled. */
#include "Service.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"service line%u %s\n",__LINE__,#x);exit(1);}}while(0)
using IOReturn=int;using UInt32=uint32_t;using IOOptionBits=uint32_t;using task_t=void *;using IOLock=std::mutex;
enum{kIOReturnSuccess=0,kIOReturnBusy=1,kIOReturnBadArgument=2,kIOReturnNotPermitted=3,kIOReturnNotFound=4,kIOReturnNotReady=5,kIOReturnNoMemory=6,kIOReturnIOError=7,kIOReturnError=8,kIOMapReadOnly=0x100};
struct OSObject{virtual ~OSObject()=default;};struct OSDictionary:OSObject{};struct IOExternalMethodDispatch{};
struct IOExternalMethodArguments{uint32_t scalarInputCount=0,scalarOutputCount=0;const uint64_t *scalarInput=nullptr;uint64_t *scalarOutput=nullptr;
    size_t structureInputSize=0,structureOutputSize=0;void *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;};
class IOUserClient;
class IOService:public OSObject{public:virtual bool start(IOService *){return true;}virtual void stop(IOService *){}
    virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **){return 0;}virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **){return 0;}
    virtual void free(){}bool init(){return true;}bool attach(IOService *){return true;}void detach(IOService *){}void release(){delete this;}
    void setName(const char *){}bool setProperty(const char *,unsigned,unsigned){return true;}bool setProperty(const char *,const char *){return true;}void registerService(){}
    static IOService *getPlatform(){static IOService s;return &s;}};
static int privilege;
class IOUserClient:public IOService{public:virtual IOReturn clientClose(){return 0;}virtual IOReturn clientMemoryForType(UInt32,IOOptionBits *,class IOMemoryDescriptor **){return 0;}
    virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *){return 0;}
    bool initWithTask(task_t,void *,UInt32,OSDictionary *){return true;}static IOReturn clientHasPrivilege(task_t,const char *){return privilege;}void terminate(){}
};
class IOMemoryDescriptor{};static const char *kIOClientPrivilegeAdministrator="root";
class OSMetaClass{public:static void *preModLoad(const char *){return (void *)1;}static int postModLoad(void *){return 0;}};
static struct {const char *name;} g_kernel_kmod_info={"kernel"};static void elden_camera_service_static_init(){}static bool ml_at_interrupt_context(){return false;}
static IOLock *IOLockAlloc(){return new IOLock;}static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}static void IOLockFree(IOLock *p){delete p;}
#define OSDeclareDefaultStructors(x)
#define OSDefineMetaClassAndStructors(x,y)
#define OSTypeAlloc(x) new x
static IOMemoryDescriptor memory;static unsigned releases,stops,closes,acquired;static int output_error;
extern "C" int elden_camera_output_status(EldenCameraFrameOutput *,EldenCameraOutputStatus *out){*out={};out->prepared=1;out->error=output_error;return 0;}
extern "C" int elden_camera_output_info(EldenCameraFrameOutput *,uint64_t out[8]){uint64_t values[]={1,0,4080,3072,5104,10,1,90};memcpy(out,values,sizeof(values));return 0;}
extern "C" int elden_camera_output_acquire(EldenCameraFrameOutput *,EldenCameraOutputFrame *out){acquired++;if(output_error)return output_error;*out={13,42,1000000,5104*3072,1,4080,3072,5104,1,90,0};return 0;}
extern "C" int elden_camera_output_release(EldenCameraFrameOutput *,uint64_t ticket){CHECK(ticket==13);releases++;return 0;}
extern "C" void *elden_camera_output_memory(EldenCameraFrameOutput *,uint64_t ticket,unsigned slot){CHECK(ticket==13&&slot==1);return &memory;}
extern "C" int elden_camera_output_close(EldenCameraFrameOutput *){closes++;return 0;}
extern "C" int elden_camera_output_stop(EldenCameraFrameOutput *){stops++;return -16;}
#include "ServiceMethods.inc"
int main(){auto *output=(EldenCameraFrameOutput *)1;outputs[0]=output;classes_registered=2;IOService provider;EldenCameraClient client;client.use(0);CHECK(client.start(&provider));
    uint64_t in[2]={13,0},out[12]={};IOExternalMethodArguments args{};args.scalarOutput=out;args.scalarOutputCount=8;
    CHECK(client.externalMethod(0,&args,nullptr,nullptr,nullptr)==0&&out[2]==4080);
    args.scalarOutputCount=12;CHECK(client.externalMethod(1,&args,nullptr,nullptr,nullptr)==0&&out[0]==13);CHECK(client.externalMethod(1,&args,nullptr,nullptr,nullptr)==kIOReturnBusy);
    IOMemoryDescriptor *mapped=nullptr;IOOptionBits options=0;CHECK(client.clientMemoryForType(1,&options,&mapped)==0&&options==kIOMapReadOnly&&mapped==&memory);
    CHECK(client.clientMemoryForType(0,&options,&mapped)==kIOReturnNotReady&&!mapped);
    for(unsigned which=0;which<5;which++){auto bad=args;if(which==0)bad.structureInputSize=1;if(which==1)bad.structureOutputSize=1;
        if(which==2)bad.structureInputDescriptor=(void *)1;if(which==3)bad.scalarOutputCount=11;if(which==4)bad.scalarOutput=nullptr;
        CHECK(client.externalMethod(1,&bad,nullptr,nullptr,nullptr)==kIOReturnBadArgument);}
    args={};args.scalarInput=in;args.scalarInputCount=1;in[0]=14;CHECK(client.externalMethod(2,&args,nullptr,nullptr,nullptr)==kIOReturnNotFound&&!releases);
    in[0]=13;CHECK(client.externalMethod(2,&args,nullptr,nullptr,nullptr)==0&&releases==1);CHECK(client.externalMethod(2,&args,nullptr,nullptr,nullptr)==kIOReturnNotFound);
    /* Client teardown withdraws admission and returns its own ticket only;
     * physical stop/retirement belongs to the serialized capture owner. */
    CHECK(client.clientClose()==0&&closes==1&&!stops&&withdrawn[0]);client.free();CHECK(closes==2&&!stops);
    /* The registry peek exposes which output owns the published slot; the
     * identity-checked withdrawal refuses a foreign output without touching
     * the slot, while the own output's slot still retires through the
     * existing physical stop path. */
    withdrawn[0]=0;
    CHECK(elden_camera_service_output(0)==output&&elden_camera_service_output(1)==nullptr&&elden_camera_service_output(2)==nullptr);
    CHECK(elden_camera_service_withdraw_output(0,(EldenCameraFrameOutput *)2)==-16&&outputs[0]==output&&!withdrawn[0]&&!stops);
    CHECK(elden_camera_service_withdraw_output(0,output)==-16&&withdrawn[0]&&stops==1);
    CHECK(elden_camera_service_withdraw_output(2,output)==-22);
    withdrawn[0]=0;EldenCameraService service;service.use(0);IOUserClient *user=nullptr;privilege=kIOReturnNotPermitted;
    CHECK(service.newUserClient((void *)1,nullptr,0,&user)==kIOReturnNotPermitted&&!user);privilege=0;
    CHECK(service.newUserClient((void *)1,nullptr,0,&user)==0&&user);IOUserClient *next=nullptr;CHECK(service.newUserClient((void *)2,nullptr,0,&next)==kIOReturnBusy&&!next);
    user->clientClose();user->free();delete user;CHECK(service.newUserClient((void *)2,nullptr,0,&next)==kIOReturnNotReady);
    printf("PASS actual read-only camera service privilege/ticket/close/ABI methods: %u checks; OS peers modeled\n",checks);return 0;
}
