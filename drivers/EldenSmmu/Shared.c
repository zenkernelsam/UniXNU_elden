/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Shared.h"
#include <stddef.h>
enum {Id0=0x4c017e84,Id1=0x60000053,Id2=0x5111,Cr0=0x230406,InvalidS2cr=0x0300006e};
struct shared {
    uint32_t magic,busy;EldenSmmuSharedConfig config;EldenSmmuSharedOps ops;EldenSmmuSharedStatus status;
    EldenSmmuBootRoute routes[6];EldenSmmuSharedLease leases[3];EldenSmmuSharedMemory memory[3];
    EldenWifiSmmu4KContextConfig context[3];EldenWifiSmmu4KProgramResult program[3];
    uint8_t visited[3][68];uint32_t attempted,irq_attempted,irq_drained,cb0;
    uint32_t normal_groups;int admission_error;
    uint32_t route_smr[6],route_s2cr[6],bank_cbar[3],bank_cba2r[3],bank_sctlr[3];
    unsigned write_group,write_mode;
};
_Static_assert(sizeof(struct shared)<=sizeof(EldenSmmuShared),"sharedSMMU storage");
static struct shared *core(EldenSmmuShared *s){return (struct shared *)(void *)s;}
static int result(int rc){return rc<=0?rc:-13;}
static int bit(unsigned group){return 1u<<group;}
static int identity(struct shared *s,uint64_t owner,uint64_t epoch){return owner==s->config.owner&&epoch==s->config.epoch?0:-116;}
static int admit(struct shared *s,int cleanup){if(!cleanup&&s->status.error)return s->status.error;
    return result(s->ops.admit(s->ops.context,s->config.owner,s->config.epoch,cleanup));}
static int early(struct shared *s){int rc=admit(s,0);if(!rc)rc=result(s->ops.early(s->ops.context,s->config.owner,s->config.epoch));
    if(!rc&&s->status.route_reserved)rc=elden_smmu_boot_routes_early(s->ops.context,s->config.epoch);return rc;}
static int failed(struct shared *s,int rc){rc=rc<0?rc:-13;if(!s->status.error)s->status.error=rc;
    __atomic_store_n(&s->normal_groups,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&s->admission_error,s->status.error,__ATOMIC_RELEASE);
    if(s->status.route_reserved&&!s->status.route_committed)(void)elden_smmu_boot_routes_poison(s->ops.context,s->config.epoch);return rc;}
