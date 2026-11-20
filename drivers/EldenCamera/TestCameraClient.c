/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual OS17 ticket client: IOKit callbacks explicitly modeled. */
#include "CameraClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t io_service_t;typedef uint32_t io_connect_t;typedef int kern_return_t;
typedef uint64_t mach_vm_address_t,mach_vm_size_t;
enum{kIOMainPortDefault=0,kIOReturnBusy=1,kIOReturnBadArgument=2,kIOMapAnywhere=1,kIOMapReadOnly=2};
static unsigned checks,unmapped,released,closed,converted;static int map_error,unmap_error,convert_error;
static uint64_t metadata[12]={13,1,0,1000,5104u*3072,4080,3072,5104,1,90,0,1};
static uint8_t *raw;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"client line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static void *IOServiceMatching(const char *n){CHECK(!strcmp(n,"EldenCameraRear"));return (void *)1;}
static io_service_t IOServiceGetMatchingService(unsigned p,void *m){CHECK(!p&&m==(void *)1);return 1;}
static unsigned mach_task_self(void){return 7;}
static int IOServiceOpen(unsigned service,unsigned task,unsigned type,io_connect_t *out){CHECK(service==1&&task==7&&!type);*out=9;return 0;}
static void IOObjectRelease(unsigned service){CHECK(service==1);}
static int IOServiceClose(unsigned connection){CHECK(connection==9);closed++;return 0;}
static int IOConnectCallScalarMethod(unsigned conn,unsigned op,const uint64_t *in,unsigned ni,uint64_t *out,uint32_t *no){CHECK(conn==9);
    if(op==0){CHECK(!in&&!ni&&out&&no&&*no==8);uint64_t v[8]={1,0,4080,3072,5104,10,1,90};memcpy(out,v,sizeof(v));}
    else if(op==1){CHECK(!in&&!ni&&out&&no&&*no==12);memcpy(out,metadata,sizeof(metadata));}
    else{CHECK(op==2&&in&&ni==1&&in[0]==13&&!out&&!no);released++;CHECK(unmapped||map_error||metadata[1]>3||metadata[5]!=4080);}return 0;
}
static int IOConnectMapMemory64(unsigned conn,unsigned type,unsigned task,mach_vm_address_t *address,mach_vm_size_t *size,unsigned options){
    CHECK(conn==9&&type==1&&task==7&&options==(kIOMapAnywhere|kIOMapReadOnly));if(map_error)return -1;*address=(uintptr_t)raw;*size=5104u*3072;return 0;}
static int IOConnectUnmapMemory64(unsigned conn,unsigned type,unsigned task,mach_vm_address_t address){CHECK(conn==9&&type==1&&task==7&&address==(uintptr_t)raw);unmapped++;return unmap_error;}
int elden_raw10_bgra(const EldenRaw10Config *c,const uint8_t *p,size_t n,uint8_t *o,size_t bytes,size_t stride){CHECK(c->rotation==90&&c->bayer==1&&p==raw&&n==5104u*3072&&o&&bytes&&stride);converted++;return convert_error;}
#include "CameraClientMethods.inc"
int main(int argc,char **argv){CHECK(argc==2);raw=malloc(5104u*3072);CHECK(raw);EldenCameraUserClient client={0};CHECK(!elden_camera_client_open(&client,0));
    if(!strcmp(argv[1],"unmap"))unmap_error=-1;if(!strcmp(argv[1],"map"))map_error=-1;if(!strcmp(argv[1],"convert"))convert_error=-22;
    if(!strcmp(argv[1],"metadata"))metadata[5]=0;
    uint8_t pixels[32]={0};uint64_t time=0;int rc=elden_camera_client_frame(&client,pixels,sizeof(pixels),16,&time);
    if(map_error)CHECK(rc==-19&&!converted&&!unmapped);
    else if(unmap_error)CHECK(rc==-5&&closed==1&&!released&&!client.connection);
    else if(convert_error)CHECK(rc==-22&&unmapped==1&&released==1&&!time);
    else if(metadata[5]==0)CHECK(rc==-116&&!converted&&!unmapped&&released==1);
    else {CHECK(!rc&&time==1000&&client.seen&&client.last_sequence==0&&converted==1&&unmapped==1&&released==1);}
    if(!map_error&&strcmp(argv[1],"metadata"))CHECK(converted==1);
    elden_camera_client_close(&client);free(raw);
    printf("PASS actual OS17 ticket client %s: %u checks; IOKit/map/converter peers modeled\n",argv[1],checks);return 0;
}
