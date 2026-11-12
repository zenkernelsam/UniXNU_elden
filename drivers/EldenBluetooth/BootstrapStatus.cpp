/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Read-only counterpart of sheng's kernel bootstrap status service. */
#include "BootstrapStatus.h"
#include "RunControl.h"
#include <sys/errno.h>
#include <sys/sysctl.h>
extern "C" unsigned elden_bt_host_state(void) __attribute__((weak_import));
namespace {
struct Published {unsigned sequence,state;uint64_t epoch;uint16_t manufacturer,patch;uint8_t address[6];};
static Published published;
static int bootstrap_status SYSCTL_HANDLER_ARGS {
#pragma unused(oidp,arg1,arg2)
    if(req->newptr)return EPERM;
    EldenBtBootStatus value;
    for(unsigned attempt=0;attempt<8;attempt++){
        unsigned before=__atomic_load_n(&published.sequence,__ATOMIC_ACQUIRE);
        if(before&1)continue;
        value.version=1;
        value.state=__atomic_load_n(&published.state,__ATOMIC_RELAXED);
        value.epoch=__atomic_load_n(&published.epoch,__ATOMIC_RELAXED);
        value.manufacturer=__atomic_load_n(&published.manufacturer,__ATOMIC_RELAXED);
        value.patch=__atomic_load_n(&published.patch,__ATOMIC_RELAXED);value.reserved=0;
        for(unsigned i=0;i<6;i++)value.address[i]=__atomic_load_n(&published.address[i],__ATOMIC_RELAXED);
        value.reserved2=0;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(before!=__atomic_load_n(&published.sequence,__ATOMIC_ACQUIRE))continue;
        if(elden_wcn_stop_requested()&&(value.state==EldenBtBootReady||value.state==EldenBtBootStarting))
            value.state=EldenBtBootStopping;
        if(value.state==EldenBtBootReady){
            unsigned host=elden_bt_host_state?elden_bt_host_state():0;
            if(host!=1)value.state=host?EldenBtBootFailed:EldenBtBootStarting;
        }
        return SYSCTL_OUT(req,&value,sizeof(value));
    }
    return EAGAIN;
}
}
SYSCTL_PROC(_kern,OID_AUTO,elden_bt_bootstrap,CTLTYPE_OPAQUE|CTLFLAG_RD|CTLFLAG_LOCKED,
    nullptr,0,bootstrap_status,"S,EldenBtBootStatus","Elden verified Bluetooth bootstrap status");
extern "C" void elden_bt_bootstrap_publish(unsigned state,uint64_t epoch,uint16_t manufacturer,uint16_t patch,const uint8_t address[6]){
    if(state>EldenBtBootStopping)return;
    __atomic_add_fetch(&published.sequence,1u,__ATOMIC_ACQ_REL);
    __atomic_store_n(&published.state,state,__ATOMIC_RELAXED);
    __atomic_store_n(&published.epoch,epoch,__ATOMIC_RELAXED);
    __atomic_store_n(&published.manufacturer,manufacturer,__ATOMIC_RELAXED);
    __atomic_store_n(&published.patch,patch,__ATOMIC_RELAXED);
    for(unsigned i=0;i<6;i++)__atomic_store_n(&published.address[i],address?address[i]:0,__ATOMIC_RELAXED);
    __atomic_add_fetch(&published.sequence,1u,__ATOMIC_RELEASE);
}
