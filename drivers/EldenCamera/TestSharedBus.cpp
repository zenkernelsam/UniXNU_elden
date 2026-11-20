/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual shared methods -> original GENI Platform + core + WL power session. */
#include "Power.h"
#include "Platform.h"
#include "Aw99706.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"shared line %u: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using IOLock=std::mutex;
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static bool irq_mode;static bool ml_at_interrupt_context(){return irq_mode;}
static bool allocation_fail;static void *IOMalloc(size_t size){return allocation_fail?nullptr:std::calloc(1,size);}
namespace {
static unsigned lease=1;
struct Context{EldenBlPlatform platform{};IOLock *serial=nullptr;uint32_t shared_camera_refs=0;bool shared_closing=false,shared_published=false,shared_released=false;};
}
#include "SharedI2c10Methods.inc"
extern "C" {void *elden_camera_i2c10_peer_create(EldenBlPlatform *);unsigned elden_camera_i2c10_peer_stat(void *,unsigned);
    void elden_camera_i2c10_peer_fault(void *,unsigned);void elden_camera_i2c10_peer_destroy(void *);}
struct Authority{std::atomic<int> deny{0},cleanup{0},retired{0};};
static int admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){auto *p=(Authority *)v;if(owner!=7||epoch!=9||(cleanup!=0&&cleanup!=1))return -116;
    return (cleanup?p->cleanup.load():p->deny.load())?-13:0;}
