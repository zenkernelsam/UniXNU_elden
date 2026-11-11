// SPDX-License-Identifier: BSD-2-Clause-Patent
// AppleKeyStoreState lifecycle for verified volatile Data. Notification-only by
// default; explicit state mode describes unprotected RAM, never secure storage.
#include <IOKit/IOService.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSymbol.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <mach/kmod.h>
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
extern volatile int native_owner_data_status;
extern volatile int native_owner_data_readonly;
extern volatile uint32_t native_data_persist_state;
extern uint64_t native_owner_data_seed_size;
int elden_recovery_armed(void);
int elden_usb_preview_line(const char *,unsigned);
void native_keyevents_static_init(void) __asm__("__GLOBAL__sub_I_NativeKeyStoreEvents.cpp");
IOService *native_keyevents_allocate(void);
}
static unsigned attempted,refused,stateRequested,admitted,opens,calls;
static IOService *active;
static void say(const char *line){elden_usb_preview_line(line,(unsigned)strlen(line));}
extern "C" bool native_aks_state_enabled(void){
    return stateRequested==1 && __atomic_load_n(&admitted,__ATOMIC_ACQUIRE)==1 &&
        native_owner_data_status==0 && native_owner_data_readonly==0 &&
        native_owner_data_seed_size==536870912ULL && native_data_persist_state==0;
}
extern "C" uint32_t native_aks_lock_state(void){return 0;}
extern "C" void native_aks_opened(void){
    unsigned n=__atomic_add_fetch(&opens,1,__ATOMIC_RELAXED);
    if(n<=8){char line[100];snprintf(line,sizeof(line),"\nELDEN_KEY_STATE open=%u volatile-unprotected-data\n",n);say(line);}
}
extern "C" void native_aks_call(uint32_t selector,int32_t result){
    unsigned n=__atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);
    if(n<=48){char line[120];snprintf(line,sizeof(line),"\nELDEN_KEY_STATE call=%u selector=%u result=%x\n",n,selector,(unsigned)result);say(line);}
}
extern "C" void native_keyevents_open_refused(void){
    unsigned n=__atomic_add_fetch(&refused,1,__ATOMIC_RELAXED);
    if(n<=8){char line[100];snprintf(line,sizeof(line),"\nELDEN_KEY_EVENTS user-client refused count=%u; notifications only\n",n);say(line);}
}
static bool classAbsent(const char *text){
    const OSSymbol *name=OSSymbol::withCString(text);
    bool absent=name && !OSMetaClass::getMetaClassWithName(name);OSSafeReleaseNULL(name);return absent;
}
static void worker(void *,wait_result_t){
    for(unsigned i=0;i<2400 && native_owner_data_status!=0;++i)IOSleep(50);
    IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    OSData *data=chosen?OSDynamicCast(OSData,chosen->getProperty("ephemeral-storage")):nullptr;
    uint32_t ephemeral=0;if(data && data->getLength()==4)memcpy(&ephemeral,data->getBytesNoCopy(),4);
    OSSafeReleaseNULL(chosen);
    if(ephemeral!=1 || native_owner_data_status!=0 || native_owner_data_readonly!=0 || native_owner_data_seed_size!=536870912ULL ||
       native_data_persist_state!=0 || !elden_recovery_armed()){
        say("\nELDEN_KEY_EVENTS refused: volatile Data/recovery qualification\n");thread_terminate(current_thread());return;
    }
    if(!classAbsent("AppleKeyStore") || !classAbsent("AppleKeyStoreUserClient")){
        say("\nELDEN_KEY_EVENTS refused: original class already present\n");thread_terminate(current_thread());return;
    }
    __atomic_store_n(&admitted,1,__ATOMIC_RELEASE);
    IOService *parent=IOService::getResourceService();
    void *handle=parent?OSMetaClass::preModLoad(g_kernel_kmod_info.name):nullptr;
    if(!handle){say("\nELDEN_KEY_EVENTS preModLoad failed\n");thread_terminate(current_thread());return;}
    native_keyevents_static_init();auto status=OSMetaClass::postModLoad(handle);
    char line[140];snprintf(line,sizeof(line),"\nELDEN_KEY_EVENTS metaclass status=%d\n",status);say(line);
    if(status){thread_terminate(current_thread());return;}
    IOService *service=native_keyevents_allocate();bool attached=false;
    if(service && service->init() && (attached=service->attach(parent)) && service->start(parent)){
        active=service;
        snprintf(line,sizeof(line),"\nELDEN_KEY_EVENTS registered AppleKeyStore notification service; state-client=%u key-operations=off\n",stateRequested);say(line);
    }else{
        if(attached)service->detach(parent);OSSafeReleaseNULL(service);
        say("\nELDEN_KEY_EVENTS service start failed\n");
    }
    thread_terminate(current_thread());
}
extern "C" void elden_keyevents_start(void){
    unsigned enabled=0;PE_parse_boot_argn("elden-key-events",&enabled,sizeof(enabled));
    if(enabled!=1 || __atomic_exchange_n(&attempted,1,__ATOMIC_ACQ_REL))return;
    PE_parse_boot_argn("elden-key-state",&stateRequested,sizeof(stateRequested));
    if(stateRequested>1){say("\nELDEN_KEY_EVENTS invalid state policy\n");return;}
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else say("\nELDEN_KEY_EVENTS worker creation failed\n");
}
