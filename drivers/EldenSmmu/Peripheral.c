/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Peripheral.h"
#include <stddef.h>
struct peripheral {
    uint32_t magic,live[3],reserved[3],installed[3];int error;
    EldenSmmuShared *shared;EldenSmmuSharedConfig config;EldenSmmuPeripheralOps ops;
    EldenCameraDmaView camera;EldenSensorMemoryView sensor;
};
_Static_assert(sizeof(struct peripheral)<=sizeof(EldenSmmuPeripheral),"peripheral adapter storage");
static struct peripheral *pget(void *p){return p;}
static int normalize(int rc){return rc<=0?rc:-13;}
static int fail(struct peripheral *p,int rc){int none=0;rc=rc<0?rc:-13;
    __atomic_compare_exchange_n(&p->error,&none,rc,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    __atomic_store_n(&p->live[1],0u,__ATOMIC_RELEASE);__atomic_store_n(&p->live[2],0u,__ATOMIC_RELEASE);return rc;}
static int base(struct peripheral *p,uint64_t owner,uint64_t epoch,unsigned group,int cleanup){
    if(!p||__atomic_load_n(&p->magic,__ATOMIC_ACQUIRE)!=0x534d5046||owner!=p->config.owner||epoch!=p->config.epoch||group<1||group>2||!(p->config.groups&(1u<<group)))return -116;
    if(cleanup!=0&&cleanup!=1)return -22;
    if(!cleanup){int rc=__atomic_load_n(&p->error,__ATOMIC_ACQUIRE);if(rc)return rc;}
    return normalize(p->ops.admit(p->ops.context,owner,epoch,group,cleanup));
}
static int group_admit(struct peripheral *p,uint64_t owner,uint64_t epoch,unsigned group,int cleanup){
    int rc=base(p,owner,epoch,group,cleanup);if(rc)return rc;
    if(cleanup)return elden_smmu_shared_admit(p->shared,owner,epoch,group,1);
    return __atomic_load_n(&p->live[group],__ATOMIC_ACQUIRE)?elden_smmu_shared_admit(p->shared,owner,epoch,group,0):elden_smmu_shared_early(p->shared,owner,epoch);
}
static int camera_admit(void *v,uint64_t o,uint64_t e,int cleanup){return group_admit(v,o,e,1,cleanup);}
static int sensor_admit(void *v,uint64_t o,uint64_t e,int cleanup){return group_admit(v,o,e,2,cleanup);}
static int camera_early(void *v,uint64_t o,uint64_t e){struct peripheral *p=v;int rc=base(p,o,e,1,0);return rc?rc:elden_smmu_shared_early(p->shared,o,e);}
static int camera_reserve(void *v,uint64_t o,uint64_t e,const EldenCameraDmaConfig *c,uint64_t bytes,EldenCameraDmaLease *out){
    struct peripheral *p=v;int rc=camera_early(v,o,e);if(rc)return rc;
    if(!c||!out||c->iova!=p->config.apertures[1].iova||!bytes||bytes>p->config.apertures[1].bytes)return -22;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&p->reserved[1],&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    EldenSmmuSharedLease l;rc=elden_smmu_shared_lease(p->shared,o,e,1,&l);if(rc)return fail(p,rc);
    *out=(EldenCameraDmaLease){l.id0,l.id1,l.id2,l.bank,l.first_slot,l.asid,l.route_epoch};return 0;
}
static int sensor_reserve(void *v,uint64_t o,uint64_t e,const EldenSensorMemoryConfig *c,EldenSensorMemoryLease *out){
    struct peripheral *p=v;int rc=base(p,o,e,2,0);if(!rc)rc=elden_smmu_shared_early(p->shared,o,e);if(rc)return rc;
    if(!c||!out||c->session!=p->config.sensor_session||c->iova!=p->config.apertures[2].iova||!c->bytes||c->bytes>p->config.apertures[2].bytes)return -22;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&p->reserved[2],&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    EldenSmmuSharedLease l;rc=elden_smmu_shared_lease(p->shared,o,e,2,&l);if(rc)return fail(p,rc);
    *out=(EldenSensorMemoryLease){l.id0,l.id2,l.asid,l.bank,l.route_epoch};return 0;
}
static int descriptor(void *v,uint64_t pa,uint64_t *out){
    EldenWifiSmmu4KTableMemory *memory=v;EldenWifiSmmu4KTableIo io;
    if(EldenWifiSmmu4KBindTableMemory(memory,&io))return -71;
    return io.read_descriptor(io.cookie,pa,out);
}
/* Table readers are retained independently of caller stack storage. */
static int camera_descriptor(void *v,uint64_t pa,uint64_t *out){struct peripheral *p=v;
    EldenWifiSmmu4KTableMemory m={.count=1,.pools={{p->camera.table_mapping,p->camera.table_physical,p->camera.table_bytes/4096}}};return descriptor(&m,pa,out);}
static int sensor_descriptor(void *v,uint64_t pa,uint64_t *out){struct peripheral *p=v;
    EldenWifiSmmu4KTableMemory m={.count=1,.pools={{p->sensor.table_mapping,p->sensor.table_physical,p->sensor.table_bytes/4096}}};return descriptor(&m,pa,out);}
static int camera_match(struct peripheral *p,const EldenCameraDmaView *v){
    const EldenCameraDmaView *b=&p->camera;
    if(!v||v->owner!=b->owner||v->epoch!=b->epoch||v->iova!=b->iova||v->allocation_bytes!=b->allocation_bytes||v->data_physical!=b->data_physical||v->table_physical!=b->table_physical||
       v->table_mapping!=b->table_mapping||v->table_bytes!=b->table_bytes||v->count!=b->count||v->stream_id!=0x1c00||v->lease.route_epoch!=b->lease.route_epoch||v->lease.bank!=b->lease.bank||v->lease.slot!=b->lease.slot||v->lease.asid!=b->lease.asid)return -116;
    for(unsigned i=0;i<v->count;i++){const EldenCameraDmaSlot *a=&v->slots[i],*x=&b->slots[i];
        if(a->physical!=x->physical||a->mapped_bytes!=x->mapped_bytes||a->sensor!=x->sensor||a->stride!=x->stride||a->buffer.id!=x->buffer.id||a->buffer.owner!=x->buffer.owner||a->buffer.epoch!=x->buffer.epoch||a->buffer.route_epoch!=x->buffer.route_epoch||a->buffer.iova!=x->buffer.iova||a->buffer.bytes!=x->buffer.bytes||a->buffer.mapping!=x->buffer.mapping)return -116;}
    return 0;
}
static int sensor_match(struct peripheral *p,const EldenSensorMemoryView *v){const EldenSensorMemoryView *b=&p->sensor;
    if(!v||v->memory.arena!=b->memory.arena||v->memory.iova!=b->memory.iova||v->memory.bytes!=b->memory.bytes||v->memory.session!=b->memory.session||v->memory.process!=b->memory.process||
       v->data_physical!=b->data_physical||v->table_physical!=b->table_physical||v->table_mapping!=b->table_mapping||v->table_bytes!=b->table_bytes||v->lease.route_epoch!=b->lease.route_epoch||v->lease.bank!=b->lease.bank||v->lease.asid!=b->lease.asid)return -116;
    for(unsigned i=0;i<4;i++)if(v->stream_ids[i]!=b->stream_ids[i])return -116;return 0;
}
static int camera_install(void *v,uint64_t o,uint64_t e,const EldenCameraDmaView *view){struct peripheral *p=v;int rc=camera_early(v,o,e);if(rc)return rc;
    if(!view||!p->reserved[1]||p->installed[1]||view->count<2||view->count>8||view->owner!=o||view->epoch!=e||view->stream_id!=0x1c00||!view->table_bytes||(view->table_bytes&4095)||view->table_bytes/4096>68)return -22;
    EldenSmmuSharedLease l;rc=elden_smmu_shared_lease(p->shared,o,e,1,&l);if(rc)return rc;
    if(view->lease.bank!=l.bank||view->lease.slot!=l.first_slot||view->lease.asid!=l.asid||view->lease.route_epoch!=l.route_epoch)return -116;
    rc=normalize(p->ops.camera_memory(p->ops.context,view,0));if(rc)return fail(p,rc);p->camera=*view;
    EldenSmmuSharedMemory m={.config=view->table_config,.tables=view->tables,.range_count=view->count,.capacity_pages=view->table_bytes/4096,.io={p,camera_descriptor},.data_physical=view->data_physical,.data_bytes=view->allocation_bytes};
    for(unsigned i=0;i<view->count;i++)m.ranges[i]=(EldenWifiSmmu4KRange){view->slots[i].buffer.iova,view->slots[i].physical,view->slots[i].mapped_bytes,3,0,3};
    rc=elden_smmu_shared_install(p->shared,o,e,1,&m);if(rc)return fail(p,rc);__atomic_store_n(&p->installed[1],1u,__ATOMIC_RELEASE);return 0;
}
static int sensor_install(void *v,uint64_t o,uint64_t e,const EldenSensorMemoryView *view){struct peripheral *p=v;int rc=base(p,o,e,2,0);if(!rc)rc=elden_smmu_shared_early(p->shared,o,e);if(rc)return rc;
    if(!view||!p->reserved[2]||p->installed[2]||view->memory.session!=p->config.sensor_session||!view->table_bytes||(view->table_bytes&4095)||view->table_bytes/4096>68)return -22;
    EldenSmmuSharedLease l;rc=elden_smmu_shared_lease(p->shared,o,e,2,&l);if(rc)return rc;
    if(view->lease.bank!=l.bank||view->lease.asid!=l.asid||view->lease.route_epoch!=l.route_epoch)return -116;
    const unsigned bases[]={0x1000,0x1040,0x1060,0x1080};for(unsigned i=0;i<4;i++)if(view->stream_ids[i]!=bases[i]+p->config.sensor_session)return -116;
    rc=normalize(p->ops.sensor_memory(p->ops.context,view,0));if(rc)return fail(p,rc);p->sensor=*view;
    EldenSmmuSharedMemory m={.config=view->table_config,.tables=view->tables,.ranges={{view->memory.iova,view->data_physical,view->memory.bytes,3,0,3}},.range_count=1,.capacity_pages=view->table_bytes/4096,.io={p,sensor_descriptor},.data_physical=view->data_physical,.data_bytes=view->memory.bytes};
    rc=elden_smmu_shared_install(p->shared,o,e,2,&m);if(rc)return fail(p,rc);__atomic_store_n(&p->installed[2],1u,__ATOMIC_RELEASE);return 0;
}
static int camera_prepared(void *v,uint64_t o,uint64_t e,const EldenCameraDmaView *view){struct peripheral *p=v;int rc=camera_early(v,o,e);
    if(!rc)rc=camera_match(p,view);if(!rc)rc=normalize(p->ops.camera_memory(p->ops.context,view,0));if(!rc)rc=elden_smmu_shared_observe_prepared(p->shared,o,e,1);return rc?fail(p,rc):0;}
static int sensor_prepared(void *v,uint64_t o,uint64_t e,const EldenSensorMemoryView *view){struct peripheral *p=v;int rc=base(p,o,e,2,0);
    if(!rc)rc=sensor_match(p,view);if(!rc)rc=normalize(p->ops.sensor_memory(p->ops.context,view,0));if(!rc)rc=elden_smmu_shared_observe_prepared(p->shared,o,e,2);return rc?fail(p,rc):0;}
static int camera_observe(void *v,uint64_t o,uint64_t e,const EldenCameraDmaView *view,const EldenCameraBuffer *b,int completion){struct peripheral *p=v;
    int rc=base(p,o,e,1,0);if(!rc&&!__atomic_load_n(&p->live[1],__ATOMIC_ACQUIRE))rc=-19;if(rc)return rc;
    rc=camera_match(p,view);if(!rc)rc=normalize(p->ops.camera_memory(p->ops.context,view,0));if(!rc)rc=elden_smmu_shared_observe(p->shared,o,e,1,0);
    if(!rc&&completion){int found=0;if(b)for(unsigned i=0;i<view->count;i++){const EldenCameraBuffer *x=&view->slots[i].buffer;
        if(b->id==x->id&&b->owner==o&&b->epoch==e&&b->route_epoch==x->route_epoch&&b->iova==x->iova&&b->bytes==x->bytes&&b->mapping==x->mapping)found=1;}
        rc=found?normalize(p->ops.camera_visible(p->ops.context,view,b)):-116;}
    return rc?fail(p,rc):0;
}
static int sensor_observe(void *v,uint64_t o,uint64_t e,const EldenSensorMemoryView *view,int cleanup){struct peripheral *p=v;
    int rc=base(p,o,e,2,cleanup);if(!rc&&!cleanup&&!__atomic_load_n(&p->live[2],__ATOMIC_ACQUIRE))rc=-19;if(rc)return rc;
    rc=sensor_match(p,view);if(!rc)rc=normalize(p->ops.sensor_memory(p->ops.context,view,cleanup));if(!rc)rc=elden_smmu_shared_observe(p->shared,o,e,2,cleanup);return rc?fail(p,rc):0;
}
static int camera_retire(void *v,uint64_t o,uint64_t e,const EldenCameraDmaView *view,unsigned reservation,unsigned install){struct peripheral *p=v;int rc=base(p,o,e,1,1);if(rc)return rc;
    if(reservation>1||install>reservation||!view)return -22;
    if(p->installed[1]){rc=camera_match(p,view);if(!rc)rc=normalize(p->ops.camera_memory(p->ops.context,view,1));if(rc)return rc;}
    __atomic_store_n(&p->live[1],0u,__ATOMIC_RELEASE);return elden_smmu_shared_retire(p->shared,o,e,1);
}
static int sensor_withdraw(void *v,uint64_t o,uint64_t e,const EldenSensorMemoryView *view){struct peripheral *p=v;int rc=base(p,o,e,2,1);if(rc)return rc;
    if(p->installed[2]){rc=sensor_match(p,view);if(!rc)rc=normalize(p->ops.sensor_memory(p->ops.context,view,1));if(rc)return rc;}
    __atomic_store_n(&p->live[2],0u,__ATOMIC_RELEASE);return elden_smmu_shared_retire(p->shared,o,e,2);
}
int elden_smmu_peripheral_open(EldenSmmuPeripheral *storage,EldenSmmuShared *shared,const EldenSmmuSharedConfig *c,const EldenSmmuPeripheralOps *ops){
    if(!storage||!shared||!c||!ops||!ops->context||!ops->admit||!(c->groups&6)||((c->groups&2)&&(!ops->camera_memory||!ops->camera_visible))||((c->groups&4)&&!ops->sensor_memory))return -22;
    int rc=elden_smmu_shared_early(shared,c->owner,c->epoch);if(rc)return rc;struct peripheral *p=pget(storage);
    EldenSmmuSharedStatus status;rc=elden_smmu_shared_status(shared,&status);if(rc)return rc;if(status.groups!=c->groups||status.route_committed)return -116;
    EldenSmmuSharedConfig actual;rc=elden_smmu_shared_configuration(shared,c->owner,c->epoch,&actual);if(rc)return rc;
    if(actual.groups!=c->groups||actual.sensor_session!=c->sensor_session)return -116;
    for(unsigned group=0;group<3;group++)if(actual.apertures[group].iova!=c->apertures[group].iova||actual.apertures[group].bytes!=c->apertures[group].bytes)return -116;
    uint32_t empty=0;if(!__atomic_compare_exchange_n(&p->magic,&empty,0x534d5052,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    uint8_t *b=(uint8_t *)storage;for(unsigned i=4;i<sizeof(*storage);i++)b[i]=0;p->shared=shared;p->config=*c;p->ops=*ops;
    __atomic_store_n(&p->magic,0x534d5046u,__ATOMIC_RELEASE);return 0;
}
int elden_smmu_camera_owner(EldenSmmuPeripheral *s,EldenCameraDmaOwner *out){struct peripheral *p=pget(s);if(!p||!out)return -22;if(__atomic_load_n(&p->magic,__ATOMIC_ACQUIRE)!=0x534d5046)return -19;int rc=base(p,p->config.owner,p->config.epoch,1,0);if(rc)return rc;
    *out=(EldenCameraDmaOwner){p,p->config.owner,p->config.epoch,camera_admit,camera_early,camera_reserve,camera_install,camera_observe,camera_retire,camera_prepared};return 0;}
int elden_smmu_sensor_owner(EldenSmmuPeripheral *s,EldenSensorMemoryOwner *out){struct peripheral *p=pget(s);if(!p||!out)return -22;if(__atomic_load_n(&p->magic,__ATOMIC_ACQUIRE)!=0x534d5046)return -19;int rc=base(p,p->config.owner,p->config.epoch,2,0);if(rc)return rc;
    *out=(EldenSensorMemoryOwner){p,p->config.owner,p->config.epoch,sensor_admit,sensor_reserve,sensor_install,sensor_observe,sensor_withdraw,sensor_prepared};return 0;}
int elden_smmu_peripheral_activate(EldenSmmuPeripheral *storage,uint64_t o,uint64_t e){struct peripheral *p=pget(storage);
    if(!p||__atomic_load_n(&p->magic,__ATOMIC_ACQUIRE)!=0x534d5046||o!=p->config.owner||e!=p->config.epoch)return -116;
    int error=__atomic_load_n(&p->error,__ATOMIC_ACQUIRE);if(error)return error;EldenSmmuSharedStatus status;int rc=elden_smmu_shared_status(p->shared,&status);if(rc)return rc;
    if(!status.route_committed||status.groups!=p->config.groups||status.installed!=status.groups)return -19;
    for(unsigned group=1;group<3;group++)if(p->config.groups&(1u<<group)){
        if(!p->installed[group]||__atomic_load_n(&p->live[group],__ATOMIC_ACQUIRE))return -16;
        rc=base(p,o,e,group,0);if(!rc)rc=elden_smmu_shared_observe(p->shared,o,e,group,0);if(rc)return fail(p,rc);
    }
    for(unsigned group=1;group<3;group++)if(p->config.groups&(1u<<group))__atomic_store_n(&p->live[group],1u,__ATOMIC_RELEASE);return 0;
}
