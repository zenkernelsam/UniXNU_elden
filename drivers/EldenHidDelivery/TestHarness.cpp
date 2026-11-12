// SPDX-License-Identifier: BSD-2-Clause-Patent
// The marked regions are filled with the ACTUAL old/derived methods and
// publisher. Only IOKit allocation, dispatch, locks and the ARM barrier are
// replaced for deterministic host fault injection; no hardware is simulated.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnNoMemory=-10,kIOReturnNotReady=-11;
constexpr int kIOReturnBusy=-12,kIOReturnBadArgument=-13,kIODirectionNone=0;
struct OSObject {
    std::atomic<unsigned> references{1};
    virtual ~OSObject()=default;
    void retain(){++references;} void release(){assert(references-- > 1);}
};
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define OSSafeReleaseNULL(p) do{delete (p);(p)=nullptr;}while(0)
#define TOUCH_FINGERS 10
static unsigned allocationFailures,reportFailures,allocations,sleeps;
static unsigned actionCalls;
static bool driverReady=true;
static std::vector<std::vector<uint8_t>> accepted;
static std::vector<int> observed;
static void IOSleep(unsigned milliseconds){assert(milliseconds==8);++sleeps;}
static void native_hid_input_observed(unsigned event,int result){assert(event==6);observed.push_back(result);}
static volatile uint32_t native_touch_seq,native_touch_count,native_touch_hid_reports;
static volatile int native_touch_ids[12],native_touch_xs[12],native_touch_ys[12];
namespace edge {static volatile uint32_t native_touch_hid_multi[3];}
struct IOBufferMemoryDescriptor {
    std::vector<uint8_t> bytes;
    static IOBufferMemoryDescriptor *withBytes(const void *p,size_t n,int){
        ++allocations;if(allocationFailures){--allocationFailures;return nullptr;}
        auto result=new IOBufferMemoryDescriptor;
        result->bytes.assign(static_cast<const uint8_t *>(p),static_cast<const uint8_t *>(p)+n);return result;
    }
};
struct IOWorkLoop {
    std::mutex gate,control;
    std::condition_variable changed;
    bool blockFirst=false,entered=false,unblock=false;
    IOReturn runAction(IOReturn (*fn)(OSObject *,void *,void *,void *,void *),OSObject *owner){
        {
            std::unique_lock<std::mutex> lock(control);
            if(blockFirst){blockFirst=false;entered=true;changed.notify_all();changed.wait(lock,[&]{return unblock;});}
        }
        std::lock_guard<std::mutex> lock(gate);++actionCalls;
        return fn(owner,nullptr,nullptr,nullptr,nullptr);
    }
};
class NativeHidInput:public OSObject {
public:
    IOWorkLoop loop;
    IOWorkLoop *inputLoop=&loop;
    bool hasEventDriverCached(){return driverReady;}
    IOReturn handleReport(IOBufferMemoryDescriptor *buffer,int,int){
        if(reportFailures){--reportFailures;return -99;}
        accepted.push_back(buffer->bytes);return kIOReturnSuccess;
    }
// @NATIVE_METHODS@
};
using IOLock=std::mutex;
static std::mutex sinkStorage,publishStorage;
static IOLock *sinkLock=&sinkStorage,*publishLock=&publishStorage;
static void IOLockLock(IOLock *p){p->lock();}
static void IOLockUnlock(IOLock *p){p->unlock();}
static unsigned touchReady=1,touchSinkGeneration=1;
static NativeHidInput *activeTouch;
static OSObject *touchObject;
static void *touchContext;
static decltype(&NativeHidInput::touchNotify) touchSink=&NativeHidInput::touchNotify;
struct elden_nvt_contact {unsigned id,x,y,raw_x,raw_y,width;};
struct elden_nvt_frame {unsigned count;elden_nvt_contact contacts[10];};
static void PE_parse_boot_argn(const char *,unsigned *value,size_t){*value=1;}
static void say(const char *){}
// @PUBLISHER@