static int retired(void *v,uint64_t owner,uint64_t epoch){if(owner!=7||epoch!=9)return -116;return ((Authority *)v)->retired.load()?-16:0;}
static int inactive(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){if(sensor>1)return -22;return retired(v,owner,epoch);}
static int delay(void *,unsigned us){return us==150?0:-22;}
static int unused(void *v,uint64_t owner,uint64_t epoch,unsigned ldo){if(ldo<1||ldo>7)return -22;return retired(v,owner,epoch);}
int main(int argc,char **argv){CHECK(argc==2);std::string mode=argv[1];Context c;IOLock mutex;c.serial=&mutex;void *peer=elden_camera_i2c10_peer_create(&c.platform);
    CHECK(!elden_bl_platform_acquire(&c.platform));shared_bus_publish(&c);Authority authority;
    EldenCameraI2c10Authority a{&authority,7,9,admit,retired};EldenCameraI2c10Port *port=nullptr;int rc=0;
    if(mode=="power-session"||mode=="power-id-fail"){
        EldenCameraPowerSession session{};EldenCameraPowerOwner owner{&authority,7,9,admit,inactive,delay,unused};
        if(mode=="power-id-fail")elden_camera_i2c10_peer_fault(peer,1);
        int opened=elden_camera_power_session_open(&session,&owner);port=session.bus.port;
        if(mode=="power-id-fail"){CHECK(opened<0&&session.initialized&&!elden_camera_i2c10_peer_stat(peer,2));
            CHECK(!elden_camera_power_session_stop(&session));}
        else{
            CHECK(!opened&&port&&session.initialized);CHECK(elden_camera_i2c10_close(port,7,9)==-16);
            const unsigned supplies[]={EldenCameraDovdd,EldenCameraDvdd,EldenCameraAvdd};
            for(unsigned sensor=0;sensor<2;sensor++)for(unsigned r:supplies)CHECK(!elden_camera_power_session_resource(&session,7,9,sensor,r,1));
            CHECK(elden_camera_power_session_stop(&session)==-16&&c.shared_camera_refs);
            CHECK(shared_bus_release(&c)==-16&&lease);
            // Closing blocks public writes; original cleanup still restores all
            // real WL channels through the retained sole controller.
            CHECK(elden_camera_power_session_resource(&session,7,9,0,EldenCameraDovdd,1)==-125);
            for(unsigned sensor=0;sensor<2;sensor++)for(unsigned r:supplies)CHECK(!elden_camera_power_session_resource(&session,7,9,sensor,r,0));
            CHECK(!elden_camera_power_session_stop(&session)&&session.closed&&port->closed&&!lease);
            CHECK(!elden_camera_i2c10_peer_stat(peer,6));
        }
        CHECK(!elden_camera_power_session_stop(&session));if(c.platform.acquired)CHECK(!shared_bus_release(&c));
        goto finish;
    }
    if(mode=="allocation-fail")allocation_fail=true;if(mode=="deny")authority.deny=1;
    if(mode=="irq")irq_mode=true;
    rc=elden_camera_i2c10_open(&a,&port);
    if(mode=="allocation-fail"||mode=="deny"||mode=="irq"){
        CHECK(rc<0&&!port&&!c.shared_camera_refs);irq_mode=false;allocation_fail=false;CHECK(!shared_bus_release(&c)&&!lease);
    }else{
        CHECK(!rc&&port&&c.shared_camera_refs==1);uint8_t value=0;
        CHECK(!elden_camera_i2c10_read(port,7,9,0,&value,0)&&value==0x82&&c.platform.bus.address==0x76);
        unsigned commands=elden_camera_i2c10_peer_stat(peer,0);
        CHECK(elden_camera_i2c10_write(port,7,9,0,0,0)==-22);CHECK(elden_camera_i2c10_read(port,7,9,2,&value,0)==-22);
        CHECK(elden_camera_i2c10_read(port,7,10,0,&value,0)==-116);CHECK(elden_camera_i2c10_peer_stat(peer,0)==commands);
        EldenAw99706 chip{};EldenBacklightIo io{&c,shared_backlight_read,shared_backlight_write};CHECK(!elden_aw99706_inspect(&chip,&io));
        if(mode=="race"){
            std::atomic<unsigned> failures{0};std::thread brightness([&]{for(unsigned i=0;i<400;i++)if(elden_aw99706_set_level(&chip,i%2?49:511))failures++;});
            std::thread camera([&]{for(unsigned i=0;i<400;i++){uint8_t x=0;if(elden_camera_i2c10_read(port,7,9,0,&x,0)||x!=0x82)failures++;}});
            brightness.join();camera.join();CHECK(!failures&&c.platform.bus.address==0x76);
        }else if(mode=="nack"||mode=="cancel"||mode=="abort"){
            elden_camera_i2c10_peer_fault(peer,mode=="nack"?1:mode=="cancel"?2:3);
            CHECK(elden_camera_i2c10_read(port,7,9,0,&value,0)<0&&c.platform.failed);commands=elden_camera_i2c10_peer_stat(peer,0);
            CHECK(shared_backlight_read(&c,0,&value)<0&&elden_camera_i2c10_peer_stat(peer,0)==commands);
            if(mode=="abort"){CHECK(c.platform.quarantined&&elden_camera_i2c10_peer_stat(peer,4));
                CHECK(shared_bus_release(&c)==-16&&lease);CHECK(elden_camera_i2c10_close(port,7,9)<0&&lease&&!c.shared_released);goto finish;}
            CHECK(!c.platform.quarantined);
        }else if(mode=="retirement-fail"){
            authority.retired=1;CHECK(elden_camera_i2c10_close(port,7,9)==-16&&c.shared_camera_refs&&!port->closed);authority.retired=0;
        }else if(mode=="release-error"){
            CHECK(shared_bus_release(&c)==-16);elden_camera_i2c10_peer_fault(peer,4);
            CHECK(elden_camera_i2c10_close(port,7,9)<0&&port->closed&&!c.shared_camera_refs&&lease);CHECK(!c.shared_released);goto finish;
        }
        if(mode=="close-first"){CHECK(!elden_camera_i2c10_close(port,7,9)&&c.platform.acquired);CHECK(!shared_bus_release(&c));}
        else{
            CHECK(shared_bus_release(&c)==-16&&lease&&c.platform.acquired);CHECK(shared_backlight_read(&c,0,&value)==-125);
            if(!c.platform.failed){CHECK(elden_camera_i2c10_read(port,7,9,0,&value,0)==-125);CHECK(!elden_camera_i2c10_read(port,7,9,0,&value,1));}
            CHECK(!elden_camera_i2c10_close(port,7,9));
        }
        CHECK(port->closed&&!c.shared_camera_refs&&!lease&&c.shared_released&&!c.platform.acquired);
        CHECK(elden_camera_i2c10_read(port,7,9,0,&value,1)==-125);CHECK(!elden_camera_i2c10_close(port,7,9));
    }
finish:
    unsigned core_checks=elden_camera_i2c10_peer_stat(peer,5);std::free(port);elden_camera_i2c10_peer_destroy(peer);
    std::printf("PASS shared camera I2C10 %s: %u method + %u actual GENI peer checks; physical power/IOKit modeled\n",mode.c_str(),checks,core_checks);return 0;
}
