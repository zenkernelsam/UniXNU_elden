/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Connect actual native sealed-file reads to actual source-derived RPC core. */
#include "PackBinding.h"
struct EldenSensorPackBinding {
    EldenSensorRpcBus bus;EldenSensorRpcProvider provider;EldenSensorRpcMemory memory;
    EldenSensorPackCache *pack;EldenSensorRpc *rpc;uint32_t stop,closed,finishing;int error;
};
static int same(EldenSensorPackBinding *c,uint64_t owner,uint64_t epoch){return c&&c->bus.owner==owner&&c->bus.epoch==epoch;}
static int check(void *v,uint64_t owner,uint64_t epoch,int cleanup){
    EldenSensorPackBinding *c=v;if(!same(c,owner,epoch))return -116;
    if(!cleanup&&(__atomic_load_n(&c->stop,__ATOMIC_ACQUIRE)||c->error))return c->error?c->error:-125;
    int rc=c->bus.admit(c->bus.context,owner,epoch,cleanup);return rc<=0?rc:-13;
}
static uint64_t now(void *v){EldenSensorPackBinding *c=v;return c->bus.now_ms(c->bus.context);}
static int send(void *v,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){EldenSensorPackBinding *c=v;int rc=check(c,owner,epoch,0);return rc?rc:c->bus.send_epoch(c->bus.context,owner,epoch,p,n);}
static void wait(void *v,uint32_t ms){EldenSensorPackBinding *c=v;if(!__atomic_load_n(&c->stop,__ATOMIC_ACQUIRE))c->bus.wait(c->bus.context,ms);}
static int publish(void *v,uint64_t owner,uint64_t epoch){EldenSensorPackBinding *c=v;int rc=check(c,owner,epoch,0);return rc?rc:c->bus.publish(c->bus.context,owner,epoch);}
static int prepare(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){EldenSensorPackBinding *c=v;int rc=check(c,owner,epoch,0);return rc?rc:c->bus.prepare(c->bus.context,owner,epoch,m);}
static int reserve(void *v,uint64_t owner,uint64_t epoch,uint64_t *out){EldenSensorPackBinding *c=v;int rc=check(c,owner,epoch,0);return rc?rc:c->bus.reserve_cookie(c->bus.context,owner,epoch,out);}
static int complete(void *v,uint64_t owner,uint64_t epoch,uint64_t cookie){EldenSensorPackBinding *c=v;int rc=check(c,owner,epoch,0);return rc?rc:c->bus.complete_cookie(c->bus.context,owner,epoch,cookie);}
static int size(void *v,uint64_t owner,uint64_t epoch,uint64_t *out){EldenSensorPackBinding *c=v;return c->pack?elden_sensor_pack_native_size(c->pack,owner,epoch,out):-19;}
static int read(void *v,uint64_t owner,uint64_t epoch,uint64_t off,void *out,uint32_t n){EldenSensorPackBinding *c=v;return c->pack?elden_sensor_pack_native_read(c->pack,owner,epoch,off,out,n):-19;}
static void *allocate(void *v,uint32_t n){EldenSensorPackBinding *c=v;return c->bus.allocate(c->bus.context,n);}
static void trace(void *v,const char *s){EldenSensorPackBinding *c=v;if(c->bus.trace)c->bus.trace(c->bus.context,s);}
static void wake(void *v){EldenSensorPackBinding *c=v;c->bus.wake(c->bus.context);}
static int withdraw(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){
    EldenSensorPackBinding *c=v;if(!same(c,owner,epoch))return -116;
    if(__atomic_load_n(&c->closed,__ATOMIC_ACQUIRE))return 0;
    int rc=c->bus.withdraw(c->bus.context,owner,epoch,m);if(rc)return rc<0?rc:-13;
    __atomic_store_n(&c->closed,1u,__ATOMIC_RELEASE);return 0;
}
int elden_sensor_pack_binding_open(const EldenSensorRpcBus *bus,const EldenSensorRpcMemory *memory,const EldenSensorPackSeal *seal,
                                  const EldenSensorPackAuthority *authority,EldenSensorPackBinding **out){
    if(!bus||!memory||!seal||!authority||!out||*out||!bus->owner||!bus->epoch||bus->owner!=authority->owner||bus->epoch!=authority->epoch||
       !bus->admit||!bus->now_ms||!bus->send_epoch||!bus->wait||!bus->publish||!bus->prepare||!bus->reserve_cookie||!bus->complete_cookie||
       !bus->allocate||!bus->withdraw||!bus->wake)return -22;
    int rc=bus->admit(bus->context,bus->owner,bus->epoch,0);if(rc)return rc<0?rc:-13;
    EldenSensorPackBinding *c=bus->allocate(bus->context,sizeof(*c));if(!c)return -12;
    uint8_t *b=(uint8_t *)c;for(unsigned i=0;i<sizeof(*c);i++)b[i]=0;c->bus=*bus;c->memory=*memory;*out=c;
    rc=elden_sensor_pack_native_bind(seal,authority,&c->pack);if(rc){c->error=rc;return rc;}
    c->provider=(EldenSensorRpcProvider){c,bus->owner,bus->epoch,check,now,send,wait,publish,prepare,reserve,complete,size,read,allocate,trace,withdraw,wake};
    rc=elden_sensor_rpc_open(&c->provider,memory,&c->rpc);if(rc)c->error=rc;return rc;
}
int elden_sensor_pack_binding_channel(EldenSensorPackBinding *c,uint64_t owner,uint64_t epoch,int up){if(!same(c,owner,epoch))return -116;return c->rpc?elden_sensor_rpc_channel(c->rpc,owner,epoch,up):c->error?c->error:-19;}
int elden_sensor_pack_binding_receive(EldenSensorPackBinding *c,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){if(!same(c,owner,epoch))return -116;return c->rpc?elden_sensor_rpc_receive(c->rpc,owner,epoch,p,n):c->error?c->error:-19;}
int elden_sensor_pack_binding_step(EldenSensorPackBinding *c){return c&&c->rpc?elden_sensor_rpc_step(c->rpc):c&&c->error?c->error:-19;}
int elden_sensor_pack_binding_request_stop(EldenSensorPackBinding *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;__atomic_store_n(&c->stop,1u,__ATOMIC_RELEASE);
    if(c->rpc)return elden_sensor_rpc_request_stop(c->rpc,owner,epoch);c->bus.wake(c->bus.context);return 0;
}
int elden_sensor_pack_binding_finish(EldenSensorPackBinding *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;int rc=elden_sensor_pack_binding_request_stop(c,owner,epoch);if(rc)return rc;
    uint32_t idle=0;if(!__atomic_compare_exchange_n(&c->finishing,&idle,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -11;
    rc=c->rpc?elden_sensor_rpc_finish(c->rpc,owner,epoch):withdraw(c,owner,epoch,&c->memory);
    if(!rc&&c->pack)rc=elden_sensor_pack_native_stop(c->pack,owner,epoch);
    __atomic_store_n(&c->finishing,0u,__ATOMIC_RELEASE);return rc; // retain every allocation until reset
}
int elden_sensor_pack_binding_status(EldenSensorPackBinding *c,EldenSensorRpcStatus *rpc,EldenSensorPackStatus *pack){
    if(!c||!rpc||!pack)return -22;if(!c->rpc||!c->pack)return c->error?c->error:-19;
    int rc=elden_sensor_rpc_status(c->rpc,rpc);if(rc)return rc;return elden_sensor_pack_native_status(c->pack,pack);
}
