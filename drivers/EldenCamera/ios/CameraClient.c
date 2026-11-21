/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* OS17 T970 IOService transport methodology; real Elden ticket lifetime is
 * explicit and uses READONLY cached snapshots instead of its DMA carve-out. */
#include "CameraClient.h"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <string.h>
int elden_camera_client_open(EldenCameraUserClient *c,unsigned sensor){
    if(!c||c->connection||sensor>1)return -22;
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching(sensor?"EldenCameraFront":"EldenCameraRear"));if(!service)return -19;
    io_connect_t connection=0;kern_return_t rc=IOServiceOpen(service,mach_task_self(),0,&connection);IOObjectRelease(service);if(rc)return -19;
    uint64_t info[8]={};uint32_t count=8;rc=IOConnectCallScalarMethod(connection,EldenCameraServiceInfo,NULL,0,info,&count);
    if(rc||count!=8||info[0]!=1||info[1]!=sensor||info[2]!=(sensor?3264u:4080u)||info[3]!=(sensor?2448u:3072u)||
       info[4]!=(sensor?4080u:5104u)||info[5]!=10||info[6]!=(sensor?EldenRawRggb:EldenRawGrbg)||info[7]!=(sensor?270u:90u)){IOServiceClose(connection);return -116;}
    c->sensor=sensor;c->connection=connection;c->last_sequence=0;c->seen=0;
    c->conversion=(EldenRaw10Config){(uint32_t)info[2],(uint32_t)info[3],(uint32_t)info[4],(uint32_t)info[6],(uint32_t)info[7],0,
        720,960,0,1023,{65536,65536,65536}}; // explicit uncalibrated RAW preview; no inferred hardware ISP
    return 0;
}
int elden_camera_client_frame(EldenCameraUserClient *c,uint8_t *pixels,size_t bytes,size_t stride,uint64_t *when){
    if(!c||!c->connection||!pixels||!when)return -22;uint64_t frame[12]={};uint32_t count=12;
    kern_return_t rc=IOConnectCallScalarMethod(c->connection,EldenCameraServiceAcquire,NULL,0,frame,&count);
    if(rc==kIOReturnBusy)return -11;if(rc)return -19;
    int error=0;mach_vm_address_t address=0;mach_vm_size_t size=0;
    if(count!=12||!frame[0]||frame[1]>=4||(c->seen&&frame[2]<=c->last_sequence)||!frame[3]||frame[4]!=(uint64_t)c->conversion.stride*c->conversion.height||
       frame[5]!=c->conversion.width||frame[6]!=c->conversion.height||frame[7]!=c->conversion.stride||frame[8]!=c->conversion.bayer||
       frame[9]!=c->conversion.rotation||frame[10]!=c->sensor||frame[11]!=1)error=-116;
    if(!error){rc=IOConnectMapMemory64(c->connection,(uint32_t)frame[1],mach_task_self(),&address,&size,kIOMapAnywhere|kIOMapReadOnly);
        if(rc||!address||size<frame[4]||size>(32u<<20))error=-19;}
    if(!error)error=elden_raw10_bgra(&c->conversion,(const uint8_t *)(uintptr_t)address,(size_t)frame[4],pixels,bytes,stride);
    if(address){rc=IOConnectUnmapMemory64(c->connection,(uint32_t)frame[1],mach_task_self(),address);if(rc&&!error)error=-5;}
    /* An unmap failure cannot be interpreted as consumer completion. Close
     * withdraws the one process-owned service, retaining storage to reset. */
    if(address&&rc){elden_camera_client_close(c);return error?error:-5;}
    uint64_t ticket=frame[0];rc=ticket?IOConnectCallScalarMethod(c->connection,EldenCameraServiceRelease,&ticket,1,NULL,NULL):kIOReturnBadArgument;
    if(rc&&!error)error=-5;if(!error){c->last_sequence=frame[2];c->seen=1;*when=frame[3];}return error;
}
void elden_camera_client_close(EldenCameraUserClient *c){if(!c)return;
    if(c->connection){IOServiceClose(c->connection);c->connection=0;}c->last_sequence=0;c->seen=0;
}
