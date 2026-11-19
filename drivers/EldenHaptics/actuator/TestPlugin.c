/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual HAL factory/interface/property/IO functions and ring. CoreFoundation
 * and CoreAudio ABI headers are real; IOKit connections/time are explicit peers.
 * This does not load the plugin into audiomxd or touch any hardware. */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <dispatch/dispatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Abi.h"
static unsigned checks,opens,closes,starts,stops,unmaps,notifications,iterator_pending;
static int available,deny_open,bad_info,bad_map,deny_stop;
static uint64_t peer_host=192000000;
static IOServiceMatchingCallback registered_callback;static void *registered_context;
typedef struct {void *memory;EldenActuatorRing kernel;} PeerConnection;
static PeerConnection peers[16];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static CFMutableDictionaryRef peer_match(const char *name){
    CHECK(!strcmp(name,"EldenHapticsService"));
    return CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
}
static io_service_t peer_get(mach_port_t port,CFDictionaryRef match){(void)port;CFRelease(match);return available?42:0;}
static kern_return_t peer_release(io_object_t object){CHECK(object==42);return 0;}
static kern_return_t peer_open(io_service_t service,task_port_t task,uint32_t type,io_connect_t *out){
    (void)task;CHECK(service==42&&type==1);if(deny_open)return KERN_FAILURE;
    CHECK(opens<16);*out=++opens;PeerConnection *p=&peers[*out-1];p->memory=calloc(1,ELDEN_ACTUATOR_BYTES);CHECK(p->memory);
    CHECK(!elden_actuator_ring_init(&p->kernel,p->memory,ELDEN_ACTUATOR_BYTES,peer_host,*out,1));p->kernel.page->state=EldenActuatorReady;return 0;
}
static kern_return_t peer_close(io_connect_t c){CHECK(c&&c<=opens);closes++;return 0;}
static kern_return_t peer_call(mach_port_t c,uint32_t selector,const uint64_t *input,uint32_t inputs,uint64_t *output,uint32_t *count){
    CHECK(c&&c<=opens&&!input&&!inputs);EldenActuatorRing *r=&peers[c-1].kernel;
    if(selector==EldenActuatorInfo){CHECK(output&&count&&*count==8);
        uint64_t data[]={ELDEN_ACTUATOR_MAGIC,1,ELDEN_ACTUATOR_BYTES,400,c,1,r->page->state,r->generation};
        memcpy(output,data,sizeof(data));if(bad_info)output[3]=401;return 0;
    }
    CHECK(!output&&!count);
    if(selector==EldenActuatorStart){starts++;CHECK(!elden_actuator_ring_new_run(r,elden_actuator_frame(r,peer_host)&~UINT64_C(1)));
        r->page->state=EldenActuatorReady|EldenActuatorRequested;return 0;}
    CHECK(selector==EldenActuatorStop);stops++;r->page->state=deny_stop?EldenActuatorHeld:EldenActuatorReady|EldenActuatorStopped;
    return deny_stop?KERN_FAILURE:0;
}
static kern_return_t peer_map(io_connect_t c,uint32_t type,task_port_t task,mach_vm_address_t *address,mach_vm_size_t *size,IOOptionBits options){
    (void)task;CHECK(c&&c<=opens&&type==EldenActuatorMemory&&options==kIOMapAnywhere);
    *address=(uintptr_t)peers[c-1].memory;*size=ELDEN_ACTUATOR_BYTES+(bad_map?8:0);return 0;
}
static kern_return_t peer_unmap(io_connect_t c,uint32_t type,task_port_t task,mach_vm_address_t address){
    (void)task;CHECK(type==EldenActuatorMemory&&address==(uintptr_t)peers[c-1].memory);unmaps++;return 0;
}
static IONotificationPortRef peer_port(mach_port_t port){(void)port;return (IONotificationPortRef)(uintptr_t)1;}
static void peer_destroy_port(IONotificationPortRef port){CHECK(port==(IONotificationPortRef)(uintptr_t)1);}
static void peer_queue(IONotificationPortRef port,dispatch_queue_t q){CHECK(port==(IONotificationPortRef)(uintptr_t)1&&q);}
static kern_return_t peer_notification(IONotificationPortRef port,const io_name_t type,CFDictionaryRef match,
    IOServiceMatchingCallback callback,void *context,io_iterator_t *iterator){
    CHECK(port==(IONotificationPortRef)(uintptr_t)1&&!strcmp(type,kIOFirstMatchNotification));CFRelease(match);
    registered_callback=callback;registered_context=context;*iterator=77;iterator_pending=available;return 0;
}
static io_object_t peer_iterator(io_iterator_t iterator){CHECK(iterator==77);if(iterator_pending){iterator_pending=0;return 42;}return 0;}
static uint64_t peer_time(void){return peer_host;}
#define IOServiceMatching peer_match
#define IOServiceGetMatchingService peer_get
#define IOObjectRelease peer_release
#define IOServiceOpen peer_open
#define IOServiceClose peer_close
#define IOConnectCallScalarMethod peer_call
#define IOConnectMapMemory64 peer_map
#define IOConnectUnmapMemory64 peer_unmap
#define IONotificationPortCreate peer_port
#define IONotificationPortDestroy peer_destroy_port
#define IONotificationPortSetDispatchQueue peer_queue
#define IOServiceAddMatchingNotification peer_notification
#define IOIteratorNext peer_iterator
#define mach_absolute_time peer_time
#include "PlugIn.c"
static OSStatus changed(AudioServerPlugInHostRef host,AudioObjectID object,UInt32 count,const AudioObjectPropertyAddress *addresses){
    CHECK(host&&object==kPlugIn&&count==2&&addresses[0].mSelector==kAudioPlugInPropertyDeviceList);notifications++;return 0;
}
static OSStatus get(AudioServerPlugInDriverRef driver,unsigned object,unsigned selector,unsigned scope,void *out,unsigned size,unsigned *written){
    AudioObjectPropertyAddress a={selector,scope,kAudioObjectPropertyElementMain};return (*driver)->GetPropertyData(driver,object,0,&a,0,NULL,size,written,out);
}
int main(void){
    AudioServerPlugInDriverRef driver=EldenActuator_Create(NULL,kAudioServerPlugInTypeUUID);CHECK(driver&&!*((uintptr_t *)(*driver)));
    CHECK(!EldenActuator_Create(NULL,IUnknownUUID));
    void *queried=NULL;CHECK((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID),&queried)==S_OK&&queried==driver);
    CHECK((*driver)->Release(driver)==1);
    AudioServerPlugInHostInterface host={0};host.PropertiesChanged=changed;CHECK(!(*driver)->Initialize(driver,&host));
    UInt32 size=9;AudioObjectID object=99;
    CHECK(!get(driver,kPlugIn,kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,&object,sizeof(object),&size)&&size==0&&object==99);
    CHECK(registered_callback&&!opens);available=1;iterator_pending=1;registered_callback(registered_context,77);
    CHECK(opens==1&&notifications==1);
    CHECK(!get(driver,kPlugIn,kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,&object,sizeof(object),&size)&&object==kActuator);
    UInt32 value=99;
    CHECK(!get(driver,kActuator,kAudioDevicePropertyDeviceCanBeDefaultDevice,kAudioObjectPropertyScopeOutput,&value,sizeof(value),&size)&&!value);
    CHECK(!get(driver,kActuator,kAudioDevicePropertyDeviceCanBeDefaultSystemDevice,kAudioObjectPropertyScopeOutput,&value,sizeof(value),&size)&&!value);
    CHECK(!get(driver,kActuator,kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput,&object,sizeof(object),&size)&&!size);
    CHECK(!get(driver,kActStream,kAudioStreamPropertyDirection,kAudioObjectPropertyScopeGlobal,&value,sizeof(value),&size)&&value==0);
    CFStringRef uid=NULL;CHECK(!get(driver,kActuator,kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,&uid,sizeof(uid),&size)&&CFEqual(uid,CFSTR("Actuator")));
    AudioStreamBasicDescription format;
    CHECK(!get(driver,kActStream,kAudioStreamPropertyPhysicalFormat,kAudioObjectPropertyScopeGlobal,&format,sizeof(format),&size));
    CHECK(format.mSampleRate==48000&&format.mChannelsPerFrame==1&&format.mBytesPerFrame==4&&format.mBitsPerChannel==32);
    AudioObjectPropertyAddress rate={kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,0};Float64 wrong_rate=44100;
    CHECK((*driver)->SetPropertyData(driver,kActuator,0,&rate,0,NULL,sizeof(wrong_rate),&wrong_rate)==kAudioDeviceUnsupportedFormatError);
    AudioObjectPropertyAddress active={kAudioStreamPropertyIsActive,kAudioObjectPropertyScopeGlobal,0};Boolean settable=true;
    CHECK(!(*driver)->IsPropertySettable(driver,kActStream,0,&active,&settable)&&!settable);
    AudioServerPlugInClientInfo c1={0},c2={0};c1.mClientID=1;c2.mClientID=2;
    CHECK(!(*driver)->AddDeviceClient(driver,kActuator,&c1)&&!(*driver)->AddDeviceClient(driver,kActuator,&c2));
    CHECK(!(*driver)->StartIO(driver,kActuator,1)&&!(*driver)->StartIO(driver,kActuator,1)&&starts==1);
    CHECK(!(*driver)->StartIO(driver,kActuator,2)&&starts==1&&gActIoUsers==2);
    Float64 sample;UInt64 time,seed;CHECK(!(*driver)->GetZeroTimeStamp(driver,kActuator,1,&sample,&time,&seed)&&seed==1&&time==peer_host);
    Boolean will=false,inplace=false;CHECK(!(*driver)->WillDoIOOperation(driver,kActuator,1,kAudioServerPlugInIOOperationWriteMix,&will,&inplace)&&will&&inplace);
    CHECK(!(*driver)->WillDoIOOperation(driver,kActuator,1,kAudioServerPlugInIOOperationReadInput,&will,&inplace)&&!will);
    AudioServerPlugInIOCycleInfo cycle={0};cycle.mOutputTime.mSampleTime=128;Float32 floats[]={1,-1,0.5f,-0.5f};
    CHECK(!(*driver)->DoIOOperation(driver,kActuator,kActStream,1,kAudioServerPlugInIOOperationWriteMix,4,&cycle,floats,NULL));
    int16_t pcm=0;CHECK(!elden_actuator_ring_read(&peers[0].kernel,128,&pcm)&&pcm==32767);
    CHECK(!elden_actuator_ring_read(&peers[0].kernel,129,&pcm)&&pcm==-32767);
    uint64_t writes=peers[0].kernel.page->writes;floats[0]=NAN;
    CHECK((*driver)->DoIOOperation(driver,kActuator,kActStream,1,kAudioServerPlugInIOOperationWriteMix,4,&cycle,floats,NULL)==kAudioHardwareIllegalOperationError&&writes==peers[0].kernel.page->writes);floats[0]=1;
    CHECK((*driver)->DoIOOperation(driver,kActuator,kActStream,9,kAudioServerPlugInIOOperationWriteMix,4,&cycle,floats,NULL)==kAudioHardwareNotRunningError);
    cycle.mOutputTime.mSampleTime=1.5;
    CHECK((*driver)->DoIOOperation(driver,kActuator,kActStream,1,kAudioServerPlugInIOOperationWriteMix,4,&cycle,floats,NULL)==kAudioHardwareIllegalOperationError);
    CHECK(!(*driver)->StopIO(driver,kActuator,1)&&!stops&&gActIoUsers==1);
    CHECK(!(*driver)->RemoveDeviceClient(driver,kActuator,&c2)&&stops==1&&!gActIoUsers);
    CHECK(!(*driver)->StartIO(driver,kActuator,1)&&starts==2&&gRing.generation==3);
    cycle.mOutputTime.mSampleTime=128;CHECK(elden_actuator_ring_read(&peers[0].kernel,128,&pcm)==-61); // previous run is not replayed
    CHECK(!(*driver)->DoIOOperation(driver,kActuator,kActStream,1,kAudioServerPlugInIOOperationWriteMix,4,&cycle,floats,NULL));
    deny_stop=1;CHECK((*driver)->StopIO(driver,kActuator,1)==kAudioHardwareNotRunningError);deny_stop=0;
    CHECK(!(*driver)->StartIO(driver,kActuator,1)&&opens==2&&closes==1&&unmaps==1);CHECK(!(*driver)->StopIO(driver,kActuator,1));
    // Failed info/map cannot expose an Actuator or leave a user mapping active.
    disconnectService();bad_info=1;CHECK((*driver)->StartIO(driver,kActuator,1)==kAudioHardwareNotRunningError&&!gListed);bad_info=0;
    bad_map=1;CHECK((*driver)->StartIO(driver,kActuator,1)==kAudioHardwareNotRunningError&&!gListed);bad_map=0;
    deny_open=1;CHECK((*driver)->StartIO(driver,kActuator,1)==kAudioHardwareNotRunningError&&!gListed);deny_open=0;
    CHECK(!(*driver)->StartIO(driver,kActuator,1)&&notifications==2);CHECK(!(*driver)->StopIO(driver,kActuator,1));
    disconnectService();if(gNotifications)peer_destroy_port(gNotifications);if(gNotificationQueue)dispatch_release(gNotificationQueue);
    for(unsigned i=0;i<opens;i++)free(peers[i].memory);
    printf("PASS: %u actual HAL interface/ring checks; factory/late discovery/output-only/clients/48k mix/generation/format/refusal/reconnect; IOKit and time modeled\n",checks);return 0;
}
