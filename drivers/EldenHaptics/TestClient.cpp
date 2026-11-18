/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Run the SAME native user-client/service handlers with explicit IOKit peers.
 * Final original vtable qualification remains a separate full-link audit. */
#include "Native.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using IOReturn=int;using UInt32=uint32_t;using task_t=void *;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnBusy=2,kIOReturnNotPermitted=3,kIOReturnNotFound=4,
      kIOReturnNotReady=5,kIOReturnNoMemory=6,kIOReturnError=7,kIOReturnTimeout=8,kIOReturnIOError=9};
struct OSObject {virtual ~OSObject()=default;};struct OSDictionary:OSObject{};
struct IOExternalMethodDispatch{};
struct IOExternalMethodArguments {
    uint32_t scalarInputCount=0,scalarOutputCount=0;const uint64_t *scalarInput=nullptr;uint64_t *scalarOutput=nullptr;
    size_t structureInputSize=0,structureOutputSize=0;void *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
};
static unsigned opened,closed,called;static int rc,open_rc,privilege_rc;static uint64_t args[8];static bool fail_init,fail_attach,fail_alloc;
static unsigned terminations,base_stops;
class IOUserClient;
class IOService:public OSObject {
public:
    virtual bool start(IOService *){return true;}virtual void stop(IOService *){base_stops++;}virtual void free(){}
    bool setProperty(const char *,unsigned,unsigned){return true;}bool setProperty(const char *,bool){return true;}void setName(const char *){}
    bool attach(IOService *){return !fail_attach;}void detach(IOService *){}
    virtual IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **){return kIOReturnNotReady;}
    virtual IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **){return kIOReturnNotReady;}
    void release(){free();delete this;}
};
class IOUserClient:public IOService {
public:
    bool initWithTask(task_t,void *,UInt32,OSDictionary *){return !fail_init;}
    static IOReturn clientHasPrivilege(task_t,const char *){return privilege_rc;}
    virtual IOReturn clientClose(){return kIOReturnSuccess;}
    virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *){return kIOReturnBadArgument;}
    void terminate(){terminations++;}
};
static const char kIOClientPrivilegeAdministrator[]="administrator";
#define OSDeclareDefaultStructors(c)
#define OSDefineMetaClassAndStructors(c,p)
template<class T>static T *allocate(){return fail_alloc?nullptr:new T;}
#define OSTypeAlloc(c) allocate<c>()
#define ELDEN_HAPTICS_HAS_LIMITS 0
#define ELDEN_HAPTICS_EFFECT_COUNT 0
extern "C" int elden_haptics_native_open(uint64_t *o,uint64_t *e){opened++;if(open_rc)return open_rc;*o=11;*e=7;return 0;}
extern "C" int elden_haptics_native_close(uint64_t o,uint64_t e){closed++;CHECK(o==11&&e==7);return 0;}
static void capture(uint64_t o,uint64_t e){called++;CHECK(o==11&&e==7);args[0]=o;args[1]=e;}
extern "C" int elden_haptics_native_submit(uint64_t o,uint64_t e,uint64_t id,uint64_t delay,uint32_t duration,uint8_t mask,uint8_t wave,uint16_t intensity){
    capture(o,e);args[2]=id;args[3]=delay;args[4]=duration;args[5]=mask;args[6]=wave;args[7]=intensity;return rc;
}
extern "C" int elden_haptics_native_submit_effect(uint64_t o,uint64_t e,uint64_t id,uint32_t effect,uint64_t delay,uint16_t intensity){
    capture(o,e);args[2]=id;args[3]=effect;args[4]=delay;args[5]=intensity;return rc;
}
extern "C" int elden_haptics_native_cancel(uint64_t o,uint64_t e,uint64_t id){capture(o,e);args[2]=id;return rc;}
extern "C" int elden_haptics_native_result(uint64_t o,uint64_t e,uint64_t id,EldenHapticsResult *out){
    capture(o,e);if(rc)return rc;*out={o,e,id,EldenHapticsComplete,0};return 0;
}
extern "C" int elden_haptics_native_effect_info(uint32_t id,uint64_t out[4]){called++;if(rc)return rc;out[0]=id;out[1]=24;out[2]=3;out[3]=24000;return 0;}
extern "C" void elden_haptics_native_info(uint64_t out[8]){called++;for(unsigned i=0;i<8;i++)out[i]=i==0?2:i;}
#include "ClientMethods.inc"
static IOReturn call(EldenHapticsClient &c,unsigned selector,IOExternalMethodArguments &a){return c.externalMethod(selector,&a,nullptr,nullptr,nullptr);}
int main(){
    IOService provider;EldenHapticsClient client;CHECK(client.start(&provider)&&opened==1);
    uint64_t input[6]={1,0,10000,3,1,32767},output[8];IOExternalMethodArguments a;
    a.scalarInput=input;a.scalarInputCount=6;CHECK(call(client,1,a)==0&&args[4]==10000&&args[7]==32767);
    unsigned before=called;input[2]=UINT64_MAX;CHECK(call(client,1,a)==kIOReturnBadArgument&&called==before);input[2]=10000;
    for(unsigned selector=0;selector<6;selector++)for(unsigned count=0;count<10;count++){
        unsigned wanted=selector==0?0:selector==1?6:selector==4?4:1;
        a={};a.scalarInputCount=count;a.scalarInput=input;a.scalarOutput=output;a.scalarOutputCount=selector==0?8:selector==3?3:selector==5?4:0;
        if(count==wanted)continue;before=called;CHECK(call(client,selector,a)==kIOReturnBadArgument&&called==before);
    }
    input[0]=9;input[1]=1;input[2]=100;input[3]=12345;a={};a.scalarInput=input;a.scalarInputCount=4;
    CHECK(call(client,4,a)==0&&args[2]==9&&args[3]==1&&args[4]==100&&args[5]==12345);
    input[1]=UINT64_MAX;before=called;CHECK(call(client,4,a)==kIOReturnBadArgument&&called==before);input[1]=1;
    a.structureInputSize=1;CHECK(call(client,4,a)==kIOReturnBadArgument);a.structureInputSize=0;a.structureOutputDescriptor=&provider;CHECK(call(client,4,a)==kIOReturnBadArgument);
    a={};a.scalarInput=input;a.scalarInputCount=1;a.scalarOutput=output;a.scalarOutputCount=3;std::memset(output,0xa5,sizeof(output));uint64_t previous[8];std::memcpy(previous,output,sizeof(output));
    rc=-16;CHECK(call(client,3,a)==kIOReturnBusy&&!std::memcmp(previous,output,sizeof(output)));rc=0;CHECK(call(client,3,a)==0&&output[0]==EldenHapticsComplete&&output[2]==9);
    for(int error:{-12,-13,-16,-19,-22,-110,-116,-125,-5}){
        rc=error;a={};a.scalarInput=input;a.scalarInputCount=1;
        int expected=error==-12?kIOReturnNoMemory:error==-13||error==-125?kIOReturnNotPermitted:error==-16?kIOReturnBusy:error==-19?kIOReturnNotReady:
                     error==-22?kIOReturnBadArgument:error==-110?kIOReturnTimeout:error==-116?kIOReturnNotFound:kIOReturnIOError;
        CHECK(call(client,2,a)==expected);
    }rc=0;
    CHECK(client.clientClose()==0&&closed==1&&terminations==1);client.free();CHECK(closed==1);before=called;CHECK(call(client,2,a)==kIOReturnBadArgument&&called==before);
    EldenHapticsService service;IOUserClient *opened_client=reinterpret_cast<IOUserClient *>(1);
    privilege_rc=1;before=opened;CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==kIOReturnNotPermitted&&!opened_client&&opened==before);
    privilege_rc=0;CHECK(service.newUserClient(nullptr,nullptr,1,&opened_client)==kIOReturnBadArgument&&!opened_client);
    fail_alloc=true;CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==kIOReturnNoMemory&&!opened_client);fail_alloc=false;
    fail_init=true;CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==kIOReturnError&&!opened_client);fail_init=false;
    fail_attach=true;CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==kIOReturnError&&!opened_client);fail_attach=false;
    open_rc=-19;CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==kIOReturnNotReady&&!opened_client);open_rc=0;
    CHECK(service.newUserClient(nullptr,nullptr,0,&opened_client)==0&&opened_client);opened_client->clientClose();opened_client->release();
    CHECK(closed==2);std::printf("PASS: %u actual native client/service method checks; privilege/shape/narrowing/owner-close/receipt preservation; IOKit peers modeled\n",checks);return 0;
}
