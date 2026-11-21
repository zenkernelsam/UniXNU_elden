/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native Info/Acquire/Release/memory handlers → actual frame-output
 * methods → actual user client → actual RAW10 converter. Only OS/physical
 * acquisition peers are modeled. Pixels are explicit synthetic fixtures. */
#include "CameraClient.h"
extern "C" {
int elden_camera_frame_peer_open(unsigned,EldenCameraFrameOutput **);
int elden_camera_frame_peer_event(EldenCameraFrameOutput *,unsigned);
int elden_camera_frame_peer_map(void *,uintptr_t *,uint64_t *);
void elden_camera_frame_peer_unmap(void *);
void elden_camera_frame_peer_finish(EldenCameraFrameOutput *);
}
#include "ServiceMethods.inc"
using io_service_t=uint32_t;using io_connect_t=uint32_t;using kern_return_t=int;
using mach_vm_address_t=uint64_t;using mach_vm_size_t=uint64_t;
enum{kIOMainPortDefault=0,kIOMapAnywhere=0x1};
static EldenCameraService *bridge_service;static IOUserClient *bridge_client;
static unsigned bridge_sensor,maps,unmaps;static void *mapped_memory;static uintptr_t mapped_address;
static void *IOServiceMatching(const char *name){CHECK(!strcmp(name,bridge_sensor?"EldenCameraFront":"EldenCameraRear"));return bridge_service;}
static io_service_t IOServiceGetMatchingService(unsigned port,void *service){CHECK(port==0&&service==bridge_service);return 1;}
static unsigned mach_task_self(){return 7;}
static kern_return_t IOServiceOpen(unsigned service,unsigned task,unsigned type,io_connect_t *out){
    CHECK(service==1&&task==7&&!type&&out);int rc=bridge_service->newUserClient((task_t)1,nullptr,type,&bridge_client);if(!rc)*out=9;return rc;
}
static void IOObjectRelease(unsigned service){CHECK(service==1);}
static int IOConnectCallScalarMethod(unsigned conn,unsigned op,const uint64_t *in,unsigned ni,uint64_t *out,uint32_t *no){
    CHECK(conn==9&&bridge_client);IOExternalMethodArguments args{};
    args.scalarInput=in;args.scalarInputCount=ni;args.scalarOutput=out;args.scalarOutputCount=no?*no:0;
    return bridge_client->externalMethod(op,&args,nullptr,nullptr,nullptr);
}
static int IOConnectMapMemory64(unsigned conn,unsigned type,unsigned task,mach_vm_address_t *address,mach_vm_size_t *size,unsigned options){
    CHECK(conn==9&&task==7&&address&&size&&options==(kIOMapAnywhere|kIOMapReadOnly)&&!mapped_memory);
    IOMemoryDescriptor *memory=nullptr;IOOptionBits imposed=0;int rc=bridge_client->clientMemoryForType(type,&imposed,&memory);
    if(rc)return rc;CHECK(imposed==kIOMapReadOnly&&memory);uint64_t bytes=0;uintptr_t p=0;
    rc=elden_camera_frame_peer_map(memory,&p,&bytes);if(!rc){maps++;mapped_memory=memory;mapped_address=p;*address=p;*size=bytes;}return rc;
}
static int IOConnectUnmapMemory64(unsigned conn,unsigned,unsigned task,mach_vm_address_t address){
    CHECK(conn==9&&task==7&&mapped_memory&&address==mapped_address);unmaps++;
    elden_camera_frame_peer_unmap(mapped_memory);mapped_memory=nullptr;mapped_address=0;return 0;
}
static int IOServiceClose(unsigned conn){CHECK(conn==9&&bridge_client&&!mapped_memory);
    int rc=bridge_client->clientClose();bridge_client->free();delete bridge_client;bridge_client=nullptr;return rc;
}
#define ELDEN_CAMERA_CLIENT_TEST 1
#include "CameraClientMethods.inc"
int main(int argc,char **argv){
    CHECK(argc==2&&(!strcmp(argv[1],"rear")||!strcmp(argv[1],"front")));bridge_sensor=!strcmp(argv[1],"front");
    EldenCameraFrameOutput *output=nullptr;CHECK(!elden_camera_frame_peer_open(bridge_sensor,&output));outputs[bridge_sensor]=output;
    EldenCameraService service;service.use(bridge_sensor);bridge_service=&service;CHECK(service.start(IOService::getPlatform()));
    EldenCameraUserClient client{};CHECK(!elden_camera_client_open(&client,bridge_sensor));
    CHECK(client.conversion.bayer==(bridge_sensor?EldenRawRggb:EldenRawGrbg));
    CHECK(!elden_camera_frame_peer_event(output,0));CHECK(!elden_camera_frame_peer_event(output,1));
    size_t stride=720u*4,bytes=stride*960;uint8_t *pixels=(uint8_t *)malloc(bytes);CHECK(pixels);bzero(pixels,bytes);
    uint64_t when=0;CHECK(!elden_camera_client_frame(&client,pixels,bytes,stride,&when));
    CHECK(client.last_sequence==1&&when==2000000&&maps==1&&unmaps==1);
    for(size_t i=0;i<bytes;i+=4){CHECK(pixels[i+2]>pixels[i+1]&&pixels[i+1]>pixels[i]);CHECK(pixels[i+3]==255);}
    /* Old ready snapshot must not resurface after newest frame's return. */
    CHECK(elden_camera_client_frame(&client,pixels,bytes,stride,&when)==-11&&client.last_sequence==1&&maps==1);
    CHECK(!elden_camera_frame_peer_event(output,2));CHECK(!elden_camera_client_frame(&client,pixels,bytes,stride,&when));
    CHECK(client.last_sequence==2&&when==3000000&&maps==2&&unmaps==2);
    EldenCameraOutputStatus status{};CHECK(!elden_camera_output_status(output,&status)&&status.dropped==1&&!status.held);
    elden_camera_client_close(&client);CHECK(!bridge_client);elden_camera_frame_peer_finish(output);free(pixels);
    printf("PASS camera native→ticketclient→RAW10 %s: %u checks; complete code path, OS/physical/pixels modeled\n",argv[1],checks);return 0;
}
