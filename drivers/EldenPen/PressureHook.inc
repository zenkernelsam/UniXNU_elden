/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* OS17 gts7xlwifi pressure adaptation, constrained to our retained driver and
 * the exact Elden-qualified original220-slot/37-slot tables. Not the donor's
 * unsafe256-slot copy. The table/service are retained until platform reset. */
namespace PenPressure {
enum {DispatchSlot=198,DriverSlots=220,GetType=18,GetInteger=23,GetFixed=25,SetFixed=26};
static IORegistryEntry *ownedDriver;
static uintptr_t *ownedTable;
static uintptr_t originalDispatch;
static volatile uint32_t converted,rejected;
template<typename R,typename... A> static R call(void *o,unsigned slot,A... args){
    return reinterpret_cast<R (*)(void *,A...)>((*reinterpret_cast<uintptr_t **>(o))[slot])(o,args...);
}
static bool exactEvent(OSObject *e){
    return e&&*reinterpret_cast<uintptr_t **>(e)==reinterpret_cast<uintptr_t *>(ELDEN_PEN_EVENT_VPTR)&&e->metaCast("IOHIDEvent");
}
#include "PoseHook.inc"
static void convert(OSObject *e,const Pose &pose){
    if(!exactEvent(e)||call<uint32_t>(e,GetType)!=11||call<int32_t,uint32_t,IOOptionBits>(e,GetInteger,0xb0004,0)!=0)return;
    int32_t value=call<int32_t,uint32_t,IOOptionBits>(e,GetFixed,0xb000a,0),scaled;
    if(elden_pen_pressure_fixed(value,&scaled)){__atomic_fetch_add(&rejected,1u,__ATOMIC_RELAXED);return;}
    call<void,uint32_t,int32_t,IOOptionBits>(e,SetFixed,0xb000a,scaled,0);
    __atomic_fetch_add(&converted,1u,__ATOMIC_RELAXED);
    applyPose(e,pose);
}
static void dispatch(IOService *self,OSObject *event,IOOptionBits options){
    if(self==__atomic_load_n(&ownedDriver,__ATOMIC_ACQUIRE)&&*reinterpret_cast<uintptr_t **>(self)==ownedTable&&exactEvent(event)&&call<uint32_t>(event,GetType)==11){
        const Pose pose=pendingPose(self);
        OSArray *children=*reinterpret_cast<OSArray **>(reinterpret_cast<uint8_t *>(event)+24);
        if(!children)convert(event,pose);
        else if(OSDynamicCast(OSArray,children)&&children->getCount()<=16){
            if(!children->getCount())convert(event,pose);
            for(unsigned i=0;i<children->getCount();++i)convert(children->getObject(i),pose);
        }else __atomic_fetch_add(&rejected,1u,__ATOMIC_RELAXED);
    }
    reinterpret_cast<void (*)(IOService *,OSObject *,IOOptionBits)>(__atomic_load_n(&originalDispatch,__ATOMIC_ACQUIRE))(self,event,options);
}
/* Called on the pen workloop only, before the first pen report. */
static bool install(IORegistryEntry *driver){
    IORegistryEntry *owned=__atomic_load_n(&ownedDriver,__ATOMIC_ACQUIRE);
    if(owned)return driver==owned&&*reinterpret_cast<uintptr_t **>(driver)==ownedTable;
    if(!driver||!driver->metaCast("IOHIDEventDriver"))return false;
    uintptr_t *table=*reinterpret_cast<uintptr_t **>(driver);
    if(table!=reinterpret_cast<uintptr_t *>(ELDEN_PEN_DRIVER_VPTR)||table[DispatchSlot]!=ELDEN_PEN_ORIGINAL_DISPATCH)return false;
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(DriverSlots*sizeof(uintptr_t)));
    if(!copy)return false;
    memcpy(copy,table,DriverSlots*sizeof(uintptr_t));driver->retain();
    ownedTable=copy;
    __atomic_store_n(&ownedDriver,driver,__ATOMIC_RELEASE);
    __atomic_store_n(&originalDispatch,copy[DispatchSlot],__ATOMIC_RELEASE);copy[DispatchSlot]=(uintptr_t)&dispatch;
    __atomic_store_n(reinterpret_cast<uintptr_t **>(driver),copy,__ATOMIC_RELEASE);
    return true;
}
}
