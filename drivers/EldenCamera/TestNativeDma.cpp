/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual allocator + original4K builder/audit, independent physical peer. */
#include "NativeDma.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <vector>
static unsigned checks,scenarios;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"dma line%u: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using IOOptionBits=uint32_t;using IOByteCount=uint64_t;using IOReturn=int;using IOLock=std::mutex;
enum {kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x100,kIOMemoryMapperNone=0x200,kIOReturnSuccess=0,
      kIOMapAnywhere=1,kIOMapUnique=2,kIOMapPrefault=4,kIOMapWriteCombineCache=8};
static void *kernel_task=(void *)1;static bool irq;static bool ml_at_interrupt_context(){return irq;}
static std::vector<void *> heaps;static std::vector<IOLock *> locks;
static void *IOMallocData(size_t bytes){void *p=std::calloc(1,bytes);if(p)heaps.push_back(p);return p;}
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}static bool IOLockTryLock(IOLock *p){return p->try_lock();}
class IOBufferMemoryDescriptor;class IOMemoryMap {public:IOBufferMemoryDescriptor *memory;uintptr_t getVirtualAddress();};
static unsigned allocated,fail_allocation,fail_prepare,cleans,translations,maps;static uint32_t line=64;
static bool split_segment,bad_gap,bad_attr,stale_alias,lazy_alias,fail_map,overlap,read_fault,write_fault;
static std::vector<IOBufferMemoryDescriptor *> descriptors;static std::vector<IOMemoryMap *> aliases;
class IOBufferMemoryDescriptor {public:
    uint8_t *cpu=nullptr;uint64_t pa=0;uint32_t bytes=0,index=0;bool prepared=false,nc=false,present=true;
    static IOBufferMemoryDescriptor *inTaskWithOptions(void *task,IOOptionBits flags,uint64_t bytes,uint64_t align){
        CHECK(task==kernel_task&&flags==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone)&&align==4096&&bytes<=128u<<20);
        unsigned index=allocated++;if(index+1==fail_allocation)return nullptr;
        auto *p=new IOBufferMemoryDescriptor;p->index=index;p->bytes=(uint32_t)bytes;p->pa=index?(overlap?UINT64_C(0x900001000):UINT64_C(0x980000000)):UINT64_C(0x900001000);
        CHECK(!posix_memalign((void **)&p->cpu,4096,(size_t)bytes));std::memset(p->cpu,0xcc,(size_t)bytes);descriptors.push_back(p);return p;
    }
    IOMemoryMap *createMappingInTask(void *task,unsigned off,unsigned flags){
        CHECK(task==kernel_task&&!off&&flags==(kIOMapAnywhere|kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache)&&prepared&&cleans==1);
        if(fail_map)return nullptr;auto *p=new IOMemoryMap;p->memory=this;nc=!stale_alias||(flags&kIOMapUnique);present=!lazy_alias||(flags&kIOMapPrefault);aliases.push_back(p);maps++;return p;
    }
};
uintptr_t IOMemoryMap::getVirtualAddress(){return (uintptr_t)memory->cpu;}
static uintptr_t elden_camera_dma_virtual(IOMemoryMap *m){return m->getVirtualAddress();}
static IOReturn elden_camera_dma_prepare(IOBufferMemoryDescriptor *m,IOOptionBits direction){CHECK(m&&direction==3&&!m->prepared);if(m->index+1==fail_prepare)return -5;m->prepared=true;return 0;}
static void *elden_camera_dma_bytes(IOBufferMemoryDescriptor *m){CHECK(m&&m->prepared);return m->cpu;}
static uint64_t elden_camera_dma_segment(IOBufferMemoryDescriptor *m,IOByteCount offset,IOByteCount *span,IOOptionBits mapper){
    CHECK(m&&m->prepared&&offset<m->bytes&&span&&mapper==kIOMemoryMapperNone);*span=m->bytes-offset;
    if(split_segment&&!m->index){uint64_t boundary=(2u<<20)-(offset%(2u<<20));if(*span>boundary)*span=boundary;}
    return m->pa+offset+(bad_gap&&!m->index&&offset>=2u<<20?4096:0);
}
static uint64_t camera_dma_translate(uintptr_t address,bool write){translations++;
    if(write?write_fault:read_fault)return 0x80d;
    for(auto *m:descriptors)if(address>=(uintptr_t)m->cpu&&address-(uintptr_t)m->cpu<m->bytes){
        uint64_t off=address-(uintptr_t)m->cpu;if(m->nc&&!m->present)return 0x80d;
        unsigned attr=m->nc?(bad_attr?0xff:0x44):0xff;return m->pa+off|((uint64_t)attr<<56);
    }CHECK(false);return 1;
}
static uint32_t camera_dma_cache_line(){return line;}
static void camera_dma_clean(void *p,uint64_t bytes,uint32_t size){CHECK(p&&bytes&&(bytes&4095)==0&&size==64);cleans++;}
static void camera_dma_barrier(){CHECK(maps==1);}
#include "NativeDmaMethods.inc"
struct Owner {
    unsigned reserved=0,installed=0,observed=0,retired=0,completion=0,early=0,early_fail_call=0;
    int admit_error=0,reserve_error=0,install_error=0,observe_error=0,retire_error=0,early_error=0;
    uint32_t bank=8,slot=9,asid=9,id0=0x4c017e84,id1=0x60000053,id2=0x5111;uint64_t route_epoch=11;
};
static int admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return ((Owner *)v)->admit_error;}
static int early(void *v,uint64_t owner,uint64_t epoch){auto *p=(Owner *)v;CHECK(owner==7&&epoch==9);p->early++;return p->early==p->early_fail_call?-16:p->early_error;}
static int reserve(void *v,uint64_t owner,uint64_t epoch,const EldenCameraDmaConfig *cfg,uint64_t bytes,EldenCameraDmaLease *out){auto *p=(Owner *)v;
    CHECK(owner==7&&epoch==9&&cfg&&bytes<128u<<20&&cfg->iova&&out);p->reserved++;
    if(p->reserve_error)return p->reserve_error;*out={p->id0,p->id1,p->id2,p->bank,p->slot,p->asid,p->route_epoch};return 0;}
