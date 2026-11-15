/* SPDX-License-Identifier: MIT */
#include "CallGate.h"
#include <cstdio>
#include <cstdlib>
using IOReturn=int;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,kIOReturnTimeout=3,kIOReturnBusy=4};
#define CHECK(x) do{if(!(x))std::abort();}while(0)
static uint64_t now=100,advance;static unsigned sleeps,tries,unlocks,waits,unlockAt,releaseAt;static bool backwards,stalled;
static uint64_t timeNow(void*){if(backwards){backwards=false;return now--;}return now;}
struct Clock{void*cookie=nullptr;uint64_t(*now_us)(void*)=timeNow;};
struct Submit{int result=0;unsigned pending=0,received=0;int waitAll(Clock*,uint32_t n){++waits;received=n;now+=advance;return result;}unsigned outstanding(){return pending;}} submit;
static EldenGpuCallGate eldenGate;
static struct {bool published=true;int*lock=nullptr;Submit*submit=nullptr;Clock clock;}g;
static int lockWord;
static bool IOLockTryLock(int*){return ++tries>unlockAt;}
static void IOLockUnlock(int*){++unlocks;}
static void IOSleep(unsigned ms){++sleeps;if(!stalled)now+=ms*1000;if(releaseAt&&sleeps==releaseAt)eldenGpuLeave(&eldenGate);}
#include "PublisherQuiesce.inc"
static void reset(){now=100;advance=0;sleeps=tries=unlocks=waits=unlockAt=releaseAt=0;backwards=stalled=false;submit={};eldenGate={};g={};g.lock=&lockWord;g.submit=&submit;}
int main(){
 reset();CHECK(native_adreno_quiesce_for_reboot(0)==kIOReturnBadArgument&&!eldenGpuClosed(&eldenGate));
 reset();g.published=false;CHECK(!native_adreno_quiesce_for_reboot(1000)&&!waits&&eldenGpuClosed(&eldenGate));
 reset();g.lock=nullptr;CHECK(native_adreno_quiesce_for_reboot(1000)==kIOReturnNotReady);
 reset();CHECK(!native_adreno_quiesce_for_reboot(5000)&&waits==1&&unlocks==1&&submit.received==5000);
 reset();CHECK(eldenGpuEnter(&eldenGate));releaseAt=3;CHECK(!native_adreno_quiesce_for_reboot(5000)&&sleeps==3&&submit.received==2000);
 reset();unlockAt=2;CHECK(!native_adreno_quiesce_for_reboot(5000)&&tries==3&&submit.received==3000);
 reset();CHECK(eldenGpuEnter(&eldenGate));CHECK(native_adreno_quiesce_for_reboot(1500)==kIOReturnTimeout&&waits==0);
 reset();submit.result=27;CHECK(native_adreno_quiesce_for_reboot(5000)==27&&unlocks==1);
 reset();submit.pending=1;CHECK(native_adreno_quiesce_for_reboot(5000)==kIOReturnBusy&&unlocks==1);
 reset();advance=5000;CHECK(native_adreno_quiesce_for_reboot(5000)==kIOReturnTimeout&&unlocks==1);
 reset();backwards=true;CHECK(native_adreno_quiesce_for_reboot(5000)==kIOReturnTimeout&&!waits);
 reset();stalled=true;unlockAt=3000;CHECK(native_adreno_quiesce_for_reboot(5000)==kIOReturnTimeout&&sleeps==2001&&!waits);
 std::puts("PASS: 12 actual GPU drain cases including late success, clock reversal/stall, live calls and outstanding fences");
}
