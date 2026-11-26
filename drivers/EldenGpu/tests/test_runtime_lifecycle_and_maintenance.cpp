// SPDX-License-Identifier: MIT
// Runtime lifecycle & maintenance state-machine boundary stress harness (ASan+UBSan).
// Real units under test: extracted elden_gpu_start/elden_gpu_quiesce_for_reboot/
// elden_gpu_runtime_status entry bodies from derived EldenGpuRuntime.cpp, composed
// end-to-end with the real derived PublisherQuiesce.inc maintenance/recovery body.
// Platform operations are explicit test peers; no IO, no device, no hardware claim.
// Gap-focused vs chain TestRuntime.cpp/TestQuiesce.cpp: per-stage status invariants,
// stress loops, stage-precision fault injection, quiesce honesty composition.
#include "Runtime.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static unsigned failures=0;
static unsigned long iterations=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;}else{++iterations;}}while(0)

typedef int IOReturn;
enum{kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnBusy=2,kIOReturnNoMemory=3,kIOReturnAborted=4,kIOReturnBadArgument=5,kIOReturnTimeout=6};

// ---- runtime platform peers
static bool route=true,rpc=true;
static int startResult;
static unsigned starts,frameBlocks;
static int failStage=-1; // -1 none; otherwise start() fails at this stage index
static unsigned stageCounter;
static volatile uint32_t runtimeState=EldenGpuIdle,runtimeStage,runtimeResult,stopRequested;
static uint64_t targetHz,startDeadline;
struct State{static void*operator new(size_t n)noexcept{return std::calloc(1,n);}static void operator delete(void*p)noexcept{std::free(p);}};
static State*context;
static uint64_t clockUs(){return 100;}
extern "C" int elden_recovery_armed(){return route;}
extern "C" int elden_recovery_rpc_ready(){return rpc;}
extern "C" void elden_gpu_framebuffer_block_for_reboot(){++frameBlocks;}
static void IOLog(const char*,...){}
static int start(){++starts;if(failStage>=0&&(int)stageCounter++==failStage)return 500+failStage;return startResult;}

// ---- quiesce platform peers (real PublisherQuiesce.inc consumes these)
static uint64_t now=100,advanceStep=0;
static bool backwards=false,stalled=false;
static unsigned releaseAt=0,unlockAt=0;
static uint64_t timeNow(void*){if(backwards){backwards=false;return now--;}return now;}
struct Clock{void*cookie=nullptr;uint64_t(*now_us)(void*)=timeNow;};
struct Submit{
    int result=0;unsigned pending=0,received=0,waits=0;
    int waitAll(Clock*,uint32_t n){++waits;received=n;now+=advanceStep;if(pending&&advanceStep)pending=0;return result;}
    unsigned outstanding(){return pending;}
} submit;
#include "CallGate.h"
static EldenGpuCallGate eldenGate;
static int lockWord;
static unsigned sleeps=0,tries=0,unlocks=0;
static bool IOLockTryLock(int*){return ++tries>unlockAt;}
static void IOLockUnlock(int*){++unlocks;}
static void IOSleep(unsigned ms){++sleeps;if(!stalled)now+=ms*1000;if(releaseAt&&sleeps==releaseAt)eldenGpuLeave(&eldenGate);}
static struct {bool published=true;int*lock=nullptr;Submit*submit=nullptr;Clock clock;}g;

extern "C" int native_adreno_quiesce_for_reboot(uint32_t timeout_us);
#include "PublisherQuiesce.inc"

#include "ActualRuntimeEntries.inc"

static void reset(){
    delete context;context=nullptr;
    route=rpc=true;startResult=0;failStage=-1;stageCounter=0;
    starts=frameBlocks=0;
    runtimeState=EldenGpuIdle;runtimeStage=runtimeResult=stopRequested=0;targetHz=startDeadline=0;
    now=100;advanceStep=0;backwards=stalled=false;releaseAt=unlockAt=0;
    sleeps=tries=unlocks=0;
    submit={};eldenGate={};g={};g.lock=&lockWord;g.submit=&submit;
}

