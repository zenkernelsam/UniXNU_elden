/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual NativeMemoryMethods.inc plus original ARM4K build/audit; IOKit,
 * physical/cache/PAR/SMMU owner are explicit peers, never hardware evidence. */
#include "NativeMemory.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks,scenarios;
#define CHECK(v) do{checks++;if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using IOOptionBits=uint32_t;using IOByteCount=uint64_t;using IOReturn=int;using IOLock=std::mutex;
enum {kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x100,kIOMemoryMapperNone=0x200,kIOReturnSuccess=0,
      kIOMapAnywhere=1,kIOMapUnique=2,kIOMapPrefault=4,kIOMapWriteCombineCache=8};
static void *kernel_task=(void *)1;static bool irq;static bool ml_at_interrupt_context(){return irq;}
static std::vector<void *> heaps;static std::vector<IOLock *> locks;
static void *IOMallocData(size_t n){void *p=std::calloc(1,n);if(p)heaps.push_back(p);return p;}
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}static bool IOLockTryLock(IOLock *p){return p->try_lock();}
class IOBufferMemoryDescriptor;class IOMemoryMap {public:IOBufferMemoryDescriptor *memory;uintptr_t getVirtualAddress();};
static unsigned allocated,fail_allocation,fail_prepare,cleans,translations,maps;
static bool split_segment,bad_gap,bad_attr,stale_alias,lazy_alias,fail_map,overlap;
static uint32_t line=64;
static std::vector<IOBufferMemoryDescriptor *> descriptors;static std::vector<IOMemoryMap *> aliases;
class IOBufferMemoryDescriptor {public:
    uint8_t *cpu=nullptr;uint64_t pa=0;uint32_t bytes=0,index=0;bool prepared=false,nc=false,present=true;
    static IOBufferMemoryDescriptor *inTaskWithOptions(void *task,IOOptionBits flags,uint64_t n,uint64_t align){
        CHECK(task==kernel_task&&flags==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone)&&align==4096&&n<=64u<<20);
        unsigned index=allocated++;if(index+1==fail_allocation)return nullptr;
        auto *p=new IOBufferMemoryDescriptor;p->index=index;p->bytes=(uint32_t)n;p->pa=index? (overlap?UINT64_C(0x900001000):UINT64_C(0x980000000)):UINT64_C(0x900001000);
        CHECK(!posix_memalign((void **)&p->cpu,4096,(size_t)n));descriptors.push_back(p);return p;
    }
    IOMemoryMap *createMappingInTask(void *task,unsigned off,unsigned flags){
        CHECK(task==kernel_task&&!off&&flags==(kIOMapAnywhere|kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache)&&prepared&&cleans==1);
        if(fail_map)return nullptr;auto *p=new IOMemoryMap;p->memory=this;nc=!stale_alias||(flags&kIOMapUnique);present=!lazy_alias||(flags&kIOMapPrefault);aliases.push_back(p);maps++;return p;
    }
};
uintptr_t IOMemoryMap::getVirtualAddress(){return (uintptr_t)memory->cpu;}
static uintptr_t elden_sensor_memory_virtual(IOMemoryMap *m){return m->getVirtualAddress();}
static IOReturn elden_sensor_memory_prepare(IOBufferMemoryDescriptor *m,IOOptionBits dir){CHECK(m&&dir==3&&!m->prepared);if(m->index+1==fail_prepare)return -5;m->prepared=true;return 0;}
static void *elden_sensor_memory_bytes(IOBufferMemoryDescriptor *m){CHECK(m&&m->prepared);return m->cpu;}
static uint64_t elden_sensor_memory_segment(IOBufferMemoryDescriptor *m,IOByteCount off,IOByteCount *span,IOOptionBits mapper){
    CHECK(m&&m->prepared&&off<m->bytes&&span&&mapper==kIOMemoryMapperNone);
    *span=m->bytes-off;if(split_segment&&m->index==0){uint64_t boundary=(2u<<20)-(off%(2u<<20));if(*span>boundary)*span=boundary;}
    return m->pa+off+(bad_gap&&m->index==0&&off>=2u<<20?4096:0);
}
static uint64_t sensor_memory_translate(uintptr_t va,bool write){
    (void)write;translations++;
    for(auto *m:descriptors)if(va>=(uintptr_t)m->cpu&&va-(uintptr_t)m->cpu<m->bytes){
        uint64_t off=va-(uintptr_t)m->cpu;if(m->nc&&!m->present)return 0x80d;
        unsigned attr=m->nc?(bad_attr?0xff:0x44):0xff;return m->pa+off|((uint64_t)attr<<56);
    }CHECK(false);return 1;
}
static uint32_t sensor_memory_cache_line(){return line;}
static void sensor_memory_clean(void *p,uint64_t n,uint32_t size){CHECK(p&&n&&(n&4095)==0&&size==64);cleans++;}
static void sensor_memory_barrier(){CHECK(maps==1);}
#include "NativeMemoryMethods.inc"
struct OwnerPeer {
    unsigned reserved=0,installed=0,observed=0,withdrawn=0;int reserve_error=0,install_error=0,observe_error=0,withdraw_error=0;
    uint32_t bank=7,asid=9,id0=0x4c017e84,id2=0x5111;uint64_t route_epoch=11;int admission_error=0;
};
static OwnerPeer *peer;
static int admission(void *p,uint64_t owner,uint64_t epoch,int cleanup){CHECK(p==peer&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return peer->admission_error;}
static int reserve(void *p,uint64_t owner,uint64_t epoch,const EldenSensorMemoryConfig *cfg,EldenSensorMemoryLease *out){CHECK(p==peer&&owner==7&&epoch==9&&cfg&&cfg->session==4&&cfg->iova==0x20001000&&cfg->bytes==(8u<<20));peer->reserved++;if(peer->reserve_error)return peer->reserve_error;*out={peer->id0,peer->id2,peer->asid,peer->bank,peer->route_epoch};return 0;}
static int install(void *p,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *view){
    CHECK(p==peer&&owner==7&&epoch==9&&view->lease.bank==peer->bank&&view->lease.asid==peer->asid&&view->memory.iova==0x20001000);
    CHECK(view->memory.arena&&view->memory.bytes==(8u<<20)&&view->data_physical==UINT64_C(0x900001000)&&view->table_physical==UINT64_C(0x980000000));
    CHECK(view->tables.leaf_pages==2048&&view->tables.table_pages==7&&view->table_bytes==7*4096&&view->context.cbar==0x1f300);
    CHECK(view->stream_ids[0]==0x1004&&view->stream_ids[1]==0x1044&&view->stream_ids[2]==0x1064&&view->stream_ids[3]==0x1084);
    CHECK(cleans==2);peer->installed++;return peer->install_error;
}
static int observe(void *p,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *view,int cleanup){CHECK(p==peer&&owner==7&&epoch==9&&view->lease.route_epoch==11&&!cleanup);peer->observed++;return peer->observe_error;}
static int withdraw(void *p,uint64_t owner,uint64_t epoch,const EldenSensorMemoryView *){CHECK(p==peer&&owner==7&&epoch==9);if(peer->withdraw_error)return peer->withdraw_error;peer->withdrawn++;return 0;}
static void reset(){for(auto *p:aliases)delete p;aliases.clear();for(auto *p:descriptors){std::free(p->cpu);delete p;}descriptors.clear();for(auto *p:locks)delete p;locks.clear();for(void *p:heaps)std::free(p);heaps.clear();
    allocated=fail_allocation=fail_prepare=cleans=translations=maps=0;split_segment=bad_gap=bad_attr=stale_alias=lazy_alias=fail_map=overlap=irq=false;line=64;scenarios++;}
static int open(OwnerPeer &p,EldenSensorNativeMemory **out){peer=&p;EldenSensorMemoryConfig cfg{0x20001000,8u<<20,4,4242};EldenSensorMemoryOwner own{&p,7,9,admission,reserve,install,observe,withdraw};return elden_sensor_memory_native_open(&cfg,&own,out);}
int main(){
    reset();OwnerPeer p;EldenSensorNativeMemory *m=nullptr;CHECK(!open(p,&m)&&m&&p.installed==1&&p.observed==1);EldenSensorMemoryView view{};CHECK(!elden_sensor_memory_native_view(m,7,9,&view)&&view.memory.iova==0x20001000);
    CHECK(!elden_sensor_memory_native_publish(m,7,9));p.withdraw_error=-16;CHECK(elden_sensor_memory_native_stop(m,7,9)==-16&&m->data_descriptor&&m->table_descriptor&&m->alias);p.withdraw_error=0;CHECK(!elden_sensor_memory_native_stop(m,7,9)&&p.withdrawn==1);CHECK(!elden_sensor_memory_native_stop(m,7,9)&&p.withdrawn==1);CHECK(elden_sensor_memory_native_view(m,7,9,&view)<0);
    for(unsigned bad=0;bad<11;bad++){
        reset();OwnerPeer q;m=nullptr;
        if(bad==0)fail_allocation=1;if(bad==1)fail_allocation=2;if(bad==2)fail_prepare=1;if(bad==3)fail_prepare=2;
        if(bad==4)bad_attr=true;if(bad==5)bad_gap=true;if(bad==6)overlap=true;if(bad==7)fail_map=true;
        if(bad==8)q.install_error=-16;if(bad==9)q.observe_error=-71;if(bad==10)q.bank=4;
        CHECK(open(q,&m)<0&&m&&!m->ready);CHECK(q.installed==0||bad==8||bad==9);if(bad<8)CHECK(q.installed==0);
    }
    reset();OwnerPeer split;m=nullptr;split_segment=stale_alias=lazy_alias=true;CHECK(!open(split,&m)&&m->ready&&maps==1);CHECK(translations==2*(2048+7+2048));
    reset();OwnerPeer corrupt;m=nullptr;CHECK(!open(corrupt,&m));((uint64_t *)m->tables)[0]=3;CHECK(sensor_memory_audit(m)<0&&!m->ready);
    reset();OwnerPeer unknown;m=nullptr;unknown.id2=0;CHECK(open(unknown,&m)==-95&&allocated==0);
    reset();OwnerPeer lost;m=nullptr;CHECK(!open(lost,&m));lost.observe_error=-71;CHECK(elden_sensor_memory_native_publish(m,7,9)==-71&&!m->ready);CHECK(elden_sensor_memory_native_admit(m,7,9,0)==-19);
    reset();OwnerPeer gate_lost;m=nullptr;CHECK(!open(gate_lost,&m));gate_lost.admission_error=-13;CHECK(elden_sensor_memory_native_admit(m,7,9,0)==-13);
    reset();OwnerPeer invalid;m=nullptr;CHECK(!open(invalid,&m));CHECK(elden_sensor_memory_native_admit(m,7,10,0)==-116&&elden_sensor_memory_native_admit(m,7,9,2)==-22);
    unsigned count=scenarios;reset();std::printf("PASS %u actual sensor native allocation/PAR/table/owner/lifetime scenarios, %u checks; IOKit/cache/PAR/MMIO peers modeled\n",count,checks);return 0;
}
