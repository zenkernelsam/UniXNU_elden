/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativeClocks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include <string>
static unsigned checks,maps,map_fail;static bool irq_mode;using IOLock=std::mutex;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"nativeclocks line%u: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<IOLock *> locks;
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static void *IOMalloc(size_t bytes){return std::calloc(1,bytes);}
static bool ml_at_interrupt_context(){return irq_mode;}
extern "C" {
void elden_camera_clock_peer_setup(void);int elden_camera_clock_peer_read(unsigned,uint32_t,uint32_t *);
int elden_camera_clock_peer_write(unsigned,uint32_t,uint32_t);void elden_camera_clock_peer_delay(unsigned);
uint64_t elden_camera_clock_peer_now(void);EldenCameraClockIo elden_camera_clock_peer_io(void);
}
static void IODelay(unsigned us){elden_camera_clock_peer_delay(us);}
namespace {
struct CameraClockWindow {unsigned space=9;
    bool map(uint64_t pa,uint32_t length){if(++maps==map_fail)return false;
        if(pa==0xade0000){CHECK(length==0x20000);space=0;}else if(pa==0x1760000){CHECK(length==0x6000);space=1;}
        else{CHECK(pa==0xf100000&&length==0x78000);space=2;}return true;}
    int read(uint32_t reg,uint32_t *out){return elden_camera_clock_peer_read(space,reg,out);}
    int write(uint32_t reg,uint32_t value){return elden_camera_clock_peer_write(space,reg,value);}
};
static uint64_t camera_clock_now(void *){return elden_camera_clock_peer_now();}
}
#include "NativeClockMethods.inc"
int main(int argc,char **argv){CHECK(argc==2);std::string test=argv[1];elden_camera_clock_peer_setup();auto io=elden_camera_clock_peer_io();
    EldenCameraNativeClockOwner owner{io.context,io.owner,io.epoch,io.admit,io.lock,io.controllers,io.dependency,io.consumer_idle,io.sensor_idle,io.root_unused,io.domain_unused,io.pins_unused};
    if(test=="map-fail")map_fail=2;if(test=="irq")irq_mode=true;EldenCameraNativeClocks *out=nullptr;
    int rc=elden_camera_native_clocks_open(&owner,&out);
    if(test=="map-fail"){CHECK(rc==-12&&out&&maps==2);CHECK(elden_camera_native_clocks_stop(out,7,9)==-19);}
    else if(test=="irq"){CHECK(rc==-22&&!maps&&!out);}
    else{CHECK(!rc&&out&&maps==3);CHECK(!elden_camera_native_pins_prepare(out,7,9));
        CHECK(!elden_camera_native_domain(out,7,9,0,0,1));CHECK(!elden_camera_native_clock(out,7,9,0,EldenCamClockCci0,37500000,1));
        uint32_t hz=0;CHECK(!elden_camera_native_clock_observe(out,7,9,EldenCamClockCci0,&hz)&&hz==37500000);
        CHECK(elden_camera_native_clock(out,7,10,0,EldenCamClockCci0,37500000,0)==-116);
        CHECK(clock_write(out,EldenCamCamcc,0,1)==-13);CHECK(clock_write(out,EldenCamTlmm,126*0x1000,0)==-13);
        CHECK(clock_write(out,EldenCamTlmm,113*0x1000+8,0)==-13);CHECK(elden_camera_native_clocks_stop(out,7,9)==-16);
        CHECK(!elden_camera_native_clock(out,7,9,0,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_native_domain(out,7,9,0,0,0));
        CHECK(!elden_camera_native_clocks_stop(out,7,9));EldenCameraClockStatus s{};CHECK(!elden_camera_native_clocks_status(out,&s)&&s.stopped);
    }
    std::free(out);for(auto *p:locks)delete p;std::printf("PASS native camera clocks %s: %u checks; original core/adapter, MMIO/IOKit/physicalowners modeled\n",test.c_str(),checks);return 0;
}
