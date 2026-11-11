/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcTrial.h"
#include "NativeBackend.h"
#include <stdarg.h>
extern "C" void elden_usb_log_putc(char);
extern "C" int elden_dc_native_write_fence(void);
namespace {
using elden_thermal_native::SpmiEndpoint;
struct Device {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p){IOFree(p,sizeof(Device));}
    SpmiEndpoint path,usb,dc;
};
static Device *device;
static volatile unsigned lease;
/* Retained until reboot so the stop path cannot race a freed lock. */
static IOLock *writerLock;
void report(const char *fmt,...){char b[280];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);for(const char *p=b;*p;p++)elden_usb_log_putc(*p);}
int read(SpmiEndpoint &p,unsigned off,uint8_t *v){return elden_adc_spmi_read(&p.bus,(uint8_t)off,v,1);}
int open(void *){
    if(device)return 0;
    unsigned expected=0;if(!__atomic_compare_exchange_n(&lease,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -1;
    if(!__atomic_load_n(&writerLock,__ATOMIC_ACQUIRE)){
        IOLock *lock=IOLockAlloc();if(!lock){__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);return -2;}
        __atomic_store_n(&writerLock,lock,__ATOMIC_RELEASE);
    }
    device=new Device;if(!device){__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);return -2;}
    int rc=device->path.open(EldenSpmiDcPath,false);
    if(!rc)rc=device->usb.open(EldenSpmiDcUsb,false);
    if(!rc)rc=device->dc.open(EldenSpmiDcLimit,true);
    report("\nELDEN_DC_NATIVE open=%d path-apid=%u usb-apid=%u dc-apid=%u dc-owner=%u; existing EE0 grant only\n",rc,device->path.bus.apid,device->usb.bus.apid,device->dc.bus.apid,device->dc.bus.write_owner);
    return rc;
}
int check(void *,uint8_t expected){
    if(!device||(expected!=9&&expected!=10))return -1;
    uint8_t a=0,b=0,suspend=0,usb=0,dc=0;
    int rc=read(device->path,0x0b,&a);
    if(!rc)rc=read(device->usb,0x54,&suspend);
    if(!rc)rc=read(device->usb,0x52,&usb);
    if(!rc)rc=read(device->dc,0x52,&dc);
    if(!rc)rc=read(device->path,0x0b,&b);
    if(rc||a!=0xdb||b!=0xdb||suspend!=1||usb!=0){report("\nELDEN_DC_NATIVE check failed rc=%d path=%x/%x suspend=%u usb=%u dc=%u\n",rc,a,b,suspend,usb,dc);return -2;}
    if(dc==expected)return 0;
    report("\nELDEN_DC_NATIVE external value=%u expected=%u; no competing write\n",dc,expected);
    return expected==9&&dc==10?1:-3;
}
int write(void *,uint8_t value){
    if(!device||(value!=9&&value!=10))return -1;
    IOLock *lock=__atomic_load_n(&writerLock,__ATOMIC_ACQUIRE);if(!lock)return -1;
    IOLockLock(lock);
    if(!elden_dc_native_write_fence()){IOLockUnlock(lock);return 1;} /* no write queued */
    unsigned before=device->dc.bus.commands;
    int rc=elden_adc_spmi_write(&device->dc.bus,0x52,&value,1);
    bool queued=device->dc.bus.commands!=before;
    uint32_t status=device->dc.bus.last_status;uint8_t observed=0;
    if(!rc)rc=read(device->dc,0x52,&observed);
    report("\nELDEN_DC_NATIVE write raw=%u rc=%d queued=%u status=%x readback=%u apid=%u owner=%u; real SPMI completion\n",value,rc,(unsigned)queued,status,observed,device->dc.bus.apid,device->dc.bus.write_owner);
    IOLockUnlock(lock);
    if(rc||observed!=value)return -2;
    return 0;
}
}
extern "C" EldenDcTrialIo elden_dc_native_io(void){return {nullptr,open,check,write};}
extern "C" void elden_dc_native_fence_writes(void){
    IOLock *lock=__atomic_load_n(&writerLock,__ATOMIC_ACQUIRE);
    if(lock){IOLockLock(lock);IOLockUnlock(lock);}
}
extern "C" void elden_dc_native_close(void){
    if(!device)return;
    bool uncertain=device->path.bus.uncertain||device->usb.bus.uncertain||device->dc.bus.uncertain;
    report("\nELDEN_DC_NATIVE close uncertain=%u commands=%u/%u/%u\n",(unsigned)uncertain,device->path.bus.commands,device->usb.bus.commands,device->dc.bus.commands);
    if(!uncertain){delete device;device=nullptr;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}
}
