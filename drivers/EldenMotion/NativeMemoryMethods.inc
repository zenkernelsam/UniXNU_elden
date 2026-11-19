/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native methods; MMIO/IOKit/cache/PAR providers modeled by host test. */
struct EldenSensorNativeMemory {
    EldenSensorMemoryConfig config;EldenSensorMemoryOwner owner;EldenSensorMemoryView view;EldenSensorMemoryStatus status;
    IOLock *lock;IOBufferMemoryDescriptor *data_descriptor,*table_descriptor;IOMemoryMap *alias;
    uint8_t *data,*tables;uint32_t cache_line,table_capacity,ready,stopping,withdrawn;
    EldenWifiSmmu4KRange range;EldenWifiSmmu4KTableMemory table_memory;EldenWifiSmmu4KTableIo io;
    unsigned char visited[36];
};
namespace {
static bool sensor_memory_owner(EldenSensorNativeMemory *c,uint64_t owner,uint64_t epoch){return c&&c->owner.owner==owner&&c->owner.epoch==epoch;}
static int sensor_memory_admit(EldenSensorNativeMemory *c,int cleanup){
    if(!cleanup&&__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -125;
    int rc=c->owner.admit(c->owner.context,c->owner.owner,c->owner.epoch,cleanup);return rc<=0?rc:-13;
}
static int sensor_memory_error(EldenSensorNativeMemory *c,int code,uint32_t reason){c->status.error=code<0?code:-13;c->status.reason=reason;__atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);return c->status.error;}
static uint32_t sensor_output_bits(uint32_t id0,uint32_t id2){
    const uint32_t widths[]={32,36,40,42,44,48};uint32_t va=id2>>8&15,out=id2>>4&15,mid=id2&15;
    if(!(id0&(1u<<30))||!(id2&(1u<<12))||va>=6||out>=6||mid>=6)return 0;
    return widths[out]<widths[mid]?widths[out]:widths[mid];
}
static int sensor_memory_verify(EldenSensorNativeMemory *c,IOBufferMemoryDescriptor *memory,uint8_t *mapping,uint64_t base,uint32_t bytes,uint32_t attribute){
    if(!memory||!mapping||!base||(base&4095)||((uintptr_t)mapping&4095)||(bytes&4095)||bytes>UINT64_MAX-base||bytes>UINTPTR_MAX-(uintptr_t)mapping)return sensor_memory_error(c,-71,1);
    for(uint64_t offset=0;offset<bytes;offset+=4096){
        int rc=sensor_memory_admit(c,0);if(rc)return sensor_memory_error(c,rc,2);
        IOByteCount span=0;uint64_t physical=elden_sensor_memory_segment(memory,offset,&span,kIOMemoryMapperNone);
        c->status.offset=offset;c->status.physical=physical;c->status.expected_physical=base+offset;c->status.segment_bytes=span;
        if(physical!=base+offset||span<4096)return sensor_memory_error(c,-71,3);
        for(unsigned access=0;access<2;access++){
            uint64_t par=sensor_memory_translate((uintptr_t)mapping+offset,access!=0);
            c->status.access=access;c->status.attribute=attribute;c->status.par=par;
            if((par&1)||((par>>56)&255)!=attribute||(par&UINT64_C(0x0000fffffffff000))!=physical)return sensor_memory_error(c,-71,4);
        }
    }
    return 0;
}
static int sensor_memory_audit(EldenSensorNativeMemory *c){
    EldenWifiSmmu4KAuditRegion region={&c->range,1,c->table_capacity,(size_t)c->table_capacity*512,c->visited,sizeof(c->visited)};
    EldenWifiSmmu4KTableAuditResult report={};EldenWifiSmmu4KStatus rc=EldenWifiSmmu4KAuditTables(&c->io,&c->view.table_config,&c->view.tables,&region,c->view.table_physical,&report);
    return rc||!report.complete?sensor_memory_error(c,-71,5):0;
}
static int sensor_memory_initialize(EldenSensorNativeMemory *c){
    c->status.stage=2;int rc=sensor_memory_admit(c,0);if(rc)return sensor_memory_error(c,rc,2);
    c->view.memory={nullptr,c->config.iova,c->config.bytes,c->config.session,c->config.process};
    const uint16_t offsets[]={0x1000,0x1040,0x1060,0x1080};
    for(unsigned i=0;i<4;i++)c->view.stream_ids[i]=(uint16_t)(offsets[i]+c->config.session);
    rc=c->owner.reserve(c->owner.context,c->owner.owner,c->owner.epoch,&c->config,&c->view.lease);
    if(rc)return sensor_memory_error(c,rc,6);
    const auto &lease=c->view.lease;
    if(!lease.route_epoch||lease.asid<1||lease.asid>65535||lease.bank<7||lease.bank>82||!sensor_output_bits(lease.id0,lease.id2))return sensor_memory_error(c,-95,6);
    c->cache_line=sensor_memory_cache_line();if(c->cache_line<32||c->cache_line>4096)return sensor_memory_error(c,-95,7);
    uint64_t first=c->config.iova,last=first+c->config.bytes-1;
    uint32_t l2=(uint32_t)((last>>30)-(first>>30)+1),l3=(uint32_t)((last>>21)-(first>>21)+1);
    c->table_capacity=1+l2+l3;if(c->table_capacity>36)return sensor_memory_error(c,-22,8);
    c->status.stage=3;IOOptionBits options=(IOOptionBits)kIODirectionInOut|(IOOptionBits)kIOMemoryPhysicallyContiguous|(IOOptionBits)kIOMemoryMapperNone;
    c->data_descriptor=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,options,c->config.bytes,4096);
    c->table_descriptor=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,options,(uint64_t)c->table_capacity*4096,4096);
    if(!c->data_descriptor||!c->table_descriptor)return sensor_memory_error(c,-12,9);
    c->status.stage=4;
    if(elden_sensor_memory_prepare(c->data_descriptor,kIODirectionInOut)!=kIOReturnSuccess||elden_sensor_memory_prepare(c->table_descriptor,kIODirectionInOut)!=kIOReturnSuccess)return sensor_memory_error(c,-5,10);
    c->status.stage=5;uint8_t *cached=(uint8_t *)elden_sensor_memory_bytes(c->data_descriptor);c->tables=(uint8_t *)elden_sensor_memory_bytes(c->table_descriptor);
    c->view.table_mapping=(const volatile uint64_t *)c->tables;
    IOByteCount span=0;uint64_t data_pa=elden_sensor_memory_segment(c->data_descriptor,0,&span,kIOMemoryMapperNone),table_pa=elden_sensor_memory_segment(c->table_descriptor,0,&span,kIOMemoryMapperNone);
    uint64_t table_bytes=(uint64_t)c->table_capacity*4096;
    c->view.data_physical=data_pa;c->view.table_physical=table_pa;c->view.table_bytes=(uint32_t)table_bytes;
    if(!data_pa||!table_pa||data_pa>UINT64_MAX-c->config.bytes||table_pa>UINT64_MAX-table_bytes||
       (data_pa<table_pa+table_bytes&&table_pa<data_pa+c->config.bytes))return sensor_memory_error(c,-71,11);
    uint64_t limit=UINT64_C(1)<<sensor_output_bits(lease.id0,lease.id2);
    if(data_pa>=limit||c->config.bytes>limit-data_pa||table_pa>=limit||table_bytes>limit-table_pa)return sensor_memory_error(c,-71,11);
    rc=sensor_memory_verify(c,c->data_descriptor,cached,data_pa,c->config.bytes,0xff);if(rc)return rc;
    rc=sensor_memory_verify(c,c->table_descriptor,c->tables,table_pa,(uint32_t)table_bytes,0xff);if(rc)return rc;
    c->status.stage=6;memset(cached,0,c->config.bytes);sensor_memory_clean(cached,c->config.bytes,c->cache_line);
    c->alias=c->data_descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache);
    if(!c->alias)return sensor_memory_error(c,-12,12);
    uintptr_t alias=elden_sensor_memory_virtual(c->alias);if(!alias)return sensor_memory_error(c,-12,12);
    c->data=(uint8_t *)alias;c->status.stage=7;
    rc=sensor_memory_verify(c,c->data_descriptor,c->data,data_pa,c->config.bytes,0x44);if(rc)return rc;
    c->view.memory={c->data,c->config.iova,c->config.bytes,c->config.session,c->config.process};
    c->view.data_physical=data_pa;c->view.table_physical=table_pa;c->view.table_bytes=(uint32_t)table_bytes;
    c->view.table_config={32,sensor_output_bits(lease.id0,lease.id2),table_pa,c->config.bytes/4096};
    c->range={c->config.iova,data_pa,c->config.bytes,EldenWifiSmmu4KRead|EldenWifiSmmu4KWrite,0,3};
    c->status.stage=8;EldenWifiSmmu4KStatus built=EldenWifiSmmu4KBuild(&c->view.table_config,&c->range,1,(uint64_t *)c->tables,c->table_capacity,&c->view.tables);
    if(built||c->view.tables.table_pages!=c->table_capacity||c->view.tables.leaf_pages!=c->config.bytes/4096)return sensor_memory_error(c,-71,13);
    c->table_memory.count=1;c->table_memory.pools[0]={(const volatile uint64_t *)c->tables,table_pa,c->table_capacity};
    if(EldenWifiSmmu4KBindTableMemory(&c->table_memory,&c->io))return sensor_memory_error(c,-71,13);
    rc=sensor_memory_audit(c);if(rc)return rc;sensor_memory_clean(c->tables,table_bytes,c->cache_line);
    EldenWifiSmmu4KContextConfig config={&c->view.table_config,nullptr,&c->view.tables,nullptr,lease.id0,lease.id2,lease.asid,0,0,0,0};
    if(EldenWifiSmmu4KEncodeContext(&config,&c->view.context))return sensor_memory_error(c,-71,14);
    c->status.stage=9;rc=sensor_memory_admit(c,0);if(rc)return sensor_memory_error(c,rc,2);
    rc=c->owner.install(c->owner.context,c->owner.owner,c->owner.epoch,&c->view);if(rc)return sensor_memory_error(c,rc,15);
    c->status.installed=1;c->status.stage=10;
    rc=c->owner.observe(c->owner.context,c->owner.owner,c->owner.epoch,&c->view,0);if(rc)return sensor_memory_error(c,rc,16);
    rc=sensor_memory_admit(c,0);if(rc)return sensor_memory_error(c,rc,2);
    c->status.stage=11;c->status.data_physical=data_pa;c->status.table_physical=table_pa;c->status.bytes=c->config.bytes;c->status.table_pages=c->table_capacity;
    __atomic_store_n(&c->ready,1u,__ATOMIC_RELEASE);return 0;
}
}
extern "C" int elden_sensor_memory_native_open(const EldenSensorMemoryConfig *cfg,const EldenSensorMemoryOwner *owner,EldenSensorNativeMemory **out){
    if(!cfg||!owner||!out||*out||!owner->owner||!owner->epoch||!owner->admit||!owner->reserve||!owner->install||!owner->observe||!owner->withdraw||
       !cfg->iova||(cfg->iova&4095)||cfg->bytes<((2u<<20)+4096)||cfg->bytes>(64u<<20)||(cfg->bytes&4095)||cfg->iova>UINT64_C(0x100000000)-cfg->bytes||
       cfg->session<3||cfg->session>8||!cfg->process||ml_at_interrupt_context())return -22;
    int rc=owner->admit(owner->context,owner->owner,owner->epoch,0);if(rc)return rc<0?rc:-13;
    auto *c=(EldenSensorNativeMemory *)IOMallocData(sizeof(EldenSensorNativeMemory));if(!c)return -12;bzero(c,sizeof(*c));c->config=*cfg;c->owner=*owner;*out=c;
    c->lock=IOLockAlloc();if(!c->lock)return sensor_memory_error(c,-12,9);IOLockLock(c->lock);rc=sensor_memory_initialize(c);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_memory_native_view(EldenSensorNativeMemory *c,uint64_t owner,uint64_t epoch,EldenSensorMemoryView *out){
    if(!sensor_memory_owner(c,owner,epoch))return -116;if(!out||!c->lock||ml_at_interrupt_context())return -22;
    IOLockLock(c->lock);int rc=__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)?sensor_memory_admit(c,0):c->status.error?c->status.error:-19;
    if(!rc)rc=c->owner.observe(c->owner.context,owner,epoch,&c->view,0);if(rc>0)rc=-13;if(!rc)rc=sensor_memory_admit(c,0);
    if(!rc)*out=c->view;else sensor_memory_error(c,rc,16);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_memory_native_admit(EldenSensorNativeMemory *c,uint64_t owner,uint64_t epoch,int cleanup){
    if(!sensor_memory_owner(c,owner,epoch))return -116;if(cleanup!=0&&cleanup!=1)return -22;
    if(!cleanup&&!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE))return -19;
    return sensor_memory_admit(c,cleanup);
}
extern "C" int elden_sensor_memory_native_publish(EldenSensorNativeMemory *c,uint64_t owner,uint64_t epoch){
    if(!sensor_memory_owner(c,owner,epoch))return -116;if(!c->lock||ml_at_interrupt_context())return -22;
    IOLockLock(c->lock);int rc=__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)?sensor_memory_admit(c,0):c->status.error?c->status.error:-19;
    if(!rc)rc=c->owner.observe(c->owner.context,owner,epoch,&c->view,0);if(rc>0)rc=-13;if(!rc)rc=sensor_memory_admit(c,0);
    if(!rc)sensor_memory_barrier();else sensor_memory_error(c,rc,16);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_memory_native_stop(EldenSensorNativeMemory *c,uint64_t owner,uint64_t epoch){
    if(!sensor_memory_owner(c,owner,epoch))return -116;__atomic_store_n(&c->stopping,1u,__ATOMIC_RELEASE);
    if(!c->lock||ml_at_interrupt_context()||!IOLockTryLock(c->lock))return -11;
    int rc=sensor_memory_admit(c,1);
    if(!rc&&!c->withdrawn)rc=c->owner.withdraw(c->owner.context,owner,epoch,&c->view);
    if(rc>0)rc=-13;
    if(!rc){c->withdrawn=1;__atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);c->status.stage=12;c->status.installed=0;}IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_memory_native_status(EldenSensorNativeMemory *c,EldenSensorMemoryStatus *out){
    if(!c||!out||!c->lock||ml_at_interrupt_context())return -22;if(!IOLockTryLock(c->lock))return -11;
    *out=c->status;out->stopping=__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE);IOLockUnlock(c->lock);return 0;
}
