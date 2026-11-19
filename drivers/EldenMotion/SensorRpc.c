/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Provider binding for the actual source-derived listener/file service. */
#include "SensorRpc.h"
#include "RpcPrivate.h"
struct EldenSensorRpc {
    SsrCore core;EldenSensorRpcProvider provider;EldenSensorRpcMemory memory;SsrOps ops;
    uint32_t stop_requested,terminal,executing,receiving,callbacks_closed;
    uint32_t error;uint8_t bounce[SSR_BOUNCE];
};
static int same(EldenSensorRpc *c,uint64_t owner,uint64_t epoch){return c&&c->provider.owner==owner&&c->provider.epoch==epoch;}
static int admit(EldenSensorRpc *c,int cleanup){int rc=c->provider.admit(c->provider.context,c->provider.owner,c->provider.epoch,cleanup);return rc<=0?rc:-13;}
static void remember(EldenSensorRpc *c,int error){uint32_t none=0;__atomic_compare_exchange_n(&c->error,&none,(uint32_t)(error<0?error:-13),0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
static int live(void *v){
    EldenSensorRpc *c=v;if(__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->error,__ATOMIC_ACQUIRE))return 0;
    int rc=admit(c,0);if(rc){remember(c,rc);return 0;}return 1;
}
static uint64_t now(void *v){EldenSensorRpc *c=v;return c->provider.now_ms(c->provider.context);}
static int send(void *v,const uint8_t *p,uint32_t n){EldenSensorRpc *c=v;if(!live(c))return -125;return c->provider.send_epoch(c->provider.context,c->provider.owner,c->provider.epoch,p,n);}
static void wait(void *v,uint32_t ms){EldenSensorRpc *c=v;if(live(c))c->provider.wait(c->provider.context,ms);}
static void trace(void *v,const char *s){EldenSensorRpc *c=v;if(c->provider.trace)c->provider.trace(c->provider.context,s);}
static int publish(void *v){EldenSensorRpc *c=v;if(!live(c))return -125;return c->provider.publish(c->provider.context,c->provider.owner,c->provider.epoch);}
static int prepare(void *v){EldenSensorRpc *c=v;if(!live(c))return -125;return c->provider.prepare(c->provider.context,c->provider.owner,c->provider.epoch,&c->memory);}
static int size(void *v,uint64_t *out){
    EldenSensorRpc *c=v;if(!live(c))return SsrPackError;
    int rc=c->provider.pack_size(c->provider.context,c->provider.owner,c->provider.epoch,out);if(rc||!live(c))return rc?rc:SsrPackError;return 0;
}
static int read(void *v,uint64_t off,void *out,uint32_t n){
    EldenSensorRpc *c=v;if(!live(c))return -125;int rc=c->provider.pack_read(c->provider.context,c->provider.owner,c->provider.epoch,off,out,n);
    if(rc)return rc;return live(c)?0:-125;
}
static void *allocate(void *v,uint32_t n){EldenSensorRpc *c=v;if(!live(c))return 0;return c->provider.allocate(c->provider.context,n);}
static int reserve(void *v,uint64_t *out){EldenSensorRpc *c=v;if(!live(c))return -125;return c->provider.reserve_cookie(c->provider.context,c->provider.owner,c->provider.epoch,out);}
static int complete(void *v,uint64_t cookie){EldenSensorRpc *c=v;if(!live(c))return -125;return c->provider.complete_cookie(c->provider.context,c->provider.owner,c->provider.epoch,cookie);}
int elden_sensor_rpc_open(const EldenSensorRpcProvider *p,const EldenSensorRpcMemory *m,EldenSensorRpc **out){
    if(!p||!m||!out||!p->owner||!p->epoch||!p->admit||!p->now_ms||!p->send_epoch||!p->wait||!p->publish||!p->prepare||
       !p->reserve_cookie||!p->complete_cookie||!p->pack_size||!p->pack_read||!p->allocate||!p->withdraw||!p->wake||
       !m->arena||!m->iova||(m->iova&4095)||m->bytes<=SSR_CALL_BYTES||m->bytes>0x4000000u||(m->bytes&4095)||
       m->iova>UINT64_C(0x100000000)-m->bytes||m->session<3||m->session>8||!m->process)return -22;
    int rc=p->admit(p->context,p->owner,p->epoch,0);if(rc)return rc<0?rc:-13;
    EldenSensorRpc *c=p->allocate(p->context,sizeof(*c));if(!c)return -12;
    uint8_t *b=(uint8_t *)c;for(unsigned i=0;i<sizeof(*c);i++)b[i]=0;c->provider=*p;c->memory=*m;
    c->ops=(SsrOps){c,now,send,wait,trace,publish,prepare,size,read,allocate,live,reserve,complete};
    SsrConfig config={m->arena,m->iova,m->bytes,c->bounce,m->process,m->session,1};
    elden_sensor_rpc_core_init(&c->core,&c->ops,&config);*out=c;return 0;
}
int elden_sensor_rpc_channel(EldenSensorRpc *c,uint64_t owner,uint64_t epoch,int up){
    if(!same(c,owner,epoch)||(up!=0&&up!=1))return -116;
    if(__atomic_load_n(&c->callbacks_closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return -125;
    if(__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE))return -125;
    elden_sensor_rpc_core_channel(&c->core,up);return 0;
}
int elden_sensor_rpc_receive(EldenSensorRpc *c,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){
    if(!same(c,owner,epoch))return -116;if(!p||n<12||n>64)return -84;
    if(__atomic_load_n(&c->callbacks_closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return -125;
    if(__atomic_load_n(&c->stop_requested,__ATOMIC_ACQUIRE))return -125;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->receiving,&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){remember(c,-16);return -16;}
    if(__atomic_load_n(&c->callbacks_closed,__ATOMIC_ACQUIRE)){__atomic_store_n(&c->receiving,0u,__ATOMIC_RELEASE);return -125;}
    elden_sensor_rpc_core_receive(&c->core,p,n);__atomic_store_n(&c->receiving,0u,__ATOMIC_RELEASE);return 0;
}
int elden_sensor_rpc_step(EldenSensorRpc *c){
    if(!c)return -22;uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->executing,&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    if(__atomic_load_n(&c->callbacks_closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE)){__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return 0;}
    int rc=elden_sensor_rpc_core_step(&c->core);__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return rc;
}
int elden_sensor_rpc_request_stop(EldenSensorRpc *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;__atomic_store_n(&c->stop_requested,1u,__ATOMIC_RELEASE);c->provider.wake(c->provider.context);return 0;
}
int elden_sensor_rpc_finish(EldenSensorRpc *c,uint64_t owner,uint64_t epoch){
    if(!same(c,owner,epoch))return -116;(void)elden_sensor_rpc_request_stop(c,owner,epoch);
    if(__atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE))return 0;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->executing,&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -11;
    int rc=admit(c,1);if(rc){__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return rc;}
    if(!__atomic_load_n(&c->callbacks_closed,__ATOMIC_ACQUIRE))rc=c->provider.withdraw(c->provider.context,owner,epoch,&c->memory);
    if(rc){__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return rc<0?rc:-13;}
    __atomic_store_n(&c->callbacks_closed,1u,__ATOMIC_RELEASE);
    if(__atomic_load_n(&c->receiving,__ATOMIC_ACQUIRE)){__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return -11;}
    __atomic_store_n(&c->terminal,1u,__ATOMIC_RELEASE);__atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return 0; // refs/arena remain to reset
}
int elden_sensor_rpc_status(EldenSensorRpc *c,EldenSensorRpcStatus *out){
    if(!c||!out)return -22;uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->executing,&zero,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    SsrStatus *s=&c->core.status;EldenSensorRpcStatus r={s->stage,s->error,s->calls,s->opens,s->reads,s->missing,s->heap_used,
        __atomic_load_n(&c->terminal,__ATOMIC_ACQUIRE),c->provider.owner,c->provider.epoch,s->bytes};
    uint32_t error=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);if(error)r.error=(int32_t)error;*out=r;
    __atomic_store_n(&c->executing,0u,__ATOMIC_RELEASE);return 0;
}
