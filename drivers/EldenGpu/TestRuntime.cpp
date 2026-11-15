/* SPDX-License-Identifier: MIT */
/* Actual runtime entry/control bodies; platform operations are test peers. */
#include "Runtime.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#define CHECK(x) do{if(!(x))std::abort();}while(0)
enum{kIOReturnNotReady=1,kIOReturnBusy=2,kIOReturnNoMemory=3,kIOReturnAborted=4,kIOReturnBadArgument=5};
static bool route=true,rpc=true,failAllocation,cancelInside;
static int startResult,drainResult;static unsigned starts,drains,closes,frameBlocks;
static volatile uint32_t runtimeState=EldenGpuIdle,runtimeStage,runtimeResult,stopRequested;
static uint64_t targetHz,startDeadline;
struct State{static void*operator new(size_t n)noexcept{return failAllocation?nullptr:std::calloc(1,n);}static void operator delete(void*p)noexcept{std::free(p);}};
static State*context;
static uint64_t clockUs(){return 100;}
extern "C" int elden_recovery_armed(){return route;}
extern "C" int elden_recovery_rpc_ready(){return rpc;}
extern "C" void native_adreno_close_gate(){++closes;}
extern "C" int native_adreno_quiesce_for_reboot(uint32_t n){CHECK(n&&n<=2000000);++drains;return drainResult;}
extern "C" void elden_gpu_framebuffer_block_for_reboot(){++frameBlocks;}
static void IOLog(const char*,...){}
static int start(){++starts;if(cancelInside)CHECK(elden_gpu_quiesce_for_reboot(1000)==kIOReturnBusy);return startResult;}
#include "ActualRuntimeEntries.inc"
static void reset(){delete context;context=nullptr;route=rpc=true;failAllocation=cancelInside=false;startResult=drainResult=0;starts=drains=closes=frameBlocks=0;runtimeState=EldenGpuIdle;runtimeStage=runtimeResult=stopRequested=0;targetHz=startDeadline=0;}
int main(){
 reset();CHECK(elden_gpu_start(0)==kIOReturnNotReady&&!starts&&!context);
 reset();CHECK(elden_gpu_start(1100000001)==kIOReturnNotReady&&!starts);
 reset();route=false;CHECK(elden_gpu_start(222000000)==kIOReturnNotReady&&!starts);
 reset();rpc=false;CHECK(elden_gpu_start(222000000)==kIOReturnNotReady&&!starts);
 reset();failAllocation=true;CHECK(elden_gpu_start(222000000)==kIOReturnNoMemory&&runtimeState==EldenGpuHeld&&closes==1&&!starts);
 reset();CHECK(!elden_gpu_start(222000000)&&runtimeState==EldenGpuReady&&starts==1);CHECK(elden_gpu_start(222000000)==kIOReturnBusy&&starts==1);
 EldenGpuRuntimeStatus status{};elden_gpu_runtime_status(&status);CHECK(status.requested_hz==222000000&&status.retained&&status.state==EldenGpuReady);
 reset();startResult=71;CHECK(elden_gpu_start(222000000)==71&&runtimeState==EldenGpuHeld&&closes==1);
 reset();cancelInside=true;CHECK(elden_gpu_start(222000000)==kIOReturnAborted&&runtimeState==EldenGpuHeld&&closes==1&&!drains);
 reset();CHECK(!elden_gpu_quiesce_for_reboot(1000)&&runtimeState==EldenGpuDrained&&!drains);CHECK(elden_gpu_start(222000000)==kIOReturnBusy);
 reset();CHECK(!elden_gpu_start(222000000));CHECK(elden_gpu_quiesce_for_reboot(0)==kIOReturnBadArgument&&!stopRequested&&!drains);
 CHECK(!elden_gpu_quiesce_for_reboot(1000)&&runtimeState==EldenGpuDrained&&drains==1&&frameBlocks==1);
 CHECK(!elden_gpu_quiesce_for_reboot(1000)&&drains==1);
 reset();CHECK(!elden_gpu_start(222000000));drainResult=99;CHECK(elden_gpu_quiesce_for_reboot(1000)==99&&runtimeState==EldenGpuHeld&&!frameBlocks);
 reset();std::puts("PASS: actual runtime admission, one attempt, allocation/error/cancel handling, truthful drain status and retained resources; no hardware");
}
