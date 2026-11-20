/* SPDX-License-Identifier: BSD-2-Clause-Patent */
struct EldenCameraNativeDma {
    EldenCameraDmaConfig config;EldenCameraDmaOwner owner;EldenCameraDmaView view;EldenCameraDmaStatus status;
    IOLock *lock;IOBufferMemoryDescriptor *data_descriptor,*table_descriptor;IOMemoryMap *alias;
    uint8_t *data,*tables;uint32_t cache_line,table_capacity,ready,stopping;
    EldenWifiSmmu4KRange ranges[8];EldenWifiSmmu4KTableMemory table_memory;EldenWifiSmmu4KTableIo io;
    unsigned char visited[68];uint32_t slot_state[8];EldenCameraFrame borrowed[8];
};
namespace {
static uint32_t camera_dma_attempted;
static int camera_dma_normalize(int rc){return rc<=0?rc:-13;}
static bool camera_dma_identity(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch){return c&&c->owner.owner==owner&&c->owner.epoch==epoch;}
static int camera_dma_admit(EldenCameraNativeDma *c,int cleanup){
    if(!cleanup&&__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -125;
    return camera_dma_normalize(c->owner.admit(c->owner.context,c->owner.owner,c->owner.epoch,cleanup));
}
static int camera_dma_error(EldenCameraNativeDma *c,int code,uint32_t reason){
    c->status.error=code<0?code:-13;c->status.reason=reason;__atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);return c->status.error;
}
#include "DmaOriginalChecks.inc"
static int camera_dma_geometry(const EldenCameraDmaConfig *cfg,uint64_t offsets[8],uint64_t mapped[8],uint32_t sensors[8],unsigned *count,uint64_t *bytes){
    if(!cfg||!count||!bytes||!cfg->iova||(cfg->iova&4095))return -22;
    unsigned total=0;uint64_t cursor=4096;
    for(unsigned sensor=0;sensor<2;sensor++){
        if(cfg->buffers[sensor]&&(cfg->buffers[sensor]<2||cfg->buffers[sensor]>4))return -22;
        uint64_t frame=(uint64_t)(sensor?4080u:5104u)*(sensor?2448u:3072u),rounded=(frame+4095)&~UINT64_C(4095);
        for(unsigned i=0;i<cfg->buffers[sensor];i++){
            if(total==8)return -22;offsets[total]=cursor;mapped[total]=rounded;sensors[total]=sensor;total++;cursor+=rounded+4096;
        }
    }
    if(total<2||cursor>(128u<<20)||cfg->iova>UINT64_C(0x100000000)-cursor)return -22;
    *count=total;*bytes=cursor;return 0;
}
static int camera_dma_audit(EldenCameraNativeDma *c){
    EldenWifiSmmu4KAuditRegion region={c->ranges,c->view.count,c->table_capacity,(size_t)c->table_capacity*512,c->visited,sizeof(c->visited)};
    EldenWifiSmmu4KTableAuditResult report={};auto rc=EldenWifiSmmu4KAuditTables(&c->io,&c->view.table_config,&c->view.tables,&region,c->view.table_physical,&report);
    return rc||!report.complete?camera_dma_error(c,-71,5):0;
}
static int camera_dma_live(EldenCameraNativeDma *c,const EldenCameraBuffer *buffer,int completion){
    int rc=camera_dma_admit(c,0);if(!rc)rc=camera_dma_normalize(c->owner.observe(c->owner.context,c->owner.owner,c->owner.epoch,&c->view,buffer,completion));
    if(!rc)rc=camera_dma_admit(c,0);if(rc)return camera_dma_error(c,rc,16);return 0;
}
static int camera_dma_slot(EldenCameraNativeDma *c,unsigned sensor,const EldenCameraBuffer *buffer){
    for(unsigned i=0;i<c->view.count;i++){const auto &slot=c->view.slots[i];const auto &b=slot.buffer;
        if(slot.sensor==sensor&&b.id==buffer->id&&buffer->owner==b.owner&&buffer->epoch==b.epoch&&buffer->route_epoch==b.route_epoch&&
           buffer->iova==b.iova&&buffer->bytes==b.bytes&&buffer->mapping==b.mapping)return (int)i;}
    return -116;
}
static int camera_dma_initialize(EldenCameraNativeDma *c){
    uint64_t offsets[8]={},mapped[8]={},bytes=0;uint32_t sensors[8]={};unsigned count=0;
    int rc=camera_dma_geometry(&c->config,offsets,mapped,sensors,&count,&bytes);if(rc)return camera_dma_error(c,rc,1);
    c->view.owner=c->owner.owner;c->view.epoch=c->owner.epoch;c->view.iova=c->config.iova;c->view.allocation_bytes=bytes;c->view.stream_id=0x1c00;c->view.count=count;
    c->status.stage=2;c->status.bytes=bytes;c->status.buffers=count;
    rc=camera_dma_admit(c,0);if(!rc)rc=camera_dma_normalize(c->owner.early(c->owner.context,c->owner.owner,c->owner.epoch));if(rc)return camera_dma_error(c,rc,2);
    c->status.reservation_attempted=1;
    rc=camera_dma_normalize(c->owner.reserve(c->owner.context,c->owner.owner,c->owner.epoch,&c->config,bytes,&c->view.lease));if(rc)return camera_dma_error(c,rc,6);
    const auto &lease=c->view.lease;
    // Exact IDs already observed on Y700, but the original provider MUST read
    // them now and prove slot/bank/ASID/IOVA ownership. IDs alone do not lease.
    if(lease.id0!=0x4c017e84||lease.id1!=0x60000053||lease.id2!=0x5111||!lease.route_epoch||
       lease.bank<7||lease.bank>=(lease.id1&255u)||lease.slot<8||lease.slot>=132||!lease.asid||lease.asid>255||!camera_output_bits(lease.id0,lease.id2))return camera_dma_error(c,-95,6);
    c->cache_line=camera_dma_cache_line();if(c->cache_line<32||c->cache_line>4096)return camera_dma_error(c,-95,7);
    uint64_t first=c->config.iova,last=first+bytes-1;
    c->table_capacity=(uint32_t)(1+((last>>30)-(first>>30)+1)+((last>>21)-(first>>21)+1));
    if(c->table_capacity>68)return camera_dma_error(c,-22,8);
    c->status.stage=3;IOOptionBits options=(IOOptionBits)kIODirectionInOut|(IOOptionBits)kIOMemoryPhysicallyContiguous|(IOOptionBits)kIOMemoryMapperNone;
    c->data_descriptor=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,options,bytes,4096);
    c->table_descriptor=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,options,(uint64_t)c->table_capacity*4096,4096);
    if(!c->data_descriptor||!c->table_descriptor)return camera_dma_error(c,-12,9);
    c->status.stage=4;
    if(elden_camera_dma_prepare(c->data_descriptor,kIODirectionInOut)!=kIOReturnSuccess||
       elden_camera_dma_prepare(c->table_descriptor,kIODirectionInOut)!=kIOReturnSuccess)return camera_dma_error(c,-5,10);
    c->status.stage=5;uint8_t *cached=(uint8_t *)elden_camera_dma_bytes(c->data_descriptor);c->tables=(uint8_t *)elden_camera_dma_bytes(c->table_descriptor);
    IOByteCount span=0;uint64_t data_pa=elden_camera_dma_segment(c->data_descriptor,0,&span,kIOMemoryMapperNone),
        table_pa=elden_camera_dma_segment(c->table_descriptor,0,&span,kIOMemoryMapperNone),table_bytes=(uint64_t)c->table_capacity*4096;
    c->view.data_physical=data_pa;c->view.table_physical=table_pa;c->view.table_mapping=(const volatile uint64_t *)c->tables;c->view.table_bytes=(uint32_t)table_bytes;
    uint64_t limit=UINT64_C(1)<<camera_output_bits(lease.id0,lease.id2);
    if(!data_pa||!table_pa||data_pa>=limit||bytes>limit-data_pa||table_pa>=limit||table_bytes>limit-table_pa||
       (data_pa<table_pa+table_bytes&&table_pa<data_pa+bytes))return camera_dma_error(c,-71,11);
    rc=camera_dma_verify(c,c->data_descriptor,cached,data_pa,(uint32_t)bytes,0xff);if(rc)return rc;
    rc=camera_dma_verify(c,c->table_descriptor,c->tables,table_pa,(uint32_t)table_bytes,0xff);if(rc)return rc;
    c->status.stage=6;memset(cached,0,(size_t)bytes);camera_dma_clean(cached,bytes,c->cache_line);
    c->alias=c->data_descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache);
    if(!c->alias)return camera_dma_error(c,-12,12);uintptr_t alias=elden_camera_dma_virtual(c->alias);if(!alias)return camera_dma_error(c,-12,12);
    c->data=(uint8_t *)alias;c->status.stage=7;rc=camera_dma_verify(c,c->data_descriptor,c->data,data_pa,(uint32_t)bytes,0x44);if(rc)return rc;
    size_t leaves=0;
    for(unsigned i=0;i<count;i++){
        unsigned sensor=sensors[i];uint64_t frame=(uint64_t)(sensor?4080u:5104u)*(sensor?2448u:3072u);
        c->view.slots[i]={{i+1,c->owner.owner,c->owner.epoch,lease.route_epoch,c->config.iova+offsets[i],frame,c->data+offsets[i]},data_pa+offsets[i],mapped[i],sensor,sensor?4080u:5104u};
        c->ranges[i]={c->config.iova+offsets[i],data_pa+offsets[i],mapped[i],EldenWifiSmmu4KRead|EldenWifiSmmu4KWrite,0,3};leaves+=(size_t)(mapped[i]/4096);
    }
    c->view.table_config={32,camera_output_bits(lease.id0,lease.id2),table_pa,leaves};c->status.stage=8;
    memset(c->tables,0,(size_t)table_bytes);
    auto built=EldenWifiSmmu4KBuild(&c->view.table_config,c->ranges,count,(uint64_t *)c->tables,c->table_capacity,&c->view.tables);
    if(built||c->view.tables.table_pages>c->table_capacity||c->view.tables.leaf_pages!=leaves)return camera_dma_error(c,-71,13);
    c->table_memory.count=1;c->table_memory.pools[0]={(const volatile uint64_t *)c->tables,table_pa,c->table_capacity};
    if(EldenWifiSmmu4KBindTableMemory(&c->table_memory,&c->io))return camera_dma_error(c,-71,13);
    rc=camera_dma_audit(c);if(rc)return rc;camera_dma_clean(c->tables,table_bytes,c->cache_line);
    EldenWifiSmmu4KContextConfig context={&c->view.table_config,nullptr,&c->view.tables,nullptr,lease.id0,lease.id2,lease.asid,0,0,0,0};
    if(EldenWifiSmmu4KEncodeContext(&context,&c->view.context))return camera_dma_error(c,-71,14);
    rc=camera_dma_admit(c,0);if(!rc)rc=camera_dma_normalize(c->owner.early(c->owner.context,c->owner.owner,c->owner.epoch));if(rc)return camera_dma_error(c,rc,2);
    c->status.stage=9;c->status.install_attempted=1;
    rc=camera_dma_normalize(c->owner.install(c->owner.context,c->owner.owner,c->owner.epoch,&c->view));if(rc)return camera_dma_error(c,rc,15);
    c->status.installed=1;c->status.stage=10;rc=camera_dma_live(c,nullptr,0);if(rc)return rc;
    c->status.stage=11;c->status.table_pages=(uint32_t)c->view.tables.table_pages;c->status.leaf_pages=(uint32_t)c->view.tables.leaf_pages;
    __atomic_store_n(&c->ready,1u,__ATOMIC_RELEASE);return 0;
}
}
extern "C" int elden_camera_dma_open(const EldenCameraDmaConfig *config,const EldenCameraDmaOwner *owner,EldenCameraNativeDma **out){
    uint64_t offsets[8],mapped[8],bytes;uint32_t sensors[8];unsigned count;
    if(!config||!owner||!out||*out||!owner->owner||!owner->epoch||!owner->admit||!owner->early||!owner->reserve||!owner->install||!owner->observe||!owner->retire||
       camera_dma_geometry(config,offsets,mapped,sensors,&count,&bytes)||ml_at_interrupt_context())return -22;
    int rc=camera_dma_normalize(owner->admit(owner->context,owner->owner,owner->epoch,0));if(!rc)rc=camera_dma_normalize(owner->early(owner->context,owner->owner,owner->epoch));if(rc)return rc;
    uint32_t none=0;if(!__atomic_compare_exchange_n(&camera_dma_attempted,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    auto *c=(EldenCameraNativeDma *)IOMallocData(sizeof(EldenCameraNativeDma));if(!c)return -12;bzero(c,sizeof(*c));c->config=*config;c->owner=*owner;*out=c;
    c->lock=IOLockAlloc();if(!c->lock)return camera_dma_error(c,-12,9);IOLockLock(c->lock);rc=camera_dma_initialize(c);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_view(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch,EldenCameraDmaView *out){
    if(!camera_dma_identity(c,owner,epoch))return -116;if(!out||!c->lock||ml_at_interrupt_context())return -22;IOLockLock(c->lock);
    int rc=__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)?camera_dma_live(c,nullptr,0):c->status.error?c->status.error:-19;
    if(!rc)*out=c->view;IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_admit(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch,int cleanup){
    if(!camera_dma_identity(c,owner,epoch))return -116;if(cleanup!=0&&cleanup!=1)return -22;
    if(!cleanup&&!__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE))return -19;return camera_dma_admit(c,cleanup);
}
extern "C" int elden_camera_dma_buffer(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch,unsigned sensor,const EldenCameraBuffer *buffer,int completion){
    if(!camera_dma_identity(c,owner,epoch))return -116;if(!buffer||sensor>1||(completion!=0&&completion!=1)||!c->lock||ml_at_interrupt_context())return -22;
    IOLockLock(c->lock);int rc=__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)?camera_dma_admit(c,0):-19;
    int index=camera_dma_slot(c,sensor,buffer);if(!rc&&index<0)rc=index;
    if(!rc){unsigned state=c->slot_state[index];if(completion?state!=1:(state!=0&&state!=4))rc=-16;}
    if(!rc){
        // Record queued ownership before its visibility callback: callback or
        // queue failure cannot imply that device traffic never executed.
        if(!completion)c->slot_state[index]=1;
        rc=camera_dma_live(c,buffer,completion);if(!rc){camera_dma_barrier();if(completion)c->slot_state[index]=2;}
    }
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_claim_frame(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch,unsigned sensor,const EldenCameraFrame *frame){
    if(!camera_dma_identity(c,owner,epoch))return -116;if(!frame||sensor>1||!c->lock||ml_at_interrupt_context())return -22;
    IOLockLock(c->lock);int rc=__atomic_load_n(&c->ready,__ATOMIC_ACQUIRE)?camera_dma_admit(c,0):-19;
    int index=camera_dma_slot(c,sensor,&frame->buffer);if(!rc&&index<0)rc=index;
    if(!rc&&(c->slot_state[index]!=2||!frame->timestamp_ns||frame->bytesused!=frame->buffer.bytes))rc=-71;
    if(!rc)rc=camera_dma_live(c,nullptr,0);
    if(!rc){camera_dma_barrier();c->slot_state[index]=3;c->borrowed[index]=*frame;}
    IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_release_frame(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch,unsigned sensor,const EldenCameraFrame *frame){
    if(!camera_dma_identity(c,owner,epoch))return -116;if(!frame||sensor>1||!c->lock||ml_at_interrupt_context())return -22;
    IOLockLock(c->lock);int rc=camera_dma_admit(c,1);int index=camera_dma_slot(c,sensor,&frame->buffer);if(!rc&&index<0)rc=index;
    if(!rc){const auto &saved=c->borrowed[index];if(c->slot_state[index]!=3||saved.sequence!=frame->sequence||saved.timestamp_ns!=frame->timestamp_ns||
        saved.bytesused!=frame->bytesused||saved.error!=frame->error)rc=-116;}
    if(!rc){camera_dma_barrier();c->slot_state[index]=4;}IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_stop(EldenCameraNativeDma *c,uint64_t owner,uint64_t epoch){
    if(!camera_dma_identity(c,owner,epoch))return -116;__atomic_store_n(&c->stopping,1u,__ATOMIC_RELEASE);
    if(!c->lock||ml_at_interrupt_context()||!IOLockTryLock(c->lock))return -11;
    int rc=camera_dma_admit(c,1);if(!rc&&!c->status.retired)rc=camera_dma_normalize(c->owner.retire(c->owner.context,owner,epoch,&c->view,c->status.reservation_attempted,c->status.install_attempted));
    if(!rc){c->status.retired=1;c->status.installed=0;c->status.stage=12;
        for(unsigned i=0;i<c->view.count;i++)if(c->slot_state[i]==1||c->slot_state[i]==2)c->slot_state[i]=5;
        // CPU-borrowed frames remain borrowed/valid storage; real device/IRQ/
        // context retirement cannot falsely acknowledge the caller returned it.
        __atomic_store_n(&c->ready,0u,__ATOMIC_RELEASE);}IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_camera_dma_status(EldenCameraNativeDma *c,EldenCameraDmaStatus *out){
    if(!c||!out||!c->lock||ml_at_interrupt_context())return -22;if(!IOLockTryLock(c->lock))return -11;
    *out=c->status;out->stopping=__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE);out->device_owned=out->completed=out->borrowed=out->returned=0;
    for(unsigned i=0;i<c->view.count;i++){out->device_owned+=c->slot_state[i]==1;out->completed+=c->slot_state[i]==2;
        out->borrowed+=c->slot_state[i]==3;out->returned+=c->slot_state[i]==4;}
    IOLockUnlock(c->lock);return 0;
}
