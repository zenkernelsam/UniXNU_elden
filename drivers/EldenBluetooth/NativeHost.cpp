// SPDX-License-Identifier: BSD-2-Clause-Patent
// Kernel transport adapter, based on pinned canonical PaganiBtHost c888efd3.
// Wire framing/flow/guards remain in canonical PBBridge, with scoped fixes.
#include "NativeHost.h"
#include "RunControl.h"
extern "C" {
#include "BtBridge.h"
#include "BtTlv.h"
#include "BtUtil.h"
}
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <sys/errno.h>
#include <sys/kern_control.h>
#include <sys/kpi_mbuf.h>
#include <sys/proc.h>
#include <sys/signal.h>
extern "C" {
void elden_usb_log_putc(char);
void psignal(proc_t,int);
}
namespace {
constexpr const char *ControlName="com.apple.uart.sk.bluetooth";
constexpr const char *ServiceName="bluetooth";
struct Host{
    PbBridge bridge={};PbBridgeIo callbacks={};EldenWcnStartupIo uart={};
    IOLock *lock=nullptr;kern_ctl_ref control=nullptr;IOService *service=nullptr;
    IORegistryEntry *chosen=nullptr,*product=nullptr;
    OSObject *old_address=nullptr,*old_le=nullptr;
    OSData *address_property=nullptr,*le_property=nullptr;
    proc_t client_proc=nullptr;
    uint32_t kick=0,room=0;bool withdrawing=false;
};
static Host *gEldenBtHost;
static void log(const char *line){for(const char *p=line;*p;p++)elden_usb_log_putc(*p);elden_usb_log_putc('\n');}
static uint64_t now_ms(void *){uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000;}
static uint64_t deadline(unsigned ms){uint64_t value;clock_interval_to_deadline(ms,kMillisecondScale,&value);return value;}
static void lock(void *p,int take){Host *h=static_cast<Host *>(p);if(take)IOLockLock(h->lock);else IOLockUnlock(h->lock);}
static int uart_write(void *p,const uint8_t *data,uint32_t bytes,unsigned timeout){
    Host *h=static_cast<Host *>(p);return h->uart.write(h->uart.context,data,bytes,timeout);
}
static uint32_t uart_read(void *p,uint8_t *data,uint32_t max){
    Host *h=static_cast<Host *>(p);size_t bytes=h->uart.read(h->uart.context,data,max);
    return bytes>max?max+1:(uint32_t)bytes;
}
static int put(void *p,uint32_t unit,const uint8_t *data,uint32_t bytes){
    Host *h=static_cast<Host *>(p);
    errno_t e=ctl_enqueuedata(h->control,unit,const_cast<uint8_t *>(data),bytes,0);
    return !e?0:e==ENOBUFS||e==ENOMEM?1:-1;
}
static void kick(void *p){Host *h=static_cast<Host *>(p);h->kick=1;IOLockWakeup(h->lock,&h->kick,true);}
static void wait_room(void *p,unsigned ms){Host *h=static_cast<Host *>(p);IOLockSleepDeadline(h->lock,&h->room,deadline(ms),THREAD_UNINT);}
static void wake_room(void *p){Host *h=static_cast<Host *>(p);IOLockWakeup(h->lock,&h->room,false);}
static void io_log(void *,const char *line){log(line);}
static errno_t connect(kern_ctl_ref,struct sockaddr_ctl *address,void **info){
    Host *h=gEldenBtHost;if(!h||__atomic_load_n(&h->withdrawing,__ATOMIC_ACQUIRE))return ECONNREFUSED;
    int pid=proc_selfpid();char name[32]={};proc_name(pid,name,sizeof(name));
    if(strcmp(name,"bluetoothd"))return EPERM;
    proc_t proc=proc_find(pid);if(proc==nullptr)return ESRCH;
    IOLockLock(h->lock);
    if(h->client_proc!=nullptr){IOLockUnlock(h->lock);proc_rele(proc);return EBUSY;}
    h->client_proc=proc;IOLockUnlock(h->lock);
    int rc=pb_bridge_connect(&h->bridge,address->sc_unit,pid);
    if(rc){
        IOLockLock(h->lock);if(h->client_proc==proc)h->client_proc=nullptr;IOLockUnlock(h->lock);
        proc_rele(proc);return rc==PbBridgeBusy?EBUSY:ECONNREFUSED;
    }
    *info=h;return 0;
}
static errno_t disconnect(kern_ctl_ref,u_int32_t unit,void *info){
    Host *h=static_cast<Host *>(info);if(!h)return 0;
    pb_bridge_disconnect(&h->bridge,unit);
    IOLockLock(h->lock);proc_t p=h->client_proc;h->client_proc=nullptr;IOLockUnlock(h->lock);
    if(p!=nullptr)proc_rele(p);return 0;
}
static errno_t send(kern_ctl_ref,u_int32_t unit,void *info,mbuf_t chain,int){
    Host *h=static_cast<Host *>(info);
    if(!h||h!=gEldenBtHost){mbuf_freem(chain);return ENOTCONN;}
    IOLockLock(h->lock);uint32_t dropped=h->bridge.tx_dropped;IOLockUnlock(h->lock);
    for(mbuf_t m=chain;m;m=mbuf_next(m)){
        size_t bytes=mbuf_len(m);
        if(bytes>UINT32_MAX){mbuf_freem(chain);pb_bridge_fail(&h->bridge);return EMSGSIZE;}
        if(bytes)pb_bridge_write(&h->bridge,unit,static_cast<const uint8_t *>(mbuf_data(m)),(uint32_t)bytes);
    }
    mbuf_freem(chain);
    IOLockLock(h->lock);bool failed=h->bridge.tx_dropped!=dropped||h->bridge.link==PbLinkDead;IOLockUnlock(h->lock);
    if(failed){pb_bridge_fail(&h->bridge);return EIO;}return 0;
}
static bool control(Host *h){
    kern_ctl_reg r={};strlcpy(r.ctl_name,ControlName,sizeof(r.ctl_name));
    r.ctl_flags=CTL_FLAG_REG_SOCK_STREAM;r.ctl_sendsize=8192;r.ctl_recvsize=65536;
    r.ctl_connect=connect;r.ctl_disconnect=disconnect;r.ctl_send=send;
    return ctl_register(&r,&h->control)==0;
}
static bool publish(Host *h,const uint8_t wire[6]){
    uint8_t address[6];pb_bdaddr_written(wire,address);
    h->chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    h->product=IORegistryEntry::fromPath("/product",gIODTPlane);
    if(!h->chosen||!h->product)return false;
    h->old_address=h->chosen->getProperty("mac-address-bluetooth0");
    h->old_le=h->product->getProperty("bluetooth-le");
    if(h->old_address)h->old_address->retain();if(h->old_le)h->old_le->retain();
    uint32_t one=1,baud=8000000;
    h->address_property=OSData::withBytes(address,6);h->le_property=OSData::withBytes(&one,4);
    if(!h->address_property||!h->le_property||
       !h->chosen->setProperty("mac-address-bluetooth0",h->address_property)||
       !h->product->setProperty("bluetooth-le",h->le_property))return false;
    IOService *node=new IOService,*parent=IOService::getResourceService();
    if(!node||!parent||!node->init()){if(node)node->release();return false;}
    node->setName(ServiceName);static const char name[]="bluetooth";
    bool ok=node->setProperty("name",const_cast<char *>(name),sizeof(name))&&
        node->setProperty("local-mac-address",address,6)&&
        node->setProperty("transport-encoding",&one,4)&&
        node->setProperty("transport-speed",&baud,4)&&node->attach(parent);
    if(ok&&!node->start(parent)){node->detach(parent);ok=false;}
    if(!ok){node->release();return false;}
    node->registerService();h->service=node;return true;
}
static void retract(Host *h){
    __atomic_store_n(&h->withdrawing,true,__ATOMIC_RELEASE);
    if(h->chosen&&h->chosen->getProperty("mac-address-bluetooth0")==h->address_property){
        if(h->old_address)h->chosen->setProperty("mac-address-bluetooth0",h->old_address);
        else h->chosen->removeProperty("mac-address-bluetooth0");
    }
    if(h->product&&h->product->getProperty("bluetooth-le")==h->le_property){
        if(h->old_le)h->product->setProperty("bluetooth-le",h->old_le);
        else h->product->removeProperty("bluetooth-le");
    }
    if(h->service){
        h->service->terminate(kIOServiceRequired|kIOServiceSynchronous);
        h->service->release();h->service=nullptr;
    }
}
static void finish(Host *h){
    retract(h);
    IOLockLock(h->lock);proc_t p=h->client_proc;h->client_proc=nullptr;
    int pid=h->bridge.kill_pid;IOLockUnlock(h->lock);
    char line[192];snprintf(line,sizeof(line),"\nELDEN_BT_HOST withdrawn guard=%u out=%u up=%u dropped=%u\n",
        h->bridge.guard,h->bridge.packets_out,h->bridge.frames_up,h->bridge.tx_dropped);log(line);
    if(p!=nullptr){if(proc_pid(p)==pid&&pid>0)psignal(p,SIGKILL);proc_rele(p);}
    if(h->control){
        errno_t error=ctl_deregister(h->control);
        if(!error)h->control=nullptr;
    }
    // Retain the dead callback context for queued/disconnect callbacks until reboot.
}
}
extern "C" int elden_bt_host_publish(const EldenWcnStartupIo *uart,const uint8_t address[6],
    const PbH4Rx *rx,const uint8_t *rest,size_t bytes){
    if(elden_wcn_stop_requested())return -125;
    if(gEldenBtHost||!uart||!uart->write||!uart->read||!address||!rx||bytes>512||(!rest&&bytes))return -22;
    Host *h=static_cast<Host *>(IOMallocZero(sizeof(Host)));if(!h)return -12;
    h->lock=IOLockAlloc();if(!h->lock){IOFree(h,sizeof(Host));return -12;}
    h->uart=*uart;h->callbacks={h,lock,now_ms,uart_write,uart_read,put,kick,wait_room,wake_room,io_log};
    pb_bridge_init(&h->bridge,&h->callbacks,address,0); // actual controller only
    pb_bridge_up(&h->bridge,rx,rest,(uint32_t)bytes);gEldenBtHost=h;
    if(!control(h)||!publish(h,address)){
        pb_bridge_fail(&h->bridge);finish(h);return -19;
    }
    log("\nELDEN_BT_HOST published actual H4 transport; no fabricated capabilities");return 0;
}
extern "C" void elden_bt_host_run(void){
    Host *h=gEldenBtHost;if(!h)return;
    uint64_t report_at=now_ms(h)+60000;
    for(;;){
        // Do not rely on another UART read noticing a revoked power lease:
        // client backpressure can intentionally prevent reads indefinitely.
        if(elden_wcn_stop_requested()){pb_bridge_fail(&h->bridge);break;}
        int ms=pb_bridge_step(&h->bridge);if(ms<0)break;
        uint64_t now=now_ms(h);
        if(now>=report_at){
            report_at=now+60000;char b[192];
            snprintf(b,sizeof(b),"\nELDEN_BT_HOST live client=%u ready=%u out=%u up=%u bad=%u pending=%u\n",
                h->bridge.client,h->bridge.ready,h->bridge.packets_out,h->bridge.frames_up,h->bridge.frames_bad,h->bridge.pending);log(b);
        }
        if(ms){IOLockLock(h->lock);if(!h->kick)IOLockSleepDeadline(h->lock,&h->kick,deadline((unsigned)ms),THREAD_UNINT);h->kick=0;IOLockUnlock(h->lock);}
    }
    finish(h);
}