static void setup(NativeHidInput &device){
    allocationFailures=reportFailures=allocations=sleeps=actionCalls=0;driverReady=true;
    native_touch_seq=native_touch_count=native_touch_hid_reports=0;
    accepted.clear();observed.clear();activeTouch=&device;touchObject=&device;touchContext=&device;
    touchSink=&NativeHidInput::touchNotify;
}
static elden_nvt_frame down(){elden_nvt_frame frame={};frame.count=1;frame.contacts[0]={0,1234,2345,0,0,1};return frame;}
static void isLift(const std::vector<uint8_t> &r){
    assert(r.size()==62 && r[0]==3 && r[1]==2 && r[2]==1 && r[61]==1);
    assert((r[3]|unsigned(r[4])<<8)==1234 && (r[5]|unsigned(r[6])<<8)==2345);
}
int main(){
    elden_nvt_frame contact=down(),empty={};
    {
        NativeHidInput device;setup(device);
        const unsigned masks[]={0x3ff,0x155,0x2b7,0x002,0};
        for(unsigned mask:masks){
            elden_nvt_frame frame={};
            for(unsigned id=0;id<10;++id)if(mask&(1u<<id)){
                frame.contacts[frame.count++]={id,id*3276u,32767u-id*3276u,0,0,1};
            }
            assert(elden_touch_hid_publish(&frame)==0);
        }
        uint64_t digest=UINT64_C(14695981039346656037);
        for(const auto &report:accepted)for(uint8_t value:report){digest^=value;digest*=UINT64_C(1099511628211);}
        assert(accepted.size()==5 && sleeps==0);
        printf("Normal trace: %016llx reports=%zu\n",(unsigned long long)digest,accepted.size());
    }
    {
        NativeHidInput device;setup(device);
        assert(elden_touch_hid_publish(&contact)==0 && accepted.size()==1);
        allocationFailures=1;
        int result=elden_touch_hid_publish(&empty);
#if ORIGINAL
        assert(result==0 && accepted.size()==1 && device.prevIds[0]==-1);
        assert(elden_touch_hid_publish(&empty)==0 && accepted.size()==1);
        puts("Original reproduced: failed lift reported success, history lost; another empty frame cannot release it");
#else
        assert(result==0 && accepted.size()==2 && sleeps==1);isLift(accepted.back());
        unsigned before=allocations;assert(elden_touch_hid_publish(&empty)==0 && allocations==before);
#endif
    }
#if !ORIGINAL
    {
        NativeHidInput device;setup(device);assert(elden_touch_hid_publish(&contact)==0);
        allocationFailures=4;assert(elden_touch_hid_publish(&empty)==kIOReturnNoMemory);
        assert(sleeps==3 && actionCalls==5 && device.prevIds[0]==1 && device.lastSeq==2);
        assert(accepted.size()==1 && device.references==1);
        assert(elden_touch_hid_publish(&empty)==0 && accepted.size()==2);isLift(accepted.back());
    }
    {
        NativeHidInput device;setup(device);assert(elden_touch_hid_publish(&contact)==0);
        reportFailures=1;assert(elden_touch_hid_publish(&empty)==-99);
        assert(sleeps==0 && device.prevIds[0]==1 && device.lastSeq==2);
        assert(elden_touch_hid_publish(&empty)==0);isLift(accepted.back());
    }
    {
        NativeHidInput device;setup(device);driverReady=false;
        assert(elden_touch_hid_publish(&contact)==kIOReturnNotReady);
        assert(sleeps==3 && actionCalls==4 && allocations==0 && device.lastSeq==0);
        driverReady=true;assert(elden_touch_hid_publish(&contact)==0 && accepted.size()==1);
        assert(elden_touch_hid_publish(&empty)==0);isLift(accepted.back());
    }
    {
        NativeHidInput device;setup(device);
        auto previous=touchSink;touchSink=nullptr;
        assert(elden_touch_hid_publish(&contact)==-1011);
        touchSink=previous;assert(elden_touch_hid_publish(&contact)==0); // no leaked lock
        auto invalid=contact;invalid.count=11;assert(elden_touch_hid_publish(&invalid)==-1001);
        invalid=contact;invalid.contacts[0].x=32768;assert(elden_touch_hid_publish(&invalid)==-2);
        assert(accepted.size()==1);
    }
#endif
    {
        NativeHidInput device;setup(device);device.loop.blockFirst=true;
        auto first=std::async(std::launch::async,[&]{return elden_touch_hid_publish(&contact);});
        {std::unique_lock<std::mutex> lock(device.loop.control);assert(device.loop.changed.wait_for(lock,std::chrono::seconds(2),[&]{return device.loop.entered;}));}
        auto second=std::async(std::launch::async,[&]{return elden_touch_hid_publish(&empty);});
#if ORIGINAL
        assert(second.wait_for(std::chrono::seconds(2))==std::future_status::ready);
        assert(second.get()==0 && accepted.empty());
#else
        assert(second.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout);
        assert(native_touch_seq==2); // first snapshot cannot be overwritten
#endif
        {std::lock_guard<std::mutex> lock(device.loop.control);device.loop.unblock=true;device.loop.changed.notify_all();}
        assert(first.get()==0);
#if ORIGINAL
        assert(accepted.empty());puts("Original reproduced: concurrent publisher overwrote the unconsumed down snapshot");
#else
        assert(second.get()==0 && accepted.size()==2);isLift(accepted.back());
        assert(device.references==1);
#endif
    }
#if !ORIGINAL
    puts("Delivery repair: transient/permanent allocation, driver absence, report failure, redundant empty, missing sink, validation and concurrent down/up passed");
#endif
}