static int install(void *v,uint64_t owner,uint64_t epoch,const EldenCameraDmaView *view){auto *p=(Owner *)v;CHECK(owner==7&&epoch==9&&view&&view->count>=2&&view->stream_id==0x1c00);
    CHECK(cleans==2&&view->data_physical==UINT64_C(0x900001000)&&view->table_physical==UINT64_C(0x980000000));
    CHECK(view->table_config.input_bits==32&&view->table_config.output_bits==36&&view->context.cbar==0x1f300&&view->context.cba2r==1);
    CHECK(view->tables.leaf_pages&&view->tables.table_pages&&view->lease.slot==p->slot&&view->lease.bank==p->bank);p->installed++;return p->install_error;}
static int observe(void *v,uint64_t owner,uint64_t epoch,const EldenCameraDmaView *view,const EldenCameraBuffer *buffer,int completion){auto *p=(Owner *)v;
    CHECK(owner==7&&epoch==9&&view->lease.route_epoch==p->route_epoch&&(completion==0||completion==1));CHECK(!completion||buffer);p->observed++;p->completion+=completion;return p->observe_error;}
static int retire(void *v,uint64_t owner,uint64_t epoch,const EldenCameraDmaView *view,unsigned reservation,unsigned installation){auto *p=(Owner *)v;
    CHECK(owner==7&&epoch==9&&view&&reservation<=1&&installation<=1&&installation<=reservation);if(p->retire_error)return p->retire_error;p->retired++;return 0;}
