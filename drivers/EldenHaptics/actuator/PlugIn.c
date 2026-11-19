/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Same-OS Actuator HAL properties are source-derived. The native connection
 * and epoch-tagged writer are Elden-specific. No speaker/input or hardware
 * success backend is created. Default absent native/calibration refuses IO.
 */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include "Abi.h"
enum {kPlugIn=kAudioObjectPlugInObject,kActuator=2,kActStream=3};
#define kPlugInBundleID "org.unixnu.elden.actuator"
#define kActuatorUID "Actuator"
#define kActuatorModelUID "EldenDualAW86937Actuator"
static const Float64 kRate=ELDEN_ACTUATOR_RATE;
static atomic_uint gRefs=1;
static AudioServerPlugInHostRef gHost;
static pthread_mutex_t gLock=PTHREAD_MUTEX_INITIALIZER;
static io_connect_t gConnect;
static mach_vm_address_t gMemory;
static mach_vm_size_t gMemoryBytes;
static EldenActuatorRing gRing;
static unsigned gActIoUsers,gListed;
static uint64_t gZeroOrigin,gSeed;
static uint64_t gRunGeneration;
static uint32_t gClients[64];static uint8_t gClientUsed[64],gClientStarted[64];
static IONotificationPortRef gNotifications;
static dispatch_queue_t gNotificationQueue;
static io_iterator_t gMatching;
static UInt32 copyList(const AudioObjectID *list,UInt32 n,UInt32 bytes,void *out){
    UInt32 count=bytes/sizeof(AudioObjectID);if(count>n)count=n;
    for(UInt32 i=0;i<count;i++)((AudioObjectID *)out)[i]=list[i];return count*sizeof(AudioObjectID);
}
static UInt32 objects(AudioObjectID o,AudioObjectPropertyScope scope,AudioObjectID *list){
    if(o==kActuator&&scope!=kAudioObjectPropertyScopeInput){list[0]=kActStream;return 1;}return 0;
}
#define REPLY(type,value) do{if(inDataSize<sizeof(type))return kAudioHardwareBadPropertySizeError;*(type *)outData=(value);*outDataSize=sizeof(type);return 0;}while(0)
#define REPLY_STR(s) REPLY(CFStringRef,CFSTR(s))
#include "Properties.inc"
static int haveActuator(void){return gListed&&gRing.page&&elden_actuator_ring_check(&gRing)==0&&
    (__atomic_load_n(&gRing.page->state,__ATOMIC_ACQUIRE)&EldenActuatorReady);}
static int client_index(uint32_t id){for(unsigned i=0;i<64;i++)if(gClientUsed[i]&&gClients[i]==id)return (int)i;return -1;}
static kern_return_t call(unsigned selector){return gConnect?IOConnectCallScalarMethod(gConnect,selector,NULL,0,NULL,NULL):KERN_FAILURE;}
static void disconnectService(void){
    __atomic_store_n(&gRunGeneration,0u,__ATOMIC_RELEASE);gListed=0;
    if(gConnect){
        if(gMemory)IOConnectUnmapMemory64(gConnect,EldenActuatorMemory,mach_task_self(),gMemory);
        IOServiceClose(gConnect);
    }
    gConnect=0;gMemory=0;gMemoryBytes=0;memset(&gRing,0,sizeof(gRing));
}
static int connectService(void){
    if(gConnect){if(haveActuator())return 0;if(gActIoUsers)return -1;disconnectService();}
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("EldenHapticsService"));if(!service)return -1;
    kern_return_t rc=IOServiceOpen(service,mach_task_self(),1,&gConnect);IOObjectRelease(service);if(rc){gConnect=0;return -1;}
    uint64_t info[8]={0};uint32_t count=8;
    rc=IOConnectCallScalarMethod(gConnect,EldenActuatorInfo,NULL,0,info,&count);
    if(rc||count!=8||info[0]!=ELDEN_ACTUATOR_MAGIC||info[1]!=1||info[2]!=ELDEN_ACTUATOR_BYTES||info[3]!=400||!info[4]||!info[5])goto failed;
    rc=IOConnectMapMemory64(gConnect,EldenActuatorMemory,mach_task_self(),&gMemory,&gMemoryBytes,kIOMapAnywhere);
    if(rc||gMemoryBytes!=ELDEN_ACTUATOR_BYTES||elden_actuator_ring_bind(&gRing,(void *)(uintptr_t)gMemory,gMemoryBytes,info[4],info[5]))goto failed;
    gListed=1;return 0;
