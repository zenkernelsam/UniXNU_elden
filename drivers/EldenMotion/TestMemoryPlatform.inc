/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* IOKit/cache/PAR peers for the composed actual memory/file/RPC test. No
 * physical-address fixture or mocked success is compiled into native input. */
using IOOptionBits=uint32_t;using IOByteCount=uint64_t;using IOReturn=int;
enum {kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x100,kIOMemoryMapperNone=0x200,kIOReturnSuccess=0,
      kIOMapAnywhere=1,kIOMapUnique=2,kIOMapPrefault=4,kIOMapWriteCombineCache=8};
static void *kernel_task=(void *)1;
class IOBufferMemoryDescriptor;
class IOMemoryMap {public:IOBufferMemoryDescriptor *memory;uintptr_t getVirtualAddress();};
static std::vector<IOBufferMemoryDescriptor *> memory_descriptors;static std::vector<IOMemoryMap *> memory_aliases;
static unsigned memory_cleans,memory_withdraws,memory_installs,memory_reserves;static int memory_observe_error,memory_withdraw_error;
class IOBufferMemoryDescriptor {public:
    uint8_t *cpu=nullptr;uint64_t pa=0;uint32_t bytes=0,index=0;bool prepared=false,nc=false;
    static IOBufferMemoryDescriptor *inTaskWithOptions(void *task,IOOptionBits flags,uint64_t n,uint64_t alignment){
        CHECK(task==kernel_task&&flags==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone)&&alignment==4096);
        auto *p=new IOBufferMemoryDescriptor;p->index=(unsigned)memory_descriptors.size();p->bytes=(uint32_t)n;
        p->pa=p->index?UINT64_C(0x980000000):UINT64_C(0x900000000);CHECK(!posix_memalign((void **)&p->cpu,4096,(size_t)n));memory_descriptors.push_back(p);return p;
    }
    IOMemoryMap *createMappingInTask(void *task,unsigned offset,unsigned options){
        CHECK(task==kernel_task&&!offset&&options==(kIOMapAnywhere|kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache)&&prepared&&memory_cleans==1);
        auto *p=new IOMemoryMap;p->memory=this;nc=true;memory_aliases.push_back(p);return p;
    }
};
uintptr_t IOMemoryMap::getVirtualAddress(){return (uintptr_t)memory->cpu;}
static uintptr_t elden_sensor_memory_virtual(IOMemoryMap *m){return m->getVirtualAddress();}
static IOReturn elden_sensor_memory_prepare(IOBufferMemoryDescriptor *p,IOOptionBits options){CHECK(p&&options==3&&!p->prepared);p->prepared=true;return 0;}
static void *elden_sensor_memory_bytes(IOBufferMemoryDescriptor *p){CHECK(p&&p->prepared);return p->cpu;}
static uint64_t elden_sensor_memory_segment(IOBufferMemoryDescriptor *p,IOByteCount offset,IOByteCount *bytes,IOOptionBits mapper){
    CHECK(p&&p->prepared&&offset<p->bytes&&bytes&&mapper==kIOMemoryMapperNone);*bytes=p->bytes-offset;return p->pa+offset;
}
static uint64_t sensor_memory_translate(uintptr_t address,bool write){
    (void)write;for(auto *p:memory_descriptors)if(address>=(uintptr_t)p->cpu&&address-(uintptr_t)p->cpu<p->bytes)
        return (p->pa+address-(uintptr_t)p->cpu)|((uint64_t)(p->nc?0x44:0xff)<<56);
    CHECK(false);return 1;
}
static uint32_t sensor_memory_cache_line(){return 64;}
static void sensor_memory_clean(void *p,uint64_t n,uint32_t line){CHECK(p&&n&&line==64);memory_cleans++;}
static void sensor_memory_barrier(){CHECK(memory_aliases.size()==1&&memory_installs==1);}
#include "NativeMemoryMethods.inc"
static void memory_test_reset(){
    for(auto *p:memory_aliases)delete p;memory_aliases.clear();for(auto *p:memory_descriptors){std::free(p->cpu);delete p;}memory_descriptors.clear();
    memory_cleans=memory_withdraws=memory_installs=memory_reserves=0;memory_observe_error=memory_withdraw_error=0;
}
