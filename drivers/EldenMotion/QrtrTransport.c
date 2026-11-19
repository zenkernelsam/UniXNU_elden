/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The existing motion worker consumes the single original QRTR provider. */
#include "NativeSsc.h"
#include "QrtrPorts.h"
typedef struct {uint32_t used;EldenMotionTransportClient client;void *context;EldenQrtrPort *port;} Connection;
static Connection connections[3];static uint32_t transport_tag;
static EldenSscServer ssc_server(const EldenQrtrServer *s){return (EldenSscServer){s->service,s->instance,s->node,s->port,s->generation};}
static EldenQrtrServer qrtr_server(const EldenSscServer *s){return (EldenQrtrServer){s->service,s->instance,s->node,s->port,s->generation};}
static void server(void *context,uint64_t owner,uint64_t epoch,const EldenQrtrServer *s,int up){
    Connection *c=(Connection *)context;EldenSscServer value=ssc_server(s);c->client.server(c->context,owner,epoch,&value,up);
}
static void receive(void *context,uint64_t owner,uint64_t epoch,const EldenQrtrServer *s,const uint8_t *data,uint32_t size){
    Connection *c=(Connection *)context;EldenSscServer value=ssc_server(s);c->client.receive(c->context,owner,epoch,&value,data,size);
}
static int admit(void *context,uint64_t owner,uint64_t epoch,int cleanup){
    return context==&transport_tag?elden_qrtr_ports_admit(owner,epoch,cleanup):-116;
}
static void *open(void *context,uint64_t owner,uint64_t epoch,uint32_t service,unsigned number,const EldenMotionTransportClient *client,void *target){
    if(context!=&transport_tag||number>=3||service!=EldenSscService||!client||!client->server||!client->receive||admit(context,owner,epoch,0))return 0;
    Connection *c=&connections[number];uint32_t none=0;
    if(!__atomic_compare_exchange_n(&c->used,&none,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
    c->client=*client;c->context=target;const EldenQrtrClient callbacks={server,receive};
    c->port=elden_qrtr_port_open(owner,epoch,service,ELDEN_QRTR_ANY_INSTANCE,&callbacks,c);
    return c->port?c:0; // attempted/failed handles remain retained, never reused
}
static int handle(void *context,void *p,Connection **out){
    if(context!=&transport_tag||!p)return -116;
    for(unsigned i=0;i<3;i++)if(p==&connections[i]&&connections[i].used&&connections[i].port){*out=&connections[i];return 0;}return -116;
}
static int level(void *context,void *p,uint64_t owner,uint64_t epoch,EldenSscServer *out){
    Connection *c;int rc=handle(context,p,&c);if(rc)return rc;if(!out)return -22;
    EldenQrtrServer s={0};rc=elden_qrtr_port_level(c->port,owner,epoch,&s);if(rc>=0)*out=ssc_server(&s);return rc;
}
static int send_epoch(void *context,void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s,const uint8_t *data,uint32_t size){
    Connection *c;int rc=handle(context,p,&c);if(rc)return rc;if(!s)return -22;
    EldenQrtrServer value=qrtr_server(s);return elden_qrtr_port_send(c->port,owner,epoch,&value,data,size);
}
static int withdraw(void *context,void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s){
    Connection *c;int rc=handle(context,p,&c);if(rc)return rc;
    EldenQrtrServer value={0};if(s)value=qrtr_server(s);return elden_qrtr_port_withdraw(c->port,owner,epoch,s?&value:0);
}
int elden_motion_qrtr_transport(EldenMotionTransport *out){
    if(!out)return -22;uint64_t owner=0,epoch=0;uint32_t size=0;int rc=elden_qrtr_ports_owner(&owner,&epoch,&size);if(rc)return rc;
    *out=(EldenMotionTransport){&transport_tag,owner,epoch,size,admit,open,level,send_epoch,withdraw};return 0;
}
