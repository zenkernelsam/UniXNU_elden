// SPDX-License-Identifier: MIT
// Host check of the Elden frame diagnostics: the bridge's offer/refusal/fence counters
// (native_elden_frame_diag, FrameBridge.cpp compiled as its own unit), the derived
// display glue's fallback/copy/validation feeds (ActualGlueMethods.inc), the forwarded
// pixel format, and clean decoupling while the GPU path is disabled or idle.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "Runtime.h"
#include "NativeGOPWire.h"
#include "FrameBridge.h"
// The static-init alias FrameBridge.cpp names with an asm symbol.
__asm__(".globl __GLOBAL__sub_I_NativeGOPPresenter.cpp\n__GLOBAL__sub_I_NativeGOPPresenter.cpp:\n ret\n");
// Kernel + runtime peers FrameBridge.cpp and the glue bodies call.
kmod_info_t g_kernel_kmod_info{"/elden.test"};
boolean_t PE_parse_boot_argn(const char *name,void *value,int){if(!std::strcmp(name,"elden-gpu")){*static_cast<unsigned*>(value)=1;return true;}if(!std::strcmp(name,"elden-gpu-hz")){*static_cast<uint32_t*>(value)=833000000;return true;}return false;}
void IOLog(const char *,...){}
static EldenGpuRuntimeStatus runtimeState{};
extern "C" void elden_gpu_runtime_status(EldenGpuRuntimeStatus *out){*out=runtimeState;}
extern "C" int elden_gpu_start(uint64_t){return 0;}
extern "C" unsigned native_legacy_fb_cache_mode(void){return 2;}
static unsigned gopOffers;static uint32_t lastGopFormat;static bool gopAccepts=true;
static IOService *created;
extern "C" IOService *native_gop_presenter_create(void){return created=new IOService;}
extern "C" void native_gop_presenter_set_source(IOService *,IOService *){}
extern "C" bool native_gop_gpu_offer(IOService *,void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t,uint32_t format){++gopOffers;lastGopFormat=format;return gopAccepts;}
static bool fencesOk=true;
extern "C" bool native_adreno_fences_detail(uint32_t *s,uint32_t *r,uint32_t *d){if(d)d[0]=d[1]=d[2]=0;if(!fencesOk)return false;*s=40;*r=37;return true;}
// The derived display-glue bodies (ActualGlueMethods.inc): they feed the same diag block
// plus the glue's own statics and report().
extern "C" volatile uint64_t native_elden_frame_diag[16];
static unsigned transfers,validations;
static void report(const char *,...){}
#include "ActualGlueMethods.inc"
int main(){
    IOService display;IOMemoryDescriptor m;
    // GPU path never attached (elden-gpu off): every offer lands on the display's own CPU
    // path, counted at [4], and nothing reaches the presenter.
    CHECK(!native_legacy_gpu_offer(&display,&m,256,64,16,NGP_BGRX8888));
    CHECK(native_elden_frame_diag[0]==1&&native_elden_frame_diag[4]==1&&native_elden_frame_diag[10]==NGP_BGRX8888&&gopOffers==0);
    // Presenter attached but runtime idle: refused at [2], still no presenter traffic.
    CHECK(elden_gpu_framebuffer_start(&display)==0);
    CHECK(!native_legacy_gpu_offer(&display,&m,256,64,16,NGP_BGRX8888)&&native_elden_frame_diag[2]==1&&gopOffers==0);
    // GPU ready, presenter accepts: format forwarded untouched, fence snapshot lands.
    runtimeState.state=EldenGpuReady;
    CHECK(native_legacy_gpu_offer(&display,&m,256,64,16,0xdeadbeef)&&gopOffers==1&&lastGopFormat==0xdeadbeef);
    CHECK(native_elden_frame_diag[1]==1&&native_elden_frame_diag[10]==0xdeadbeef);
    CHECK(native_elden_frame_diag[11]==1&&native_elden_frame_diag[12]==40&&native_elden_frame_diag[13]==37);
    // Fence snapshot refused (GPU unpublished): offer still accepted, counted at [14].
    fencesOk=false;CHECK(native_legacy_gpu_offer(&display,&m,256,64,16,NGP_BGRX8888));CHECK(native_elden_frame_diag[14]==1);fencesOk=true;
    // Presenter declined (no client / swap outstanding): refused at [5].
    gopAccepts=false;CHECK(!native_legacy_gpu_offer(&display,&m,256,64,16,NGP_BGRX8888)&&native_elden_frame_diag[5]==1);gopAccepts=true;
    // Reboot block: refused at [3].
    elden_gpu_framebuffer_block_for_reboot();
    CHECK(!native_legacy_gpu_offer(&display,&m,256,64,16,NGP_BGRX8888)&&native_elden_frame_diag[3]==1);
    // Display-side feeds: fallback status, copy count/bytes, accepted validations.
    native_legacy_gpu_fallback_observed(9);native_legacy_gpu_fallback_observed(5);
    CHECK(native_elden_frame_diag[6]==2&&native_elden_frame_diag[7]==5);
    native_legacy_copy_timing_observed(1000,8192);native_legacy_copy_timing_observed(2000,4096);
    CHECK(native_elden_frame_diag[8]==2&&native_elden_frame_diag[9]==12288);
    native_legacy_swap_validation_observed(true,0,nullptr,nullptr);native_legacy_swap_validation_observed(false,3,nullptr,nullptr);
    CHECK(native_elden_frame_diag[15]==1);
    elden_gpu_framebuffer_stop();delete created;
    std::puts("PASS: frame diagnostics — per-reason offer refusals, fallback/copy/format counters, KMD fence correlation, GPU-off decoupling");
}