static int prepared(void *v,uint64_t owner,uint64_t epoch,const EldenCameraDmaView *view){return observe(v,owner,epoch,view,nullptr,0);}
static void reset(){for(auto *p:aliases)delete p;aliases.clear();for(auto *p:descriptors){std::free(p->cpu);delete p;}descriptors.clear();for(auto *p:locks)delete p;locks.clear();for(auto *p:heaps)std::free(p);heaps.clear();
    allocated=fail_allocation=fail_prepare=cleans=translations=maps=camera_dma_attempted=0;
    split_segment=bad_gap=bad_attr=stale_alias=lazy_alias=fail_map=overlap=irq=read_fault=write_fault=false;line=64;scenarios++;}
static int open(Owner &p,EldenCameraNativeDma **out,uint32_t rear=2,uint32_t front=2,uint64_t iova=0x20000000){
    EldenCameraDmaConfig cfg{iova,{rear,front}};EldenCameraDmaOwner owner{&p,7,9,admit,early,reserve,install,observe,retire,nullptr};return elden_camera_dma_open(&cfg,&owner,out);}
static uint64_t leaf(EldenCameraNativeDma *m,uint64_t iova){uint64_t table=m->view.table_physical;
    for(unsigned level=m->view.tables.start_level;level<4;level++){uint64_t desc=0;unsigned shift=12+9*(3-level);uint64_t addr=table+((iova>>shift)&511)*8;
        CHECK(!m->io.read_descriptor(m->io.cookie,addr,&desc));if(!desc)return 0;if(level==3)return desc;CHECK((desc&3)==3);table=desc&UINT64_C(0x0000fffffffff000);}
    CHECK(false);return 0;}
