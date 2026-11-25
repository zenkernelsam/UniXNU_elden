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
/* Elden-side frame diagnostics, aligned with the GopFramebuffer@b0d812d counters: [0] frame offers the display driver made to the GPU
 * path, [1] of them the presenter accepted; offers refused because [2] the GPU runtime was not EldenGpuReady, [3] the bridge was stopped
 * for reboot, [4] the presenter never attached (the GPU path was never enabled: native_legacy_gpu_offer still forwards, so the display's
 * own CPU path shows up here as [4], not as GPU work) or [5] the presenter declined (no client waiting, a swap already outstanding);
 * [6] CPU fallbacks the display reported to native_legacy_gpu_fallback_observed and [7] the last status it named; [8] CPU copy calls
 * and [9] their bytes from native_legacy_copy_timing_observed; [10] the last pixel format offered (the display's real surface format,
 * forwarded since b0d812d); [11] KMD fence snapshots taken at an accepted offer, [12]/[13] the submission and retirement counters that
 * snapshot read, [14] snapshots refused (GPU not published or its submit lock busy); [15] swaps the display's validator accepted. */
volatile uint64_t native_elden_frame_diag[16];
bool native_adreno_fences_detail(uint32_t *,uint32_t *,uint32_t *) __attribute__((weak_import));
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
extern "C" bool elden_gpu_frame_offer(void *display,IOMemoryDescriptor *memory,uint64_t stride,uint32_t width,uint32_t height,uint32_t format) {
    EldenGpuRuntimeStatus state{};elden_gpu_runtime_status(&state);
    IOService *p=__atomic_load_n(&presenter,__ATOMIC_ACQUIRE);
    native_elden_frame_diag[0]=native_elden_frame_diag[0]+1;native_elden_frame_diag[10]=format;
    if(!p){native_elden_frame_diag[4]=native_elden_frame_diag[4]+1;return false;}
    if(state.state!=EldenGpuReady){native_elden_frame_diag[2]=native_elden_frame_diag[2]+1;return false;}
    if(__atomic_load_n(&stopped,__ATOMIC_ACQUIRE)){native_elden_frame_diag[3]=native_elden_frame_diag[3]+1;return false;}
    if(!native_gop_gpu_offer(p,display,memory,stride,width,height,format)) {
        native_elden_frame_diag[5]=native_elden_frame_diag[5]+1;return false;
    }
    native_elden_frame_diag[1]=native_elden_frame_diag[1]+1;
    /* Pair the accepted frame with the KMD's submission and retirement counters (the same record the display's gpuWrote keeps for its
     * direct scanout): the frame's sequence arrives at completeSwap, the fences say where the GPU's work stood when the wait began. */
    uint32_t submitted=0,retired=0,detail[3]={};
    if(&native_adreno_fences_detail && native_adreno_fences_detail(&submitted,&retired,detail)) {
        native_elden_frame_diag[11]=native_elden_frame_diag[11]+1;
        native_elden_frame_diag[12]=submitted;native_elden_frame_diag[13]=retired;
    } else native_elden_frame_diag[14]=native_elden_frame_diag[14]+1;
    return true;
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
