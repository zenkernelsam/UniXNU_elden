/* SPDX-License-Identifier: MIT */
/* Runs the actual generated presenter methods with retained-object peers. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#define CHECK(x) do{if(!(x))std::abort();}while(0)
using IOReturn=int;using wait_result_t=int;
enum{kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnNotPermitted=2,kIOReturnNotReady=3,kIOReturnTimeout=4,kIOReturnNoMemory=5,kIOReturnBadArgument=6,kIOReturnAborted=7,THREAD_AWAKENED=0,THREAD_TIMED_OUT=1,THREAD_INTERRUPTED=2,THREAD_ABORTSAFE=3,kMillisecondScale=1000000,kIOMapAnywhere=0};
struct Task{unsigned refs=1;};using task_t=Task*;constexpr task_t TASK_NULL=nullptr;
static void task_reference(task_t t){CHECK(t);++t->refs;}static void task_deallocate(task_t t){CHECK(t&&t->refs>1);--t->refs;}
struct Lock{bool held=false;};static void IOLockLock(Lock*l){CHECK(!l->held);l->held=true;}static void IOLockUnlock(Lock*l){CHECK(l->held);l->held=false;}
static void IOLockWakeup(Lock*l,void*,bool){CHECK(l->held);}
static uint64_t clockValue;static void clock_interval_to_deadline(uint32_t n,unsigned,uint64_t *v){*v=clockValue+uint64_t(n)*1000000;}
static uint64_t mach_absolute_time(){return clockValue;}static void absolutetime_to_nanoseconds(uint64_t n,uint64_t*v){*v=n;}
static int proc_selfpid(){return 1;}
static wait_result_t IOLockSleepDeadline(Lock*l,void*,uint64_t until,int){CHECK(l->held);clockValue=until;return THREAD_TIMED_OUT;}
static std::function<void()> mappingHook;static bool mappingFails;static unsigned liveMaps,completions;static int lastStatus;
struct IOMemoryMap;
struct IOMemoryDescriptor{unsigned refs=1;uint64_t length=4096;void retain(){++refs;}void release(){CHECK(refs>0);if(!--refs)delete this;}uint64_t getLength(){return length;}IOMemoryMap*createMappingInTask(task_t,unsigned,unsigned);};
struct IOMemoryMap{unsigned refs=1;IOMemoryDescriptor*m;explicit IOMemoryMap(IOMemoryDescriptor*p):m(p){m->retain();++liveMaps;}void retain(){++refs;}void release(){CHECK(refs);if(!--refs){m->release();--liveMaps;delete this;}}uint64_t getVirtualAddress(){return 0x10000000;}uint64_t getLength(){return m->length;}};
IOMemoryMap*IOMemoryDescriptor::createMappingInTask(task_t,unsigned,unsigned){if(mappingFails)return nullptr;auto*m=new IOMemoryMap(this);if(mappingHook){auto f=mappingHook;mappingHook=nullptr;f();}return m;}
#define OSSafeReleaseNULL(x) do{if(x){(x)->release();(x)=nullptr;}}while(0)
static void native_legacy_gpu_complete(void*,int status){++completions;lastStatus=status;}
static uint64_t native_gop_gpu_offers,native_gop_gpu_failures,native_gop_gpu_completions,native_gop_gpu_abandoned,native_gop_wait_stats[7],native_gop_wait_reasons[4],native_gop_wait_ring[64][3];static uint32_t native_gop_wait_ring_count;
#include "NativeGOPWire.h"
class NativeGOPPresenter{
public:
 Lock storage;Lock*gate=&storage;bool gpuMode=true,gpuClient=false,online=true;task_t handoffOwner=TASK_NULL;uint64_t handoffEpoch=0;
 struct{bool offered=false,delivered=false;uint64_t sequence=0;void*display=nullptr;IOMemoryDescriptor*memory=nullptr;IOMemoryMap*map=nullptr;uint64_t stride=0;uint32_t width=0,height=0,format=0;}swap;
 uint64_t swapSequence=0;struct Kept{IOMemoryDescriptor*memory=nullptr;IOMemoryMap*map=nullptr;task_t task=nullptr;uint32_t id=0;uint64_t used=0;};static constexpr unsigned kKept=6;Kept kept[kKept];uint32_t keptId=0;uint64_t keptUse=0;
 bool offer(void*,IOMemoryDescriptor*,uint64_t,uint32_t,uint32_t,uint32_t);
 IOReturn waitSwap(task_t,uint64_t,NGPSwap*);IOReturn completeSwap(uint64_t,int,task_t=TASK_NULL);
 void abandonSwap(task_t=TASK_NULL);void dropKept();
};
#include "ActualFrameMethods.inc"
int main(){
 Task a,b;NativeGOPPresenter p;auto *m=new IOMemoryDescriptor;NGPSwap frame{};int display=1;
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnTimeout&&a.refs==2);
 CHECK(p.waitSwap(&b,0,&frame)==kIOReturnNotPermitted&&b.refs==1);
 CHECK(p.offer(&display,m,256,64,16,1));CHECK(!p.waitSwap(&a,0,&frame)&&liveMaps==1);
 CHECK(p.completeSwap(frame.sequence,0,&b)==kIOReturnBadArgument);p.abandonSwap(&b);CHECK(p.swap.offered&&a.refs==3);
 CHECK(!p.completeSwap(frame.sequence,0,&a)&&completions==1);p.abandonSwap(&a);CHECK(a.refs==1&&m->refs==1&&!liveMaps);
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnTimeout);CHECK(p.offer(&display,m,256,64,16,1));mappingFails=true;
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnNoMemory&&!p.swap.offered&&lastStatus==kIOReturnNoMemory);mappingFails=false;p.abandonSwap(&a);
 CHECK(a.refs==1&&m->refs==1&&!liveMaps);
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnTimeout);CHECK(p.offer(&display,m,256,64,16,1));mappingHook=[&]{p.abandonSwap(&a);};
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnNotReady);CHECK(a.refs==1&&m->refs==1&&!liveMaps);
 auto *second=new IOMemoryDescriptor;NGPSwap newFrame{};
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnTimeout);CHECK(p.offer(&display,m,256,64,16,1));
 mappingHook=[&]{p.abandonSwap(&a);CHECK(p.waitSwap(&b,0,&newFrame)==kIOReturnTimeout);CHECK(p.offer(&display,second,256,64,16,1));CHECK(!p.waitSwap(&b,0,&newFrame));};
 CHECK(p.waitSwap(&a,0,&frame)==kIOReturnNotReady);CHECK(p.handoffOwner==&b&&p.swap.offered&&liveMaps==1&&m->refs==1);
 CHECK(!p.completeSwap(newFrame.sequence,0,&b));p.abandonSwap(&b);CHECK(a.refs==1&&b.refs==1&&m->refs==1&&second->refs==1&&!liveMaps);
 m->release();second->release();std::puts("PASS: actual presenter owner isolation, mapping failure fallback, close-during-map and new-owner race; references balanced");
}