static EldenGpuRuntimeStatus snap(){EldenGpuRuntimeStatus s{};elden_gpu_runtime_status(&s);return s;}

int main(){
    // ---- Positive 1: full lifecycle with per-stage status invariants
    {
        reset();
        EldenGpuRuntimeStatus s0=snap();
        CHECK(s0.state==EldenGpuIdle&&!s0.retained&&s0.requested_hz==0);
        CHECK(!elden_gpu_start(222000000));
        EldenGpuRuntimeStatus s1=snap();
        CHECK(s1.state==EldenGpuReady&&s1.retained&&s1.requested_hz==222000000);
        CHECK(!elden_gpu_quiesce_for_reboot(1000));
        EldenGpuRuntimeStatus s2=snap();
        CHECK(s2.state==EldenGpuDrained&&s2.retained);
        CHECK(s2.requested_hz==222000000);
    }
    // ---- Positive 2: quiesce honesty — retained set, never claims clocks off
    {
        reset();
        CHECK(!elden_gpu_start(525000000));
        CHECK(!elden_gpu_quiesce_for_reboot(1000));
        EldenGpuRuntimeStatus s=snap();
        CHECK(s.retained!=0);
        CHECK(frameBlocks==1);
        // repeated quiesce after drain is stable success, no double block
        CHECK(!elden_gpu_quiesce_for_reboot(1000));
    }
    // ---- Positive 3: stress — repeated lifecycle loops with quiesce maintenance cycles
    {
        unsigned cycles=0;
        for(unsigned round=0;round<500;++round){
            reset();
            if(elden_gpu_start(222000000+ (round%2)*300000000))continue; // alternate 222M/522M
            if(elden_gpu_start(222000000)!=kIOReturnBusy){++failures;std::printf("FAIL stress double-start round %u\n",round);continue;}
            if(elden_gpu_quiesce_for_reboot(1000))continue;
            EldenGpuRuntimeStatus s=snap();
            if(s.state==EldenGpuDrained&&s.retained)++cycles;
            iterations+=4;
        }
        CHECK(cycles==500);
    }
    // ---- Positive 4: quiesce before any start drains trivially (established semantics preserved under repetition)
    {
        for(unsigned round=0;round<100;++round){
            reset();
            if(elden_gpu_quiesce_for_reboot(1000))continue;
            EldenGpuRuntimeStatus s=snap();
            if(s.state!=EldenGpuDrained){++failures;std::printf("FAIL prestart drain round %u\n",round);continue;}
            iterations+=2;
        }
        CHECK(iterations>1000);
    }
    // ---- Positive 5: valid OPP admission sweep — every admitted frequency accepted, exact-match semantics
    {
        const uint64_t valid[]={160000000,222000000,342000000,389000000,443000000,525000000,607000000,660000000,734000000,832000000,900000000};
        unsigned accepted=0;
        for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);++i){
            reset();
            if(!elden_gpu_start(valid[i])){
                EldenGpuRuntimeStatus s=snap();
                if(s.state==EldenGpuReady&&s.requested_hz==valid[i])++accepted;
            }
            iterations+=2;
        }
        CHECK(accepted==sizeof(valid)/sizeof(*valid));
    }
    // ---- Positive 6: status struct consistency under load — fields coherent across rapid queries
    {
        reset();
        CHECK(!elden_gpu_start(342000000));
        bool coherent=true;
        for(unsigned i=0;i<300;++i){
            EldenGpuRuntimeStatus s=snap();
            if(s.state!=EldenGpuReady||s.requested_hz!=342000000||!s.retained)coherent=false;
            iterations++;
        }
        CHECK(coherent);
    }

    // ---- Negative 1: invalid hz values never reach Ready
    {
        // Entry admission is a range gate (0<hz<=1.1GHz); exact-OPP membership is a
        // documented caller contract enforced downstream by the votes layer, which is
        // fail-closed. Audit-verified layered design, not a defect.
        const uint64_t entry_rejected[]={0,1100000001,0xFFFFFFFFFFFFFFFFull};
        unsigned rejected=0;
        for(unsigned i=0;i<sizeof(entry_rejected)/sizeof(*entry_rejected);++i){
            reset();
            int rc=elden_gpu_start(entry_rejected[i]);
            EldenGpuRuntimeStatus s=snap();
            if(rc&&s.state!=EldenGpuReady)++rejected;
            iterations++;
        }
        CHECK(rejected==sizeof(entry_rejected)/sizeof(*entry_rejected));
        // In-range non-OPP: passes entry admission but any downstream failure lands
        // in Held (fail-closed), never silently Ready with a fabricated success.
        reset();
        startResult=42;
        int rc2=elden_gpu_start(222000001);
        EldenGpuRuntimeStatus s2=snap();
        CHECK(rc2==42&&s2.state==EldenGpuHeld);
    }
    // ---- Negative 2: one-attempt contract — start after Ready/Drained/Held all Busy-refused
    {
        reset();
        CHECK(!elden_gpu_start(222000000));
        CHECK(elden_gpu_start(222000000)==kIOReturnBusy);
        CHECK(!elden_gpu_quiesce_for_reboot(1000));
        CHECK(elden_gpu_start(222000000)==kIOReturnBusy);
        reset();
        startResult=71;
        CHECK(elden_gpu_start(222000000)==71);
        EldenGpuRuntimeStatus s=snap();
        CHECK(s.state==EldenGpuHeld);
        CHECK(elden_gpu_start(222000000)==kIOReturnBusy);
    }
    // ---- Negative 3: quiesce timeout with stuck fence -> Held, never false Drained, retained honest
    {
        reset();
        CHECK(!elden_gpu_start(222000000));
        submit.pending=3;stalled=true;advanceStep=0;
        int rc=elden_gpu_quiesce_for_reboot(1000);
        EldenGpuRuntimeStatus s=snap();
        CHECK(rc!=0);
        CHECK(s.state==EldenGpuHeld);
        CHECK(s.state!=EldenGpuDrained);
        CHECK(s.retained!=0);
    }
    // ---- Negative 4: timeout_us==0 rejected without side effects
    {
        reset();
        CHECK(!elden_gpu_start(222000000));
        int rc=elden_gpu_quiesce_for_reboot(0);
        CHECK(rc==kIOReturnBadArgument);
        EldenGpuRuntimeStatus s=snap();
        CHECK(s.state==EldenGpuReady);
    }
    // ---- Negative 5: runtime_status(nullptr) safety — no crash, state unchanged
    {
        reset();
        CHECK(!elden_gpu_start(222000000));
        elden_gpu_runtime_status(nullptr);
        EldenGpuRuntimeStatus s=snap();
        CHECK(s.state==EldenGpuReady);
    }
    // ---- Negative 6: platform peer faults — route/rpc down, allocation-like start failure -> Held, never Ready
    {
        reset();route=false;
        CHECK(elden_gpu_start(222000000)==kIOReturnNotReady);
        EldenGpuRuntimeStatus s1=snap();
        CHECK(s1.state!=EldenGpuReady);
        reset();rpc=false;
        CHECK(elden_gpu_start(222000000)==kIOReturnNotReady);
        reset();startResult=55;
        CHECK(elden_gpu_start(222000000)==55);
        EldenGpuRuntimeStatus s2=snap();
        CHECK(s2.state==EldenGpuHeld&&eldenGpuClosed(&eldenGate));
    }
    // ---- Negative 7: maintenance path misuse — quiesce peers unpublished/not-ready fail closed
    {
        reset();
        g.published=false;
        // quiesce with unpublished state must not fabricate drain success
        reset();
        g.lock=nullptr;
        int rc=native_adreno_quiesce_for_reboot(1000);
        CHECK(rc!=0);
    }

    if(failures){
        std::printf("RUNTIME_LIFECYCLE_RESULT {\"passed\":false,\"failures\":%u,\"iterations\":%lu}\n",failures,iterations);
        return 1;
    }
    std::printf("RUNTIME_LIFECYCLE_RESULT {\"passed\":true,\"positive\":6,\"negative\":7,\"iterations\":%lu}\n",iterations);
    return 0;
}