failed:
    disconnectService();return -1;
}
static void announce(void){
    const AudioObjectPropertyAddress changed[]={
      {kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain},
      {kAudioObjectPropertyOwnedObjects,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain}};
    if(gHost)gHost->PropertiesChanged(gHost,kPlugIn,2,changed);
}
static void serviceAppeared(void *context,io_iterator_t iterator){
    (void)context;io_service_t service;
    while((service=IOIteratorNext(iterator)))IOObjectRelease(service);
    pthread_mutex_lock(&gLock);unsigned prior=gListed;(void)connectService();
    unsigned appeared=!prior&&gListed;pthread_mutex_unlock(&gLock);if(appeared)announce();
}
static void observeService(void){
    if(gNotifications)return;
    gNotifications=IONotificationPortCreate(kIOMainPortDefault);if(!gNotifications)return;
    gNotificationQueue=dispatch_queue_create("org.unixnu.elden.actuator.discovery",DISPATCH_QUEUE_SERIAL);
    if(!gNotificationQueue){IONotificationPortDestroy(gNotifications);gNotifications=NULL;return;}
    IONotificationPortSetDispatchQueue(gNotifications,gNotificationQueue);
    // The owning native worker can publish after audiomxd's Initialize.
    // A hidden device cannot rely on StartIO to discover that late provider.
    if(IOServiceAddMatchingNotification(gNotifications,kIOFirstMatchNotification,IOServiceMatching("EldenHapticsService"),
        serviceAppeared,NULL,&gMatching)!=KERN_SUCCESS){
        IONotificationPortDestroy(gNotifications);gNotifications=NULL;dispatch_release(gNotificationQueue);gNotificationQueue=NULL;return;
    }
    serviceAppeared(NULL,gMatching); // consume the initial iterator and arm it
}
static HRESULT QueryInterface(void *,REFIID,LPVOID *);
static ULONG AddRef(void *p){(void)p;return atomic_fetch_add(&gRefs,1)+1;}
static ULONG Release(void *p){(void)p;unsigned current=atomic_load(&gRefs);while(current&&!atomic_compare_exchange_weak(&gRefs,&current,current-1)){}return current?current-1:0;}
static OSStatus Initialize(AudioServerPlugInDriverRef d,AudioServerPlugInHostRef host){(void)d;gHost=host;pthread_mutex_lock(&gLock);(void)connectService();pthread_mutex_unlock(&gLock);observeService();return 0;}
static OSStatus CreateDevice(AudioServerPlugInDriverRef d,CFDictionaryRef desc,const AudioServerPlugInClientInfo *c,AudioObjectID *o){(void)d;(void)desc;(void)c;(void)o;return kAudioHardwareUnsupportedOperationError;}
static OSStatus DestroyDevice(AudioServerPlugInDriverRef d,AudioObjectID o){(void)d;(void)o;return kAudioHardwareUnsupportedOperationError;}
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef d,AudioObjectID o,const AudioServerPlugInClientInfo *c){
    (void)d;if(o!=kActuator||!c)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gLock);int at=client_index(c->mClientID);
    if(at<0)for(unsigned i=0;i<64;i++)if(!gClientUsed[i]){gClientUsed[i]=1;gClients[i]=c->mClientID;at=(int)i;break;}
    pthread_mutex_unlock(&gLock);return at<0?kAudioHardwareIllegalOperationError:0;
}
static OSStatus StopIO(AudioServerPlugInDriverRef,AudioObjectID,UInt32);
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef d,AudioObjectID o,const AudioServerPlugInClientInfo *c){
    (void)d;
    if(o!=kActuator||!c)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gLock);int at=client_index(c->mClientID);OSStatus rc=0;
    if(at>=0){
        gClientUsed[at]=0;
        if(gClientStarted[at]){gClientStarted[at]=0;if(gActIoUsers)gActIoUsers--;
            if(!gActIoUsers){__atomic_store_n(&gRunGeneration,0u,__ATOMIC_RELEASE);if(call(EldenActuatorStop))rc=kAudioHardwareNotRunningError;}}
    }
    pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d,AudioObjectID o,UInt64 a,void *v){(void)d;(void)o;(void)a;(void)v;return 0;}
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d,AudioObjectID o,UInt64 a,void *v){(void)d;(void)o;(void)a;(void)v;return 0;}
static OSStatus property(AudioObjectID o,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 inDataSize,UInt32 *outDataSize,void *outData){
    if(!a||!outDataSize||(!outData&&inDataSize))return kAudioHardwareBadPropertySizeError;
    *outDataSize=0;AudioObjectPropertySelector s=a->mSelector;
    if(o==kPlugIn){
        if(s==kAudioObjectPropertyBaseClass){REPLY(AudioClassID,kAudioObjectClassID);}
        if(s==kAudioObjectPropertyClass){REPLY(AudioClassID,kAudioPlugInClassID);}
        if(s==kAudioObjectPropertyOwner){REPLY(AudioObjectID,kAudioObjectUnknown);}
        if(s==kAudioObjectPropertyManufacturer){REPLY_STR("UniXNU");}
        if(s==kAudioPlugInPropertyResourceBundle){REPLY_STR("");}
        if(s==kAudioObjectPropertyOwnedObjects||s==kAudioPlugInPropertyDeviceList){if(haveActuator()){REPLY(AudioObjectID,kActuator);}return 0;}
        if(s==kAudioPlugInPropertyTranslateUIDToDevice){
            CFStringRef uid=q&&qs==sizeof(CFStringRef)?*(const CFStringRef *)q:NULL;
            int found=uid&&CFGetTypeID(uid)==CFStringGetTypeID()&&CFEqual(uid,CFSTR(kActuatorUID))&&haveActuator();REPLY(AudioObjectID,found?kActuator:kAudioObjectUnknown);
        }
        if(s==kAudioObjectPropertyCustomPropertyInfoList||s==kAudioPlugInPropertyBoxList||s==kAudioPlugInPropertyClockDeviceList)return 0;
    }else if((o==kActuator||o==kActStream)&&haveActuator()){
        if(s==kAudioObjectPropertyOwnedObjects||s==kAudioDevicePropertyStreams){
            if(o==kActuator&&a->mScope!=kAudioObjectPropertyScopeInput){REPLY(AudioObjectID,kActStream);}return 0;
        }
        if(s=='slay'){ // kAudioDevicePropertyStreamConfiguration in AudioHardware.h
            if(o!=kActuator)return kAudioHardwareUnknownPropertyError;
            UInt32 channels=a->mScope==kAudioObjectPropertyScopeInput?0:1;
            size_t bytes=offsetof(AudioBufferList,mBuffers)+(channels?sizeof(AudioBuffer):0);
            if(inDataSize<bytes)return kAudioHardwareBadPropertySizeError;
            AudioBufferList *list=outData;memset(list,0,bytes);list->mNumberBuffers=channels;if(channels)list->mBuffers[0].mNumberChannels=1;*outDataSize=(UInt32)bytes;return 0;
        }
        if(s==kAudioObjectPropertyCustomPropertyInfoList||s=='taps'){*outDataSize=0;return 0;}
        return actuatorData(o,a,inDataSize,outDataSize,outData);
    }else return kAudioHardwareBadObjectError;
    return kAudioHardwareUnknownPropertyError;
}
static OSStatus GetPropertyData(AudioServerPlugInDriverRef d,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,UInt32 *written,void *out){
    (void)d;(void)pid;pthread_mutex_lock(&gLock);OSStatus rc=property(o,a,qs,q,size,written,out);pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size){
    _Alignas(8) uint8_t scratch[512];if(!size)return kAudioHardwareBadPropertySizeError;return GetPropertyData(d,o,pid,a,qs,q,sizeof(scratch),size,scratch);
}
static Boolean HasProperty(AudioServerPlugInDriverRef d,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a){UInt32 bytes=0;return GetPropertyDataSize(d,o,pid,a,0,NULL,&bytes)==0;}
static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,Boolean *out){
    if(!a||!out)return kAudioHardwareBadPropertySizeError;
    if(!HasProperty(d,o,pid,a))return kAudioHardwareUnknownPropertyError;
    *out=(o==kActuator&&a->mSelector==kAudioDevicePropertyNominalSampleRate)||
         (o==kActStream&&(a->mSelector==kAudioStreamPropertyPhysicalFormat||a->mSelector==kAudioStreamPropertyVirtualFormat));return 0;
}
static OSStatus setProperty(AudioObjectID o,const AudioObjectPropertyAddress *a,UInt32 size,const void *data){
    if(!a||!data)return kAudioHardwareBadPropertySizeError;
    if(!haveActuator()||(o!=kActuator&&o!=kActStream))return kAudioHardwareBadObjectError;
    if(o==kActuator&&a->mSelector==kAudioDevicePropertyNominalSampleRate)return size==sizeof(Float64)&&*(const Float64 *)data==kRate?0:kAudioDeviceUnsupportedFormatError;
    if(o==kActStream&&(a->mSelector==kAudioStreamPropertyPhysicalFormat||a->mSelector==kAudioStreamPropertyVirtualFormat)){
        if(size!=sizeof(AudioStreamBasicDescription))return kAudioHardwareBadPropertySizeError;
        AudioStreamBasicDescription f=actuatorFormat();return memcmp(&f,data,sizeof(f))?kAudioDeviceUnsupportedFormatError:0;
    }
    return kAudioHardwareUnknownPropertyError;
}
static OSStatus SetPropertyData(AudioServerPlugInDriverRef d,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,const void *data){
    (void)d;(void)pid;(void)qs;(void)q;
    pthread_mutex_lock(&gLock);OSStatus rc=setProperty(o,a,size,data);pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus StartIO(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client){
    (void)d;if(o!=kActuator)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gLock);unsigned old=gListed;(void)connectService();int at=client_index(client);OSStatus rc=0;
    if(at<0||!haveActuator())rc=kAudioHardwareNotRunningError;
    else if(!gClientStarted[at]){
        if(!gActIoUsers&&call(EldenActuatorStart))rc=kAudioHardwareNotRunningError;
        if(!rc){if(!gActIoUsers){gZeroOrigin=elden_actuator_frame(&gRing,mach_absolute_time());gSeed++;
            gRing.generation=__atomic_load_n(&gRing.page->generation,__ATOMIC_ACQUIRE);
            __atomic_store_n(&gRunGeneration,gRing.generation,__ATOMIC_RELEASE);}
            gClientStarted[at]=1;gActIoUsers++;}
    }
    unsigned appeared=!old&&gListed;pthread_mutex_unlock(&gLock);if(appeared)announce();return rc;
}
static OSStatus StopIO(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client){
    (void)d;if(o!=kActuator)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gLock);int at=client_index(client);OSStatus rc=0;
    if(at>=0&&gClientStarted[at]){gClientStarted[at]=0;if(gActIoUsers)gActIoUsers--;if(!gActIoUsers){
        __atomic_store_n(&gRunGeneration,0u,__ATOMIC_RELEASE);if(call(EldenActuatorStop))rc=kAudioHardwareNotRunningError;}}
    pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client,Float64 *sample,UInt64 *host,UInt64 *seed){
    (void)d;(void)client;if(o!=kActuator||!sample||!host||!seed)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gLock);OSStatus rc=0;
    if(!haveActuator()||!gActIoUsers)rc=kAudioHardwareNotRunningError;
    else{uint64_t now=elden_actuator_frame(&gRing,mach_absolute_time()),wraps=now>gZeroOrigin?(now-gZeroOrigin)/16384:0;
        uint64_t frame=gZeroOrigin+wraps*16384;*sample=(Float64)frame;*host=gRing.anchor+frame*400;*seed=gSeed;}
    pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client,UInt32 op,Boolean *will,Boolean *in_place){
    (void)d;(void)client;if(o!=kActuator||!will||!in_place)return kAudioHardwareBadObjectError;*will=op==kAudioServerPlugInIOOperationWriteMix;*in_place=true;return 0;
}
static OSStatus BeginIOOperation(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client,UInt32 op,UInt32 frames,const AudioServerPlugInIOCycleInfo *info){
    (void)d;(void)client;(void)op;(void)frames;(void)info;return o==kActuator?0:kAudioHardwareBadObjectError;
}
static OSStatus DoIOOperation(AudioServerPlugInDriverRef d,AudioObjectID o,AudioObjectID stream,UInt32 client,UInt32 op,UInt32 frames,
                             const AudioServerPlugInIOCycleInfo *info,void *main,void *secondary){
    (void)d;(void)secondary;
    if(o!=kActuator||stream!=kActStream||op!=kAudioServerPlugInIOOperationWriteMix||!info||!main||!frames||frames>2048)return kAudioHardwareIllegalOperationError;
    Float64 stamp=info->mOutputTime.mSampleTime;
    uint64_t generation=__atomic_load_n(&gRunGeneration,__ATOMIC_ACQUIRE);
    if(!generation)return kAudioHardwareNotRunningError;
    if(!isfinite(stamp)||stamp<0||stamp>9007199254740991.0||floor(stamp)!=stamp)return kAudioHardwareIllegalOperationError;
    int16_t pcm[2048];const Float32 *input=main;
    for(unsigned i=0;i<frames;i++){Float32 f=input[i];if(!isfinite(f))return kAudioHardwareIllegalOperationError;
        if(f>1)f=1;if(f<-1)f=-1;pcm[i]=(int16_t)lroundf(f*32767);}
    pthread_mutex_lock(&gLock);OSStatus rc=0;
    int at=client_index(client);
    if(!haveActuator()||!gActIoUsers||at<0||!gClientStarted[at]||generation!=__atomic_load_n(&gRunGeneration,__ATOMIC_ACQUIRE))rc=kAudioHardwareNotRunningError;
    else if(elden_actuator_ring_write(&gRing,(uint64_t)stamp,pcm,frames))rc=kAudioHardwareIllegalOperationError;
    pthread_mutex_unlock(&gLock);return rc;
}
static OSStatus EndIOOperation(AudioServerPlugInDriverRef d,AudioObjectID o,UInt32 client,UInt32 op,UInt32 frames,const AudioServerPlugInIOCycleInfo *info){(void)d;(void)o;(void)client;(void)op;(void)frames;(void)info;return 0;}
static AudioServerPlugInDriverInterface gInterface={NULL,QueryInterface,AddRef,Release,Initialize,CreateDevice,DestroyDevice,AddDeviceClient,RemoveDeviceClient,
    PerformDeviceConfigurationChange,AbortDeviceConfigurationChange,HasProperty,IsPropertySettable,GetPropertyDataSize,GetPropertyData,SetPropertyData,
    StartIO,StopIO,GetZeroTimeStamp,WillDoIOOperation,BeginIOOperation,DoIOOperation,EndIOOperation};
static AudioServerPlugInDriverInterface *gInterfacePtr=&gInterface;
static HRESULT QueryInterface(void *self,REFIID uuid,LPVOID *out){
    if(!out)return E_NOINTERFACE;*out=NULL;CFUUIDRef requested=CFUUIDCreateFromUUIDBytes(NULL,uuid);HRESULT rc=E_NOINTERFACE;
    if(requested&&(CFEqual(requested,IUnknownUUID)||CFEqual(requested,kAudioServerPlugInDriverInterfaceUUID))){AddRef(self);*out=&gInterfacePtr;rc=S_OK;}
    if(requested)CFRelease(requested);return rc;
}
__attribute__((visibility("default"))) void *EldenActuator_Create(CFAllocatorRef allocator,CFUUIDRef type){(void)allocator;return CFEqual(type,kAudioServerPlugInTypeUUID)?&gInterfacePtr:NULL;}