static int enter(EldenSmmuShared *s){if(!s||__atomic_load_n(&core(s)->magic,__ATOMIC_ACQUIRE)!=0x534d5348)return -22;uint32_t zero=0;
    return __atomic_compare_exchange_n(&core(s)->busy,&zero,1u,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:-16;}
static int leave(struct shared *s,int rc){__atomic_store_n(&s->busy,0u,__ATOMIC_RELEASE);return rc;}
static int rd(struct shared *s,uint32_t off,uint32_t *out){if(!out||off&3||off>=0x100000)return -22;int rc=admit(s,1);if(rc)return rc;
    *out=UINT32_MAX;s->status.reads++;s->status.last_offset=off;rc=result(s->ops.io.read32(s->ops.io.cookie,off,out));s->status.last_value=*out;
    return rc?rc:*out==UINT32_MAX?-5:0;}
static int equal(struct shared *s,uint32_t off,uint32_t expected){uint32_t value=0;int rc=rd(s,off,&value);return rc?rc:value!=expected?-116:0;}
static int layout_observe(struct shared *s,int touch);
static int write_guard(struct shared *s);
static void record32(struct shared *s,uint32_t off,uint32_t value){
    for(unsigned i=0;i<s->status.route_count;i++){
        if(off==0x800+4*s->routes[i].slot)s->route_smr[i]=value;
        if(off==0xc00+4*s->routes[i].slot)s->route_s2cr[i]=value;
    }
    for(unsigned i=0;i<3;i++)if(s->config.groups&bit(i)){
        unsigned bank=s->leases[i].bank;
        if(off==0x1000+4*bank)s->bank_cbar[i]=value;
        if(off==0x1800+4*bank)s->bank_cba2r[i]=value;
        if(off==0x80000+4096*bank)s->bank_sctlr[i]=value;
    }
}
static int program_read32(void *p,size_t off,uint32_t *out){return off>UINT32_MAX?-22:rd(p,(uint32_t)off,out);}
static int program_read64(void *p,size_t off,uint64_t *out){struct shared *s=p;if(!out||off&7||off>0xffff8)return -22;
    int rc=admit(s,1);if(rc)return rc;s->status.reads++;return result(s->ops.io.read64(s->ops.io.cookie,off,out));}
static int program_write32(void *p,size_t off,uint32_t value){struct shared *s=p;if(off>UINT32_MAX)return -22;
    uint32_t bank=s->leases[s->write_group].bank,cb=0x80000+4096*bank;int allowed=0;
    if(s->write_mode==1){
        allowed=off==0x1000+4*bank||off==0x1800+4*bank||off==cb||off==cb+0x10||off==cb+0x30||off==cb+0x38||off==cb+0x3c||off==cb+0x58||off==cb+0x610||off==cb+0x618||off==cb+0x7f0;
        for(unsigned i=0;i<s->status.route_count;i++)if(s->routes[i].bank==bank&&(off==0x800+4*s->routes[i].slot||off==0xc00+4*s->routes[i].slot))allowed=1;
    }else if(s->write_mode==2)allowed=(off==cb&&!value)||off==cb+0x610||off==cb+0x7f0;
    if(!allowed)return -13;
    int rc=write_guard(s);if(rc)return rc;s->status.writes++;rc=result(s->ops.io.write32(s->ops.io.cookie,off,value));
    if(!rc)record32(s,(uint32_t)off,value);return rc;}
static int program_write64(void *p,size_t off,uint64_t value){struct shared *s=p;int rc=write_guard(s);if(rc)return rc;
    uint32_t cb=0x80000+4096*s->leases[s->write_group].bank;if(s->write_mode!=1||(off!=cb+0x20&&off!=cb+0x28))return -13;
    s->status.writes++;return result(s->ops.io.write64(s->ops.io.cookie,off,value));}
static int program_barrier(void *p){struct shared *s=p;int rc=admit(s,s->write_mode==2);return rc?rc:result(s->ops.io.barrier(s->ops.io.cookie));}
static EldenWifiSmmu4KIo program_io(struct shared *s){return (EldenWifiSmmu4KIo){s,0x100000,program_read32,program_read64,program_write32,program_write64,program_barrier};}
static int put(struct shared *s,uint32_t off,uint32_t value){int rc=program_write32(s,off,value);
    if(!rc)rc=program_barrier(s);if(!rc)rc=equal(s,off,value);return rc;}
static int enabled_group(struct shared *s,unsigned group){return group<3&&(s->config.groups&bit(group));}
static int plan_valid(const EldenSmmuSharedConfig *c){
    if(!c||!c->owner||!c->epoch||!c->groups||c->groups&~7u)return 0;
    if((c->groups&4u)&&(c->sensor_session<3||c->sensor_session>8))return 0;
    for(unsigned i=0;i<3;i++)if(c->groups&bit(i)){
        const EldenSmmuSharedAperture *p=&c->apertures[i];if(!p->iova||!p->bytes||(p->iova|p->bytes)&4095||p->bytes>UINT64_C(0x100000000)||p->iova>UINT64_C(0x100000000)-p->bytes)return 0;
        for(unsigned j=0;j<i;j++)if(c->groups&bit(j)){const EldenSmmuSharedAperture *q=&c->apertures[j];if(p->iova<q->iova+q->bytes&&q->iova<p->iova+p->bytes)return 0;}
    }
    return 1;
}
static int baseline(struct shared *s){
    int rc=equal(s,0,Cr0);if(!rc)rc=equal(s,0x20,Id0);if(!rc)rc=equal(s,0x24,Id1);if(!rc)rc=equal(s,0x28,Id2);if(!rc)rc=equal(s,0x48,0);if(rc)return rc;
    const uint32_t active[]={0x80000060u,0x80000540u,0x80020800u,0x80000040u,0x80000480u,0x80000540u,0x80000480u};
    for(unsigned i=0;i<132;i++)if((rc=equal(s,0x800+4*i,i<7?active[i]:0))||(rc=equal(s,0xc00+4*i,i<7?i:InvalidS2cr)))return rc;
    for(unsigned i=0;i<83;i++)if((rc=equal(s,0x1000+4*i,i<7?0x1f000u:0))||(rc=equal(s,0x1800+4*i,i<7?1:0)))return rc;
    return 0;
}
static int select_groups(struct shared *s){
    unsigned used[256]={0},freebanks[3]={0},freecount=0;int rc;
    for(unsigned bank=0;bank<83;bank++){
        uint32_t cb=0x80000+4096*bank,ctl=0,fsr=0;if((rc=rd(s,cb,&ctl))||(rc=rd(s,cb+0x58,&fsr)))return rc;
        if(!bank){s->cb0=ctl;if(ctl&1)return -95;}
        if(fsr&0xc00001feu)return -5;
        if(ctl&1){uint32_t cbar=0,cba2r=0,high0=0,high1=0;
            if((rc=rd(s,0x1000+4*bank,&cbar))||(rc=rd(s,0x1800+4*bank,&cba2r))||(rc=rd(s,cb+0x24,&high0))||(rc=rd(s,cb+0x2c,&high1)))return rc;
            if((cbar>>16&3)!=1||cba2r!=1)return -95;unsigned a=high0>>16,b=high1>>16;if(a<256)used[a]=1;if(b<256)used[b]=1;
        }else if(bank>=7&&freecount<3)freebanks[freecount++]=bank;
    }
    unsigned slot=8,bank_index=0;
    for(unsigned group=0;group<3;group++)if(enabled_group(s,group)){
        if(bank_index>=freecount)return -28;unsigned asid=1;while(asid<256&&used[asid])asid++;if(asid==256)return -28;used[asid]=1;
        EldenSmmuSharedLease *lease=&s->leases[group];unsigned bank=freebanks[bank_index++],count=group==2?4u:1u;
        *lease=(EldenSmmuSharedLease){bank,asid,slot,count,Id0,Id1,Id2,s->config.epoch};
        if((rc=rd(s,0x80000+4096*bank,&s->bank_sctlr[group])))return rc;
        s->status.selected_banks[group]=bank;s->status.selected_asids[group]=asid;
        const unsigned sensor_sids[]={0x1000,0x1040,0x1060,0x1080};
        for(unsigned i=0;i<count;i++){unsigned sid=group==0?0x1401:group==1?0x1c00:sensor_sids[i]+s->config.sensor_session;
            unsigned index=s->status.route_count++;s->routes[index]=(EldenSmmuBootRoute){slot,bank,0x80000000u|sid,bank,0x1f300,1};s->route_s2cr[index]=InvalidS2cr;s->status.route_slots[index]=slot++;}
    }
    return 0;
}
int elden_smmu_shared_open(EldenSmmuShared *storage,const EldenSmmuSharedConfig *config,const EldenSmmuSharedOps *ops){
    if(!storage||__atomic_load_n(&core(storage)->magic,__ATOMIC_ACQUIRE)||!plan_valid(config)||!ops||!ops->context||!ops->admit||!ops->early||!ops->quiesced||!ops->memory||!ops->irq||!ops->peer_route||!ops->now_us||!ops->bank||
       !ops->io.read32||!ops->io.read64||!ops->io.write32||!ops->io.write64||!ops->io.barrier||ops->io.register_bytes!=0x100000)return -22;
    int rc=result(ops->admit(ops->context,config->owner,config->epoch,0));if(!rc)rc=result(ops->early(ops->context,config->owner,config->epoch));if(rc)return rc;
    uint32_t empty=0;if(!__atomic_compare_exchange_n(&core(storage)->magic,&empty,0x534d5052u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    uint8_t *b=(uint8_t *)storage;for(unsigned i=sizeof(uint32_t);i<sizeof(*storage);i++)b[i]=0;struct shared *s=core(storage);s->config=*config;s->ops=*ops;
    s->busy=1;__atomic_store_n(&s->magic,0x534d5348u,__ATOMIC_RELEASE);
    s->status.stage=1;s->status.groups=config->groups;
    for(unsigned group=0;group<3;group++)if(enabled_group(s,group)){rc=result(ops->quiesced(ops->context,config->owner,config->epoch,group));if(rc)return leave(s,failed(s,rc));}
    rc=baseline(s);if(!rc)rc=select_groups(s);if(rc)return leave(s,failed(s,rc));s->status.stage=2;
    rc=early(s);if(!rc)rc=elden_smmu_boot_routes_reserve(ops->context,config->epoch,s->routes,s->status.route_count);if(rc)return leave(s,failed(s,rc));s->status.route_reserved=1;
    for(unsigned group=0;group<3;group++)if(enabled_group(s,group)){
        rc=result(ops->bank(ops->context,config->owner,config->epoch,group,&s->leases[group],0));if(rc)return leave(s,failed(s,rc));
        s->irq_attempted|=bit(group);rc=result(ops->irq(ops->context,config->owner,config->epoch,group,s->leases[group].bank,1));if(rc)return leave(s,failed(s,rc));}
    s->status.stage=3;s->status.translation_mode=2;return leave(s,0);
}
int elden_smmu_shared_early(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch){int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch))return leave(s,-116);rc=early(s);if(!rc)rc=elden_smmu_boot_routes_early(s->ops.context,epoch);return leave(s,rc);}
int elden_smmu_shared_lease(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group,EldenSmmuSharedLease *out){
    if(!out)return -22;int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch)||!enabled_group(s,group))return leave(s,-116);rc=early(s);if(!rc)*out=s->leases[group];return leave(s,rc);
}
static int region_valid(struct shared *s,unsigned group,const EldenSmmuSharedMemory *m){
    if(!m||!m->io.read_descriptor||!m->range_count||m->range_count>8||!m->capacity_pages||m->capacity_pages>68||m->config.input_bits!=32||m->config.output_bits!=36)return 0;
    if(!m->data_physical||!m->data_bytes||(m->data_physical|m->data_bytes)&4095||m->data_bytes>UINT64_C(0x1000000000)||m->data_physical>UINT64_C(0x1000000000)-m->data_bytes)return 0;
    const EldenSmmuSharedAperture *p=&s->config.apertures[group];
    for(unsigned i=0;i<m->range_count;i++){const EldenWifiSmmu4KRange *r=&m->ranges[i];
        if(!r->physical||!r->bytes||(r->iova|r->physical|r->bytes)&4095||r->bytes>UINT64_C(0x1000000000)||r->physical>UINT64_C(0x1000000000)-r->bytes||
           r->physical<m->data_physical||r->bytes>m->data_bytes||r->physical-m->data_physical>m->data_bytes-r->bytes||
           r->iova<p->iova||r->bytes>p->bytes||r->iova-p->iova>p->bytes-r->bytes||r->permissions!=3||r->writeback||r->shareability!=3)return 0;
        for(unsigned j=0;j<i;j++){const EldenWifiSmmu4KRange *q=&m->ranges[j];if(r->physical<q->physical+q->bytes&&q->physical<r->physical+r->bytes)return 0;}
    }
    uint64_t tables=m->config.table_physical,table_bytes=(uint64_t)m->capacity_pages*4096;
    if(!tables||(tables&4095)||tables>UINT64_C(0x1000000000)-table_bytes)return 0;
    if(tables<m->data_physical+m->data_bytes&&m->data_physical<tables+table_bytes)return 0;
    for(unsigned peer=0;peer<3;peer++)if(s->status.installed&bit(peer)){
        const EldenSmmuSharedMemory *other=&s->memory[peer];uint64_t other_table=other->config.table_physical,other_bytes=(uint64_t)other->capacity_pages*4096;
        if(tables<other_table+other_bytes&&other_table<tables+table_bytes)return 0;
        if(m->data_physical<other->data_physical+other->data_bytes&&other->data_physical<m->data_physical+m->data_bytes)return 0;
        if(tables<other->data_physical+other->data_bytes&&other->data_physical<tables+table_bytes)return 0;
        if(m->data_physical<other_table+other_bytes&&other_table<m->data_physical+m->data_bytes)return 0;
        for(unsigned i=0;i<m->range_count;i++){
            const EldenWifiSmmu4KRange *r=&m->ranges[i];
            if(r->physical<other_table+other_bytes&&other_table<r->physical+r->bytes)return 0;
            for(unsigned j=0;j<other->range_count;j++){
                const EldenWifiSmmu4KRange *q=&other->ranges[j];
                if(r->physical<q->physical+q->bytes&&q->physical<r->physical+r->bytes)return 0;
                if(tables<q->physical+q->bytes&&q->physical<tables+table_bytes)return 0;
            }
        }
    }
    return 1;
}
int elden_smmu_shared_install(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group,const EldenSmmuSharedMemory *memory){
    int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);if(identity(s,owner,epoch)||!enabled_group(s,group))return leave(s,-116);
    if(s->attempted&bit(group)||!region_valid(s,group,memory))return leave(s,-22);rc=early(s);if(rc)return leave(s,rc);
    rc=result(s->ops.quiesced(s->ops.context,owner,epoch,group));if(!rc)rc=result(s->ops.memory(s->ops.context,owner,epoch,group,memory,0));if(rc)return leave(s,failed(s,rc));
    s->memory[group]=*memory;EldenSmmuSharedMemory *m=&s->memory[group];EldenSmmuSharedLease *lease=&s->leases[group];
    s->context[group]=(EldenWifiSmmu4KContextConfig){&m->config,0,&m->tables,0,Id0,Id2,lease->asid,0,0,0,0};
    EldenWifiSmmu4KAuditRegion region={m->ranges,m->range_count,m->capacity_pages,m->capacity_pages*512,s->visited[group],sizeof(s->visited[group])};
    EldenWifiSmmu4KAuditConfig audit={m->io,&region,0};EldenWifiSmmu4KAuditResult proof;
    int status=EldenWifiSmmu4KAuditDeclaredContext(&s->context[group],&audit,&proof);if(status)return leave(s,failed(s,-100-status));
    rc=early(s);if(rc)return leave(s,failed(s,rc));s->status.stage=4;s->attempted|=bit(group);
    s->write_group=group;s->write_mode=1;EldenWifiSmmu4KIo io=program_io(s);
    status=EldenWifiSmmu4KProgramDisabledContext(&io,lease->bank,&s->context[group],1000,&s->program[group]);s->write_mode=0;
    if(status)return leave(s,failed(s,-100-status));s->status.active|=bit(group);
    /* Original context validation permits the documented read-only ASIDPNE
     * clear. Freeze the actual validated readback, not the attempted value. */
    s->bank_sctlr[group]=s->program[group].readback.sctlr;
    status=EldenWifiSmmu4KObserveAuditedContext(&io,lease->bank,&s->context[group],&audit,&proof);
    if(status||!proof.complete)return leave(s,failed(s,status?-100-status:-71));s->status.installed|=bit(group);s->status.stage=5;return leave(s,0);
}
static int prepared_observe(struct shared *s,unsigned group){
    EldenSmmuSharedMemory *m=&s->memory[group];EldenSmmuSharedLease *lease=&s->leases[group];
    int rc=early(s);if(!rc)rc=result(s->ops.memory(s->ops.context,s->config.owner,s->config.epoch,group,m,0));
    if(!rc)rc=result(s->ops.irq(s->ops.context,s->config.owner,s->config.epoch,group,lease->bank,2));
    if(!rc)rc=result(s->ops.bank(s->ops.context,s->config.owner,s->config.epoch,group,lease,0));if(rc)return rc;
    EldenWifiSmmu4KAuditRegion region={m->ranges,m->range_count,m->capacity_pages,m->capacity_pages*512,s->visited[group],sizeof(s->visited[group])};
    EldenWifiSmmu4KAuditConfig audit={m->io,&region,0};EldenWifiSmmu4KAuditResult proof;
    EldenWifiSmmu4KIo io=program_io(s);int status=EldenWifiSmmu4KObserveAuditedContext(&io,lease->bank,&s->context[group],&audit,&proof);
    return status?-100-status:!proof.complete?-71:0;
}
int elden_smmu_shared_observe_prepared(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group){
    int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch)||!enabled_group(s,group)||!(s->status.installed&bit(group))||s->status.route_committed)return leave(s,-116);
    rc=prepared_observe(s,group);return leave(s,rc?failed(s,rc):0);
}
static int routes_observe(struct shared *s){
    int rc=equal(s,0,Cr0);if(!rc)rc=equal(s,0x20,Id0);if(!rc)rc=equal(s,0x24,Id1);if(!rc)rc=equal(s,0x28,Id2);if(!rc)rc=equal(s,0x48,0);if(rc)return rc;
    const uint32_t active[]={0x80000060u,0x80000540u,0x80020800u,0x80000040u,0x80000480u,0x80000540u,0x80000480u};
    for(unsigned i=0;i<132;i++)for(unsigned kind=0;kind<2;kind++){
        uint32_t off=(kind?0xc00:0x800)+4*i,base=kind?(i<7?i:InvalidS2cr):(i<7?active[i]:0),expected=base;
        rc=elden_smmu_boot_routes_peek(s->ops.context,s->config.epoch,off,base,&expected);
        if(!rc&&i==7)rc=result(s->ops.peer_route(s->ops.context,s->config.owner,s->config.epoch,off,base,&expected));
        if(!rc&&i==7&&expected!=(kind?0u:0x800000a3u)&&expected!=base)return -116;
        if(rc||(rc=equal(s,off,expected)))return rc;
    }
    for(unsigned bank=0;bank<83;bank++)for(unsigned kind=0;kind<2;kind++){
        uint32_t off=(kind?0x1800:0x1000)+4*bank,base=bank<7?(kind?1u:0x1f000u):0,expected=base;
        rc=elden_smmu_boot_routes_peek(s->ops.context,s->config.epoch,off,base,&expected);
        if(rc||(rc=equal(s,off,expected)))return rc;
    }
    return equal(s,0x80000,s->cb0);
}
/* Track only writes whose actual callbacks succeeded. This full fresh
 * snapshot precedes every write, including the original programmer's writes.
 * Applied-error uncertainty never authorizes a guessed rollback image. */
