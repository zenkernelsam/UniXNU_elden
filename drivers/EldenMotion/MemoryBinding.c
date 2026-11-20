/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "MemoryBinding.h"
struct EldenSensorMemoryBinding {
    EldenSensorEndpoint endpoint;EldenSensorNativeMemory *memory;EldenSensorMemoryView view;
    EldenSensorPackBinding *pack;EldenSensorRpcBus bus;uint32_t stopping,withdrawn,finishing;int error;
};
static int same(EldenSensorMemoryBinding *c,uint64_t owner,uint64_t epoch){return c&&owner==c->endpoint.owner&&epoch==c->endpoint.epoch;}
static int admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){EldenSensorMemoryBinding *c=v;
    if(!same(c,owner,epoch))return -116;if(!cleanup&&(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE)||c->error))return c->error?c->error:-125;
    int rc=c->endpoint.admit(c->endpoint.context,owner,epoch,cleanup);if(rc)return rc<0?rc:-13;
    return c->memory?elden_sensor_memory_native_admit(c->memory,owner,epoch,cleanup):-19;
}
static uint64_t now(void *v){EldenSensorMemoryBinding *c=v;return c->endpoint.now_ms(c->endpoint.context);}
static int send(void *v,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){EldenSensorMemoryBinding *c=v;int rc=admit(c,owner,epoch,0);return rc?rc:c->endpoint.send_epoch(c->endpoint.context,owner,epoch,p,n);}
static void wait(void *v,uint32_t ms){EldenSensorMemoryBinding *c=v;if(!__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))c->endpoint.wait(c->endpoint.context,ms);}
static int publish(void *v,uint64_t owner,uint64_t epoch){EldenSensorMemoryBinding *c=v;return c->memory?elden_sensor_memory_native_publish(c->memory,owner,epoch):-19;}
static int prepare(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *memory){EldenSensorMemoryBinding *c=v;EldenSensorMemoryView view;
    int rc=c->memory?elden_sensor_memory_native_view(c->memory,owner,epoch,&view):-19;if(rc)return rc;
    if(view.memory.arena!=memory->arena||view.memory.iova!=memory->iova||view.memory.bytes!=memory->bytes||view.memory.session!=memory->session||view.memory.process!=memory->process)return -116;return 0;
}
static int reserve(void *v,uint64_t owner,uint64_t epoch,uint64_t *out){EldenSensorMemoryBinding *c=v;int rc=admit(c,owner,epoch,0);return rc?rc:c->endpoint.reserve_cookie(c->endpoint.context,owner,epoch,out);}
static int complete(void *v,uint64_t owner,uint64_t epoch,uint64_t cookie){EldenSensorMemoryBinding *c=v;int rc=admit(c,owner,epoch,0);if(rc)return rc;
    rc=c->endpoint.complete_cookie(c->endpoint.context,owner,epoch,cookie);if(rc)return rc;
    return elden_sensor_memory_native_publish(c->memory,owner,epoch); // real NC memory/context barrier before reading DSP outputs
}
static void *allocate(void *v,uint32_t n){EldenSensorMemoryBinding *c=v;return c->endpoint.allocate(c->endpoint.context,n);}
static void trace(void *v,const char *s){EldenSensorMemoryBinding *c=v;if(c->endpoint.trace)c->endpoint.trace(c->endpoint.context,s);}
static void wake(void *v){EldenSensorMemoryBinding *c=v;c->endpoint.wake(c->endpoint.context);}
static int withdraw(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){EldenSensorMemoryBinding *c=v;(void)m;
    if(!same(c,owner,epoch))return -116;
    int rc=0;if(!__atomic_load_n(&c->withdrawn,__ATOMIC_ACQUIRE)){rc=c->endpoint.withdraw(c->endpoint.context,owner,epoch);if(!rc)__atomic_store_n(&c->withdrawn,1u,__ATOMIC_RELEASE);}
    if(rc)return rc<0?rc:-13;return c->memory?elden_sensor_memory_native_stop(c->memory,owner,epoch):0;
}
int elden_sensor_memory_binding_open(const EldenSensorMemoryConfig *cfg,const EldenSensorMemoryOwner *owner,
    const EldenSensorEndpoint *endpoint,const EldenSensorPackSeal *seal,const EldenSensorPackAuthority *authority,EldenSensorMemoryBinding **out){
    if(!cfg||!owner||!endpoint||!seal||!authority||!out||*out||!endpoint->owner||!endpoint->epoch||
       owner->owner!=endpoint->owner||owner->epoch!=endpoint->epoch||authority->owner!=endpoint->owner||authority->epoch!=endpoint->epoch||
       !endpoint->admit||!endpoint->now_ms||!endpoint->send_epoch||!endpoint->wait||!endpoint->reserve_cookie||!endpoint->complete_cookie||
       !endpoint->allocate||!endpoint->withdraw||!endpoint->wake)return -22;
    int rc=endpoint->admit(endpoint->context,endpoint->owner,endpoint->epoch,0);if(rc)return rc<0?rc:-13;
    EldenSensorMemoryBinding *c=endpoint->allocate(endpoint->context,sizeof(*c));if(!c)return -12;uint8_t *b=(uint8_t *)c;for(unsigned i=0;i<sizeof(*c);i++)b[i]=0;
    c->endpoint=*endpoint;*out=c;rc=elden_sensor_memory_native_open(cfg,owner,&c->memory);if(rc){c->error=rc;return rc;}
    rc=elden_sensor_memory_native_view(c->memory,endpoint->owner,endpoint->epoch,&c->view);if(rc){c->error=rc;return rc;}
    c->bus=(EldenSensorRpcBus){c,endpoint->owner,endpoint->epoch,admit,now,send,wait,publish,prepare,reserve,complete,allocate,trace,withdraw,wake};
    rc=elden_sensor_pack_binding_open(&c->bus,&c->view.memory,seal,authority,&c->pack);if(rc)c->error=rc;return rc;
}
int elden_sensor_memory_binding_channel(EldenSensorMemoryBinding *c,uint64_t owner,uint64_t epoch,int up){if(!same(c,owner,epoch))return -116;return c->pack?elden_sensor_pack_binding_channel(c->pack,owner,epoch,up):c->error?c->error:-19;}
int elden_sensor_memory_binding_receive(EldenSensorMemoryBinding *c,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){if(!same(c,owner,epoch))return -116;return c->pack?elden_sensor_pack_binding_receive(c->pack,owner,epoch,p,n):c->error?c->error:-19;}
int elden_sensor_memory_binding_step(EldenSensorMemoryBinding *c){return c&&c->pack?elden_sensor_pack_binding_step(c->pack):c&&c->error?c->error:-19;}
int elden_sensor_memory_binding_request_stop(EldenSensorMemoryBinding *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;__atomic_store_n(&c->stopping,1u,__ATOMIC_RELEASE);c->endpoint.wake(c->endpoint.context);
    return c->pack?elden_sensor_pack_binding_request_stop(c->pack,owner,epoch):0;
}
int elden_sensor_memory_binding_finish(EldenSensorMemoryBinding *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;int rc=elden_sensor_memory_binding_request_stop(c,owner,epoch);if(rc)return rc;
    uint32_t idle=0;if(!__atomic_compare_exchange_n(&c->finishing,&idle,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -11;
    rc=c->pack?elden_sensor_pack_binding_finish(c->pack,owner,epoch):withdraw(c,owner,epoch,&c->view.memory);
    __atomic_store_n(&c->finishing,0u,__ATOMIC_RELEASE);return rc;
}
int elden_sensor_memory_binding_open_prepared(EldenSensorNativeMemory *memory,const EldenSensorEndpoint *endpoint,
    const EldenSensorPackSeal *seal,const EldenSensorPackAuthority *authority,EldenSensorMemoryBinding **out){
    if(!memory||!endpoint||!seal||!authority||!out||*out||!endpoint->owner||!endpoint->epoch||
       authority->owner!=endpoint->owner||authority->epoch!=endpoint->epoch||!endpoint->admit||!endpoint->now_ms||
       !endpoint->send_epoch||!endpoint->wait||!endpoint->reserve_cookie||!endpoint->complete_cookie||
       !endpoint->allocate||!endpoint->withdraw||!endpoint->wake)return -22;
    int rc=endpoint->admit(endpoint->context,endpoint->owner,endpoint->epoch,0);if(rc)return rc<0?rc:-13;
    EldenSensorMemoryView view;rc=elden_sensor_memory_native_view(memory,endpoint->owner,endpoint->epoch,&view);if(rc)return rc;
    EldenSensorMemoryBinding *c=endpoint->allocate(endpoint->context,sizeof(*c));if(!c)return -12;
    uint8_t *bytes=(uint8_t *)c;for(unsigned i=0;i<sizeof(*c);i++)bytes[i]=0;
    c->endpoint=*endpoint;c->memory=memory;c->view=view;*out=c;
    c->bus=(EldenSensorRpcBus){c,endpoint->owner,endpoint->epoch,admit,now,send,wait,publish,prepare,reserve,complete,allocate,trace,withdraw,wake};
    rc=elden_sensor_pack_binding_open(&c->bus,&view.memory,seal,authority,&c->pack);if(rc)c->error=rc;return rc;
}
int elden_sensor_memory_binding_status(EldenSensorMemoryBinding *c,EldenSensorRpcStatus *rpc,EldenSensorPackStatus *pack){
    if(!c||!rpc||!pack)return -22;return c->pack?elden_sensor_pack_binding_status(c->pack,rpc,pack):c->error?c->error:-19;
}
