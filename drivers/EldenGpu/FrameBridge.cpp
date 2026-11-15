/* SPDX-License-Identifier: MIT */
#include "FrameBridge.h"
#include "Runtime.h"
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "NativeGOPWire.h"
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
void elden_gop_static_init(void) __asm__("__GLOBAL__sub_I_NativeGOPPresenter.cpp");
IOService *native_gop_presenter_create(void);
void native_gop_presenter_set_source(IOService *,IOService *);
bool native_gop_gpu_offer(IOService *,void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t,uint32_t);
unsigned native_legacy_fb_cache_mode(void);
}
static IOService *presenter,*parent;
static uint32_t attempted,stopped;
extern "C" int elden_gpu_framebuffer_start(IOService *nub) {
    if(!nub||native_legacy_fb_cache_mode()!=2)return kIOReturnBadArgument;
    if(__atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return kIOReturnBusy;
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);if(!handle)return kIOReturnNotReady;
    elden_gop_static_init();if(OSMetaClass::postModLoad(handle))return kIOReturnNotReady;
    parent=IOService::getResourceService();IOService *p=native_gop_presenter_create();
    if(!p||!parent){if(p)p->release();return kIOReturnNoMemory;}
    if(!p->init()||!p->attach(parent)){p->release();return kIOReturnError;}
    native_gop_presenter_set_source(p,nub);
    if(!p->start(parent)){p->detach(parent);p->release();return kIOReturnError;}
    __atomic_store_n(&presenter,p,__ATOMIC_RELEASE);return 0;
}
extern "C" bool elden_gpu_frame_offer(void *display,IOMemoryDescriptor *memory,uint64_t stride,uint32_t width,uint32_t height) {
    EldenGpuRuntimeStatus state{};elden_gpu_runtime_status(&state);
    IOService *p=__atomic_load_n(&presenter,__ATOMIC_ACQUIRE);
    if(!p||state.state!=EldenGpuReady||__atomic_load_n(&stopped,__ATOMIC_ACQUIRE))return false;
    return native_gop_gpu_offer(p,display,memory,stride,width,height,NGP_BGRX8888);
}
extern "C" void elden_gpu_framebuffer_stop(void) {
    if(__atomic_exchange_n(&stopped,1u,__ATOMIC_ACQ_REL))return;
    IOService *p=__atomic_load_n(&presenter,__ATOMIC_ACQUIRE);
    if(p)p->stop(parent); // Keep the service/mapping alive until platform reset.
}
extern "C" void elden_gpu_framebuffer_block_for_reboot(void) {
    // The RPC drain must not wait for the framebuffer workloop. All GPU
    // requests are already closed and drained; hold the last frame until reset.
    __atomic_store_n(&stopped,1u,__ATOMIC_RELEASE);
}
extern "C" void elden_gpu_display_ready(IOService *nub) {
    unsigned mode=0;uint32_t hz=0;
    if(!PE_parse_boot_argn("elden-gpu",&mode,sizeof(mode))||mode!=1)return;
    if(!PE_parse_boot_argn("elden-gpu-hz",&hz,sizeof(hz))||!hz){IOLog("Elden GPU: exact requested OPP required\n");return;}
    int result=elden_gpu_framebuffer_start(nub);
    if(!result)result=elden_gpu_start(hz);
    if(result)elden_gpu_framebuffer_stop();
    IOLog("Elden GPU display integration rc=%x\n",unsigned(result));
}