static void staged(){reset();Owner o;EldenCameraNativeDma *m=nullptr;EldenCameraDmaConfig cfg{0x20000000,{2,2}};
    EldenCameraDmaOwner owner{&o,7,9,admit,early,reserve,install,observe,retire,prepared};
    CHECK(!elden_camera_dma_prepare_batch(&cfg,&owner,&m)&&m&&!m->ready&&m->prepared);
    EldenCameraDmaView view{};CHECK(elden_camera_dma_view(m,7,9,&view)==-19&&elden_camera_dma_admit(m,7,9,0)==-19);
    CHECK(!elden_camera_dma_activate(m,7,9)&&m->ready&&m->status.stage==11);CHECK(elden_camera_dma_activate(m,7,9)==-16);
    CHECK(!elden_camera_dma_view(m,7,9,&view));CHECK(!elden_camera_dma_stop(m,7,9));CHECK(elden_camera_dma_activate(m,7,9)==-125);
    reset();Owner fail;m=nullptr;owner={&fail,7,9,admit,early,reserve,install,observe,retire,prepared};
    CHECK(!elden_camera_dma_prepare_batch(&cfg,&owner,&m));fail.observe_error=-71;
    CHECK(elden_camera_dma_activate(m,7,9)==-71&&!m->ready);fail.observe_error=0;CHECK(elden_camera_dma_activate(m,7,9)==-71);
}
static void normal(){reset();Owner o;EldenCameraNativeDma *m=nullptr;CHECK(!open(o,&m)&&m&&o.installed==1&&o.early>=3);EldenCameraDmaView view{};CHECK(!elden_camera_dma_view(m,7,9,&view)&&view.count==4);
    CHECK(!leaf(m,view.iova));for(unsigned i=0;i<view.count;i++){const auto &s=view.slots[i];
        CHECK(s.buffer.iova<UINT64_C(0x100000000)&&s.physical>UINT64_C(0x800000000)&&s.mapped_bytes>=s.buffer.bytes);
        CHECK((leaf(m,s.buffer.iova)&UINT64_C(0x0000fffffffff000))==s.physical);
        CHECK(!leaf(m,s.buffer.iova+s.mapped_bytes));CHECK(!leaf(m,s.buffer.iova-4096));}
    auto buffer=view.slots[0].buffer;unsigned sensor=view.slots[0].sensor;CHECK(elden_camera_dma_buffer(m,7,9,sensor,&buffer,1)==-16);
    CHECK(!elden_camera_dma_buffer(m,7,9,sensor,&buffer,0));CHECK(elden_camera_dma_buffer(m,7,9,sensor,&buffer,0)==-16);
    CHECK(!elden_camera_dma_buffer(m,7,9,sensor,&buffer,1));EldenCameraFrame f{buffer,1,1000,(uint32_t)buffer.bytes,0};
    CHECK(!elden_camera_dma_claim_frame(m,7,9,sensor,&f));CHECK(elden_camera_dma_buffer(m,7,9,sensor,&buffer,0)==-16);
    auto bad=f;bad.sequence++;CHECK(elden_camera_dma_release_frame(m,7,9,sensor,&bad)==-116);CHECK(!elden_camera_dma_release_frame(m,7,9,sensor,&f));
    CHECK(!elden_camera_dma_buffer(m,7,9,sensor,&buffer,0));EldenCameraDmaStatus status{};CHECK(!elden_camera_dma_status(m,&status)&&status.device_owned==1);
    o.retire_error=-16;CHECK(elden_camera_dma_stop(m,7,9)==-16&&m->data_descriptor&&m->table_descriptor&&m->alias);o.retire_error=0;
    CHECK(!elden_camera_dma_stop(m,7,9)&&o.retired==1);CHECK(!elden_camera_dma_stop(m,7,9)&&o.retired==1);CHECK(elden_camera_dma_view(m,7,9,&view)<0);
    CHECK(!elden_camera_dma_status(m,&status)&&status.retired&&!status.device_owned&&!status.completed);
}
static void refusal(){for(unsigned which=0;which<16;which++){reset();Owner o;EldenCameraNativeDma *m=nullptr;
    if(which==0)fail_allocation=1;if(which==1)fail_allocation=2;if(which==2)fail_prepare=1;if(which==3)fail_prepare=2;
    if(which==4)bad_attr=true;if(which==5)bad_gap=true;if(which==6)overlap=true;if(which==7)fail_map=true;
    if(which==8)o.install_error=-16;if(which==9)o.observe_error=-71;if(which==10)o.bank=4;if(which==11)o.slot=7;
    if(which==12)o.id1=0;if(which==13)o.asid=256;if(which==14)read_fault=true;if(which==15)write_fault=true;
    CHECK(open(o,&m)<0&&m&&!m->ready);if(which<8||which>9)CHECK(!o.installed);
    CHECK(!elden_camera_dma_stop(m,7,9));}
    reset();Owner early_bad;EldenCameraNativeDma *m=nullptr;early_bad.early_error=-16;CHECK(open(early_bad,&m)==-16&&!m&&!allocated);CHECK(!camera_dma_attempted);
    reset();Owner split;split_segment=stale_alias=lazy_alias=true;CHECK(!open(split,&m)&&m->ready&&maps==1);CHECK(translations>24000);
    reset();Owner corrupt;m=nullptr;CHECK(!open(corrupt,&m));((uint64_t *)m->tables)[0]=3;CHECK(camera_dma_audit(m)<0&&!m->ready);
    reset();Owner lost;m=nullptr;CHECK(!open(lost,&m));lost.observe_error=-71;auto b=m->view.slots[0].buffer;
    CHECK(elden_camera_dma_buffer(m,7,9,0,&b,0)==-71&&m->slot_state[0]==1&&!m->ready);CHECK(!elden_camera_dma_stop(m,7,9));
    reset();Owner wrong;m=nullptr;CHECK(!open(wrong,&m));auto bad=m->view.slots[0].buffer;bad.mapping=(void *)1;
    unsigned n=wrong.observed;CHECK(elden_camera_dma_buffer(m,7,9,0,&bad,0)==-116&&wrong.observed==n);
    CHECK(elden_camera_dma_admit(m,7,10,0)==-116&&elden_camera_dma_admit(m,7,9,2)==-22);
    for(unsigned call=2;call<=3;call++){reset();Owner late;late.early_fail_call=call;m=nullptr;CHECK(open(late,&m)==-16&&m&&!late.installed);
        CHECK(!m->status.install_attempted);if(call==3)CHECK(m->status.reservation_attempted&&allocated==2&&m->alias);
        CHECK(!elden_camera_dma_stop(m,7,9));}
    reset();Owner borrowed;m=nullptr;CHECK(!open(borrowed,&m));auto retained=m->view.slots[0].buffer;
    CHECK(!elden_camera_dma_buffer(m,7,9,0,&retained,0)&&!elden_camera_dma_buffer(m,7,9,0,&retained,1));EldenCameraFrame f{retained,7,1000,(uint32_t)retained.bytes,0};
    CHECK(!elden_camera_dma_claim_frame(m,7,9,0,&f));CHECK(!elden_camera_dma_stop(m,7,9));EldenCameraDmaStatus status;
    CHECK(!elden_camera_dma_status(m,&status)&&status.retired&&status.borrowed==1&&!status.device_owned);
    CHECK(!elden_camera_dma_release_frame(m,7,9,0,&f));CHECK(elden_camera_dma_buffer(m,7,9,0,&retained,0)==-19);
}
static void geometry(){for(unsigned rear=0;rear<=4;rear++)for(unsigned front=0;front<=4;front++){
    reset();Owner o;EldenCameraNativeDma *m=nullptr;int rc=open(o,&m,rear,front,0x3fffd000);
    bool valid=(rear==0||rear>=2)&&(front==0||front>=2)&&(rear+front>=2);
    CHECK(valid?rc==0:rc<0);if(!valid){CHECK(!m&&!allocated&&!camera_dma_attempted);continue;}
    CHECK(m->view.count==rear+front&&m->view.tables.table_pages>2);CHECK(!leaf(m,m->view.iova));
    for(unsigned i=0;i<m->view.count;i++)CHECK(!leaf(m,m->view.slots[i].buffer.iova+m->view.slots[i].mapped_bytes));
    CHECK(!elden_camera_dma_stop(m,7,9));}
    reset();Owner o;EldenCameraNativeDma *m=nullptr;CHECK(open(o,&m,2,2,0xfffff000)<0&&!m);CHECK(open(o,&m,2,2,0x20000001)<0&&!m);
}
#if defined(ELDEN_CAMERA_DMA_PEER_API)
static Owner capture_owner;
extern "C" EldenCameraNativeDma *elden_camera_dma_peer_open(void){reset();capture_owner=Owner{};EldenCameraNativeDma *out=nullptr;CHECK(!open(capture_owner,&out,3,3));return out;}
extern "C" void elden_camera_dma_peer_fault(unsigned fault){
    capture_owner.observe_error=fault==1?-71:0;capture_owner.retire_error=fault==2?-16:0;
    capture_owner.admit_error=fault==3?-13:0;
}
extern "C" unsigned elden_camera_dma_peer_completion_count(void){return capture_owner.completion;}
extern "C" void elden_camera_dma_peer_reset(void){reset();}
extern "C" int elden_camera_dma_peer_prepare(const EldenCameraDmaConfig *cfg,const EldenCameraDmaOwner *owner,EldenCameraNativeDma **out){reset();return elden_camera_dma_prepare_batch(cfg,owner,out);}
extern "C" int elden_camera_dma_peer_backing(const EldenCameraDmaView *view){
    if(!view||descriptors.size()!=2||!descriptors[0]->prepared||!descriptors[1]->prepared||!descriptors[0]->nc||aliases.size()!=1)return -13;
    if(view->data_physical!=descriptors[0]->pa||view->table_physical!=descriptors[1]->pa||view->table_mapping!=(const volatile uint64_t *)descriptors[1]->cpu||
       view->allocation_bytes!=descriptors[0]->bytes||view->table_bytes!=descriptors[1]->bytes)return -116;return 0;
}
#endif
int main(){staged();normal();refusal();geometry();unsigned count=scenarios;reset();std::printf("PASS camera native DMA: %u scenarios/%u checks; original4K/native methods, IOKit/cache/PAR/route/physicalDMA peers modeled\n",count,checks);return 0;}