static int layout_observe(struct shared *s,int touch){
    int rc=equal(s,0,Cr0);if(!rc)rc=equal(s,0x20,Id0);if(!rc)rc=equal(s,0x24,Id1);if(!rc)rc=equal(s,0x28,Id2);if(!rc)rc=equal(s,0x48,0);if(rc)return rc;
    const uint32_t active[]={0x80000060u,0x80000540u,0x80020800u,0x80000040u,0x80000480u,0x80000540u,0x80000480u};
    for(unsigned slot=0;slot<132;slot++)for(unsigned kind=0;kind<2;kind++){
        uint32_t off=(kind?0xc00:0x800)+4*slot,base=kind?(slot<7?slot:InvalidS2cr):(slot<7?active[slot]:0),expected=base;
        for(unsigned i=0;i<s->status.route_count;i++)if(s->routes[i].slot==slot)expected=kind?s->route_s2cr[i]:s->route_smr[i];
        if(touch&&slot==7){rc=result(s->ops.peer_route(s->ops.context,s->config.owner,s->config.epoch,off,base,&expected));if(rc)return rc;
            if(expected!=base&&expected!=(kind?0u:0x800000a3u))return -116;}
        if((rc=equal(s,off,expected)))return rc;
    }
    for(unsigned bank=0;bank<83;bank++)for(unsigned kind=0;kind<2;kind++){
        uint32_t off=(kind?0x1800:0x1000)+4*bank,expected=bank<7?(kind?1u:0x1f000u):0;
        for(unsigned group=0;group<3;group++)if(enabled_group(s,group)&&s->leases[group].bank==bank)expected=kind?s->bank_cba2r[group]:s->bank_cbar[group];
        if((rc=equal(s,off,expected)))return rc;
    }
    if((rc=equal(s,0x80000,s->cb0)))return rc;
    /* A new peer context may not silently take our bank or selected ASID,
     * even when it has not published a stream route. Enabled unowned banks
     * are scanned independently of the stream/CBAR bookkeeping. */
    for(unsigned bank=1;bank<83;bank++){
        uint32_t cb=0x80000+4096*bank,ctl=0,fsr=0;if((rc=rd(s,cb,&ctl))||(rc=rd(s,cb+0x58,&fsr)))return rc;
        if(fsr&0xc00001feu)return -5;int ours=-1;
        for(unsigned group=0;group<3;group++)if(enabled_group(s,group)&&s->leases[group].bank==bank)ours=(int)group;
        if(ours>=0){if(ctl!=s->bank_sctlr[ours])return -116;}
        else if(ctl&1){uint32_t low=0,high=0,cbar=0,cba2r=0;
            if((rc=rd(s,0x1000+4*bank,&cbar))||(rc=rd(s,0x1800+4*bank,&cba2r))||(rc=rd(s,cb+0x24,&low))||(rc=rd(s,cb+0x2c,&high)))return rc;
            if((cbar>>16&3)!=1||cba2r!=1)return -95;
            for(unsigned group=0;group<3;group++)if(enabled_group(s,group)&&(s->leases[group].asid==low>>16||s->leases[group].asid==high>>16))return -16;
        }
    }return 0;
}
static int write_guard(struct shared *s){
    unsigned group=s->write_group;if(!enabled_group(s,group)||!s->write_mode)return -13;
    int cleanup=s->write_mode==2,rc=cleanup?admit(s,1):early(s);
    if(!rc&&!cleanup)rc=elden_smmu_boot_routes_early(s->ops.context,s->config.epoch);
    if(!rc)rc=result(s->ops.bank(s->ops.context,s->config.owner,s->config.epoch,group,&s->leases[group],cleanup));
    if(!rc)rc=result(s->ops.quiesced(s->ops.context,s->config.owner,s->config.epoch,group));
    if(!rc)rc=result(s->ops.memory(s->ops.context,s->config.owner,s->config.epoch,group,&s->memory[group],cleanup));
    if(!rc&&!(s->irq_drained&bit(group)))rc=result(s->ops.irq(s->ops.context,s->config.owner,s->config.epoch,group,s->leases[group].bank,2));
    if(!rc)rc=layout_observe(s,s->status.route_committed);return rc;
}
int elden_smmu_shared_commit(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch){int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch))return leave(s,-116);if(s->status.route_committed||s->status.installed!=s->config.groups)return leave(s,-16);rc=early(s);if(rc)return leave(s,rc);
    for(unsigned group=0;group<3;group++)if(enabled_group(s,group)){
        rc=result(s->ops.quiesced(s->ops.context,owner,epoch,group));if(rc)return leave(s,failed(s,rc));
        rc=prepared_observe(s,group);if(rc)return leave(s,failed(s,rc));}
    s->status.stage=6;rc=layout_observe(s,0);if(rc)return leave(s,failed(s,rc));
    for(unsigned i=0;i<s->status.route_count;i++){const EldenSmmuBootRoute *r=&s->routes[i];
        for(unsigned group=0;group<3;group++)if(enabled_group(s,group)&&s->leases[group].bank==r->bank)s->write_group=group;
        s->write_mode=1;
        if((rc=equal(s,0x800+4*r->slot,0))||(rc=equal(s,0xc00+4*r->slot,InvalidS2cr))||
           (rc=put(s,0xc00+4*r->slot,r->s2cr))||(rc=put(s,0x800+4*r->slot,r->smr))){s->write_mode=0;return leave(s,failed(s,rc));}}
    s->write_mode=0;
    /* Routes are now installed, but publication is still closed. Re-audit
     * every actual context/root/table after the route writes; a prior table
     * observation cannot prove that a post-write image stayed unchanged. */
    rc=layout_observe(s,0);if(rc)return leave(s,failed(s,rc));
    for(unsigned group=0;group<3;group++)if(enabled_group(s,group)){
        rc=result(s->ops.quiesced(s->ops.context,owner,epoch,group));
        if(!rc)rc=prepared_observe(s,group);if(rc)return leave(s,failed(s,rc));
    }
    EldenSmmuBootRoute observed[6];for(unsigned i=0;i<s->status.route_count;i++){observed[i]=s->routes[i];EldenSmmuBootRoute *r=&observed[i];
        if((rc=rd(s,0x800+4*r->slot,&r->smr))||(rc=rd(s,0xc00+4*r->slot,&r->s2cr))||
           (rc=rd(s,0x1000+4*r->bank,&r->cbar))||(rc=rd(s,0x1800+4*r->bank,&r->cba2r)))return leave(s,failed(s,rc));}
    rc=early(s);if(!rc)rc=elden_smmu_boot_routes_commit(s->ops.context,epoch,observed,s->status.route_count);if(rc)return leave(s,failed(s,rc));
    s->status.route_committed=1;s->status.stage=7;rc=routes_observe(s);
    if(!rc)__atomic_store_n(&s->normal_groups,s->status.active,__ATOMIC_RELEASE);return leave(s,rc?failed(s,rc):0);
}
int elden_smmu_shared_admit(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group,int cleanup){
    if(!storage||__atomic_load_n(&core(storage)->magic,__ATOMIC_ACQUIRE)!=0x534d5348||(cleanup!=0&&cleanup!=1))return -22;struct shared *s=core(storage);
    if(identity(s,owner,epoch)||!enabled_group(s,group))return -116;
    if(!cleanup){int error=__atomic_load_n(&s->admission_error,__ATOMIC_ACQUIRE);if(error)return error;
        if(!(__atomic_load_n(&s->normal_groups,__ATOMIC_ACQUIRE)&bit(group)))return -19;}
    return result(s->ops.admit(s->ops.context,owner,epoch,cleanup));
}
int elden_smmu_shared_observe(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group,int cleanup){
    if(cleanup!=0&&cleanup!=1)return -22;
    int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);rc=elden_smmu_shared_admit(storage,owner,epoch,group,cleanup);if(rc)return leave(s,rc);
    rc=routes_observe(s);if(!rc)rc=result(s->ops.memory(s->ops.context,owner,epoch,group,&s->memory[group],cleanup));
    if(!rc)rc=result(s->ops.irq(s->ops.context,owner,epoch,group,s->leases[group].bank,2));
    if(!rc)rc=result(s->ops.bank(s->ops.context,owner,epoch,group,&s->leases[group],cleanup));
    if(!rc){s->write_mode=cleanup?2u:0u;EldenWifiSmmu4KIo io=program_io(s);int status=EldenWifiSmmu4KObserveOwnedContext(&io,s->leases[group].bank,&s->context[group],&s->program[group]);s->write_mode=0;if(status)rc=-100-status;}
    if(!rc)rc=routes_observe(s);return leave(s,rc?failed(s,rc):0);
}
int elden_smmu_shared_retire(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,unsigned group){int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch)||!enabled_group(s,group))return leave(s,-116);if(s->status.retired&bit(group))return leave(s,0);rc=admit(s,1);
    __atomic_fetch_and(&s->normal_groups,~(unsigned)bit(group),__ATOMIC_RELEASE);
    if(!rc)rc=result(s->ops.quiesced(s->ops.context,owner,epoch,group));if(rc)return leave(s,rc);
    if(s->status.route_committed){rc=routes_observe(s);if(rc)return leave(s,rc);}
    if(!(s->irq_drained&bit(group))){rc=result(s->ops.irq(s->ops.context,owner,epoch,group,s->leases[group].bank,0));if(rc)return leave(s,rc);s->irq_drained|=bit(group);}
    if(s->attempted&bit(group)){s->write_group=group;s->write_mode=2;EldenWifiSmmu4KIo io=program_io(s);
        int status=EldenWifiSmmu4KDisableOwnedContext(&io,s->leases[group].bank,&s->context[group],1000,&s->program[group]);s->write_mode=0;
        if(status)return leave(s,-100-status);if(!s->program[group].disabled_verified||!s->program[group].tlb_synchronized)return leave(s,-71);}
    s->status.active&=~bit(group);
    if(!(s->status.active&s->config.groups)){
        rc=result(s->ops.irq(s->ops.context,owner,epoch,group,s->leases[group].bank,3));if(rc)return leave(s,rc);
    }
    s->status.retired|=bit(group);return leave(s,0);
}
int elden_smmu_shared_status(EldenSmmuShared *storage,EldenSmmuSharedStatus *out){if(!out)return -22;int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);*out=s->status;return leave(s,0);}
int elden_smmu_shared_configuration(EldenSmmuShared *storage,uint64_t owner,uint64_t epoch,EldenSmmuSharedConfig *out){
    if(!out)return -22;int rc=enter(storage);if(rc)return rc;struct shared *s=core(storage);
    if(identity(s,owner,epoch))return leave(s,-116);*out=s->config;return leave(s,0);
}
