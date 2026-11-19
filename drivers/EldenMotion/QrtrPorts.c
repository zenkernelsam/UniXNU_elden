/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Client selection/copy-and-callback/pure-frame pattern derived from
 * Pagani AdspQrtr.c c888efd3 (Sheng08cde20e); no board intents/PD table/core
 * imported. One original IPCRTR producer, owned epochs and retained handles. */
#include "QrtrPorts.h"
#include "QcomQrtr.h"

struct EldenQrtrPort {
    uint32_t used,number,service,instance,known,closing,terminal,callbacks,inflight,deleting,retiring,del_queued,last_channel_epoch;
    uint64_t requests,received;EldenQrtrServer server,last;
    EldenQrtrClient client;void *context;int error;
};
static struct {
    EldenQrtrPortOps ops;ShengAdspChannel *channel;
    EldenQrtrPort ports[EldenQrtrPorts];EldenQrtrServer remote[EldenQrtrRemoteServers];
    uint32_t remote_count,remote_node,channel_epoch,link_up,hello,configured,started,rx_size;
    uint64_t next_generation;int error;
} registry;
static uint32_t prepared;
static int ready(void){return __atomic_load_n(&prepared,__ATOMIC_ACQUIRE)==2;}
static void lock(void){registry.ops.lock(registry.ops.context);}
static void unlock(void){registry.ops.unlock(registry.ops.context);}
static int same(const EldenQrtrServer *a,const EldenQrtrServer *b){
    return a&&b&&a->service==b->service&&a->instance==b->instance&&a->node==b->node&&a->port==b->port&&a->generation==b->generation;
}
static int address(const EldenQrtrServer *a,const EldenQrtrServer *b){
    return a->service==b->service&&a->instance==b->instance&&a->node==b->node&&a->port==b->port;
}
static int member(const EldenQrtrPort *p){for(unsigned i=0;i<EldenQrtrPorts;i++)if(p==&registry.ports[i]&&p->used)return 1;return 0;}
static int identity(uint64_t owner,uint64_t epoch){return ready()&&owner==registry.ops.owner&&epoch==registry.ops.epoch;}
static int admit(uint64_t owner,uint64_t epoch,int cleanup){
    if(!identity(owner,epoch))return -116;
    int rc=registry.ops.admit(registry.ops.context,owner,epoch,cleanup);return rc<=0?rc:-13;
}
static EldenQrtrPortStatus status_locked(const EldenQrtrPort *p){
    return (EldenQrtrPortStatus){p->number,p->last_channel_epoch?p->last_channel_epoch:registry.channel_epoch,p->closing,p->terminal,p->callbacks,p->inflight,p->del_queued,
        registry.ops.owner,registry.ops.epoch,p->requests,p->received,p->last,p->error?p->error:registry.error};
}
int elden_qrtr_ports_prepare(const EldenQrtrPortOps *ops){
    if(!ops||!ops->owner||!ops->epoch||!ops->lock||!ops->unlock||!ops->admit||!ops->retire||
       ops->rx_intent_size<QCOM_QRTR_HEADER+EldenQrtrMaxReceive||ops->rx_intent_size>65536||
       !ops->rx_intent_count||ops->rx_intent_count>24)return -22;
    int rc=ops->admit(ops->context,ops->owner,ops->epoch,0);if(rc)return rc<0?rc:-13;
    uint32_t none=0;if(!__atomic_compare_exchange_n(&prepared,&none,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    registry.ops=*ops;registry.next_generation=1;__atomic_store_n(&prepared,2,__ATOMIC_RELEASE);return 0;
}
int elden_qrtr_ports_owner(uint64_t *owner,uint64_t *epoch,uint32_t *receive){
    if(!ready())return -19;if(!owner||!epoch||!receive)return -22;lock();
    if(registry.error){int rc=registry.error;unlock();return rc;}
    *owner=registry.ops.owner;*epoch=registry.ops.epoch;*receive=EldenQrtrMaxReceive;unlock();return 0;
}
int elden_qrtr_ports_admit(uint64_t owner,uint64_t epoch,int cleanup){
    int rc=admit(owner,epoch,cleanup);if(rc)return rc;lock();rc=cleanup?0:registry.error;unlock();return rc;
}
int elden_qrtr_ports_rx_config(uint32_t *size,uint32_t *count){
    /* Legacy router startup seals the absence of a provider. A late prepare
     * cannot change already allocated intents or replace the sole owner. */
    uint32_t none=0;if(__atomic_compare_exchange_n(&prepared,&none,3,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
    if(!ready())return -16;if(!size||!count)return -22;lock();
    if(registry.started||registry.configured){unlock();return -16;}
    registry.configured=1;*size=registry.ops.rx_intent_size;*count=registry.ops.rx_intent_count;unlock();return 1;
}
int elden_qrtr_ports_bind(ShengAdspChannel *channel,uint32_t size){
    if(!ready())return 0;lock();
    if(!registry.configured||registry.started||!channel||size!=registry.ops.rx_intent_size){registry.error=-116;unlock();return -116;}
    registry.channel=channel;registry.rx_size=size;registry.started=1;unlock();return 0;
}
typedef struct {EldenQrtrPort *port;EldenQrtrServer server;int up;} Notice;
static void notices(Notice *tell,unsigned count){
    for(unsigned i=0;i<count;i++){
        EldenQrtrPort *p=tell[i].port;lock();int deliver=!p->closing&&!p->terminal;
        EldenQrtrClient client=p->client;void *context=p->context;unlock();
        if(deliver&&client.server)client.server(context,registry.ops.owner,registry.ops.epoch,&tell[i].server,tell[i].up);
        lock();p->callbacks--;unlock();
    }
}
void elden_qrtr_ports_fault(int error){
    if(!ready())return;Notice tell[EldenQrtrPorts];unsigned count=0;lock();
    if(!registry.error)registry.error=error<0?error:-13;registry.link_up=registry.hello=0;registry.remote_count=registry.remote_node=0;
    for(unsigned i=0;i<EldenQrtrPorts;i++){
        EldenQrtrPort *p=&registry.ports[i];if(!p->used||!p->known)continue;
        p->known=0;if(!p->closing&&!p->terminal&&p->client.server){p->callbacks++;tell[count++]=(Notice){p,p->server,0};}
    }
    unlock();notices(tell,count);
}
void elden_qrtr_ports_link(uint32_t epoch,int up){
    if(!ready())return;Notice tell[EldenQrtrPorts];unsigned count=0;lock();
    if(!registry.started||!epoch||registry.error){unlock();return;}
    if(epoch<registry.channel_epoch||(epoch==registry.channel_epoch&&registry.link_up==(uint32_t)(up!=0))){unlock();return;}
    registry.channel_epoch=epoch;registry.link_up=up!=0;registry.hello=0;registry.remote_count=registry.remote_node=0;
    for(unsigned i=0;i<EldenQrtrPorts;i++){
        EldenQrtrPort *p=&registry.ports[i];if(!p->used||!p->known)continue;
        p->known=0;if(!p->closing&&!p->terminal&&p->client.server){p->callbacks++;tell[count++]=(Notice){p,p->server,0};}
    }
    unlock();notices(tell,count);
}
static int follows(const EldenQrtrPort *p,const EldenQrtrServer *server){
    return p->service==server->service&&(p->instance==ELDEN_QRTR_ANY_INSTANCE||p->instance==server->instance);
}
void elden_qrtr_ports_snapshot(uint32_t epoch,int hello,uint32_t remote_node,const EldenQrtrServer *servers,uint32_t count){
    if(!ready())return;if(count>EldenQrtrRemoteServers||(count&&!servers)){elden_qrtr_ports_fault(-75);return;}
    Notice tell[2*EldenQrtrPorts];unsigned messages=0;EldenQrtrServer next[EldenQrtrRemoteServers];lock();
    if(registry.error||!registry.link_up||epoch!=registry.channel_epoch){unlock();return;}
    if(hello&&(!remote_node||remote_node==1||remote_node==QCOM_QRTR_NODE_BROADCAST)){unlock();elden_qrtr_ports_fault(-84);return;}
    uint32_t used=hello?count:0;
    for(uint32_t i=0;i<used;i++){
        next[i]=servers[i];next[i].generation=0;
        if(!next[i].service||!next[i].node||next[i].node==1||next[i].node==QCOM_QRTR_NODE_BROADCAST||
           !next[i].port||next[i].port==QCOM_QRTR_PORT_CTRL){unlock();elden_qrtr_ports_fault(-84);return;}
        for(uint32_t j=0;j<i;j++)if(next[j].node==next[i].node&&next[j].port==next[i].port){unlock();elden_qrtr_ports_fault(-84);return;}
        for(uint32_t j=0;j<registry.remote_count;j++)if(address(&next[i],&registry.remote[j]))next[i].generation=registry.remote[j].generation;
        if(!next[i].generation){
            if(registry.next_generation==UINT64_MAX){unlock();elden_qrtr_ports_fault(-75);return;}
            next[i].generation=registry.next_generation++;
        }
    }
    for(uint32_t i=0;i<used;i++)registry.remote[i]=next[i];registry.remote_count=used;registry.remote_node=hello?remote_node:0;registry.hello=hello!=0;
    for(unsigned i=0;i<EldenQrtrPorts;i++){
        EldenQrtrPort *p=&registry.ports[i];if(!p->used||p->closing||p->terminal)continue;int selected=-1;
        /* Keep the already selected exact server through unrelated list reorder. */
        for(uint32_t j=0;j<used;j++)if(follows(p,&next[j])&&p->known&&same(&p->server,&next[j]))selected=(int)j;
        if(selected<0)for(uint32_t j=0;j<used;j++)if(follows(p,&next[j])){selected=(int)j;break;}
        if((selected<0&&!p->known)||(selected>=0&&p->known&&same(&p->server,&next[selected])))continue;
        if(p->known&&p->client.server){p->callbacks++;tell[messages++]=(Notice){p,p->server,0};}
        p->known=selected>=0;
        if(selected>=0){p->server=next[selected];if(!p->requests){p->last=p->server;p->last_channel_epoch=epoch;}
            if(p->client.server){p->callbacks++;tell[messages++]=(Notice){p,p->server,1};}}
    }
    unlock();notices(tell,messages);
}
void elden_qrtr_ports_receive(uint32_t epoch,uint32_t number,uint32_t node,uint32_t from,const uint8_t *data,uint32_t size){
    if(!ready())return;EldenQrtrPort *p=0;EldenQrtrClient client={0};EldenQrtrServer server={0};void *context=0;lock();
    if(registry.error||!registry.link_up||!registry.hello||epoch!=registry.channel_epoch){unlock();return;}
    for(unsigned i=0;i<EldenQrtrPorts;i++)if(registry.ports[i].used&&registry.ports[i].number==number){p=&registry.ports[i];break;}
    if(!p||!p->known||p->closing||p->terminal||p->server.node!=node||p->server.port!=from){unlock();return;}
    if(!data||!size||size>EldenQrtrMaxReceive||size>registry.rx_size-QCOM_QRTR_HEADER){p->error=-75;unlock();elden_qrtr_ports_fault(-75);return;}
    if(p->received==UINT64_MAX||p->callbacks==UINT32_MAX){unlock();elden_qrtr_ports_fault(-75);return;}
    p->callbacks++;p->received++;client=p->client;server=p->server;context=p->context;unlock();
    if(client.receive)client.receive(context,registry.ops.owner,registry.ops.epoch,&server,data,size);
    lock();p->callbacks--;unlock();
}
EldenQrtrPort *elden_qrtr_port_open(uint64_t owner,uint64_t epoch,uint32_t service,uint32_t instance,const EldenQrtrClient *client,void *context){
    if(!service||!client||!client->server||!client->receive||admit(owner,epoch,0))return 0;lock();
    if(registry.error){unlock();return 0;}EldenQrtrPort *p=0;
    for(unsigned i=0;i<EldenQrtrPorts;i++)if(!registry.ports[i].used){p=&registry.ports[i];p->used=1;p->number=0x4000u+i;break;}
    if(!p){unlock();return 0;}p->service=service;p->instance=instance;p->client=*client;p->context=context;
    for(uint32_t i=0;i<registry.remote_count;i++)if(registry.link_up&&registry.hello&&follows(p,&registry.remote[i])){p->known=1;p->server=p->last=registry.remote[i];p->last_channel_epoch=registry.channel_epoch;break;}
    /* level reads the current snapshot. No synchronous callback before caller
     * has attached the returned handle; future notifications use real epochs. */
    unlock();return p;
}
int elden_qrtr_port_level(EldenQrtrPort *p,uint64_t owner,uint64_t epoch,EldenQrtrServer *out){
    int rc=admit(owner,epoch,0);if(rc)return rc;if(!out)return -22;lock();
    if(!member(p)){unlock();return -22;}if(p->closing||p->terminal||p->error||registry.error){rc=p->error?p->error:registry.error?registry.error:-125;unlock();return rc;}
    int known=p->known&&registry.link_up&&registry.hello;EldenQrtrServer server=p->server;uint32_t channel_epoch=registry.channel_epoch;ShengAdspChannel *channel=registry.channel;unlock();
    EldenAdspChannelStatus observed={0};
    if(!known){*out=(EldenQrtrServer){0};return 0;}
    if(!channel||elden_adsp_channel_status(channel,&observed)||!observed.connected||observed.epoch!=channel_epoch||observed.closing||observed.error)return -116;
    lock();int fresh=!p->closing&&!p->terminal&&!registry.error&&registry.channel_epoch==channel_epoch&&p->known&&same(&p->server,&server);unlock();if(!fresh)return -116;
    *out=server;return 1;
}
int elden_qrtr_port_send(EldenQrtrPort *p,uint64_t owner,uint64_t epoch,const EldenQrtrServer *server,const uint8_t *data,uint32_t size){
    int rc=admit(owner,epoch,0);if(rc)return rc;if(!server||!data||!size||size>EldenQrtrMaxData)return -22;
    lock();if(!member(p)){unlock();return -22;}
    if(p->closing||p->terminal||registry.error||p->error||!p->known||!registry.link_up||!registry.hello||!same(server,&p->server)||
       (p->requests&&(!same(server,&p->last)||p->last_channel_epoch!=registry.channel_epoch))){unlock();return -116;}
    if(p->inflight){unlock();return -16;}if(p->requests==UINT64_MAX){unlock();return -75;}
    uint32_t channel_epoch=registry.channel_epoch,number=p->number;ShengAdspChannel *channel=registry.channel;p->inflight++;unlock();
    uint8_t packet[512];uint32_t bytes=qcom_qrtr_frame(packet,sizeof packet,QCOM_QRTR_DATA,1,number,server->node,server->port,data,size);
    lock();
    if(p->closing||p->terminal||registry.error||!p->known||!same(server,&p->server)||registry.channel_epoch!=channel_epoch)rc=-3;
    else {
        /* The bounded original ADSP enqueue copies preallocated data, drops
         * its own lock before wake/callback, and never calls into QRTR.
         * Holding this registry lock through that enqueue prevents an old
         * same-link service generation racing into a replacement address. */
        rc=bytes&&channel?sheng_adsp_send_epoch(channel,channel_epoch,packet,bytes):-2;
    }
    p->inflight--;if(!rc){p->requests++;p->last=*server;p->last_channel_epoch=channel_epoch;}unlock();
    return rc==0?0:rc==-1?-11:rc==-3?-116:-22;
}
int elden_qrtr_port_status(EldenQrtrPort *p,EldenQrtrPortStatus *out){
    if(!ready()||!out)return -22;lock();if(!member(p)){unlock();return -22;}*out=status_locked(p);unlock();return 0;
}
int elden_qrtr_port_withdraw(EldenQrtrPort *p,uint64_t owner,uint64_t epoch,const EldenQrtrServer *expected){
    int rc=admit(owner,epoch,1);if(rc)return rc;lock();if(!member(p)){unlock();return -22;}
    if(p->terminal){unlock();return 0;}
    if(!p->closing&&p->requests&&(!expected||!same(expected,&p->last))){unlock();return -116;}
    p->closing=1;p->known=0;
    if(p->inflight||p->callbacks||p->deleting||p->retiring){unlock();return -11;}
    uint32_t channel_epoch=registry.channel_epoch,peer=registry.remote_node;ShengAdspChannel *channel=registry.channel;EldenQrtrPortStatus port=status_locked(p);unlock();
    EldenAdspChannelStatus observed={0};
    if(!channel||elden_adsp_channel_status(channel,&observed))return -11;
    if(peer&&observed.connected&&observed.epoch==channel_epoch&&observed.epoch==port.channel_epoch&&!observed.closing&&!observed.error&&!port.del_client_queued){
        lock();if(p->inflight||p->callbacks||p->deleting||p->retiring||p->del_queued){unlock();return -11;}p->deleting=1;unlock();
        uint8_t packet[52];const uint32_t words[4]={1,port.number,0,0};
        /* Destination is the actual HELLO peer from this exact generation,
         * never the GLINK epoch, service node or a fixed donor node. */
        uint32_t bytes=qcom_qrtr_control_frame(packet,sizeof packet,QCOM_QRTR_DEL_CLIENT,1,port.number,peer,words);
        rc=bytes?sheng_adsp_send_epoch(channel,channel_epoch,packet,bytes):-2;
        lock();p->deleting=0;if(!rc)p->del_queued=1;unlock();
        if(!rc)return -11;
        if(rc==-1)return -11;if(rc!=-3)return -22;
    }
    /* Fresh queue observation is stronger than this port's local counters.
     * No sender for this closed port remains; unrelated shared users retain
     * their own admission. Physical remote inactivity is still separate. */
    if(elden_adsp_channel_status(channel,&observed)||observed.queued||observed.pumping||observed.callbacks)return -11;
    lock();if(p->inflight||p->callbacks||p->deleting||p->retiring){unlock();return -11;}
    p->retiring=1;port=status_locked(p);unlock();rc=registry.ops.retire(registry.ops.context,&port);
    lock();p->retiring=0;if(!rc)p->terminal=1;unlock();return rc<=0?rc:-13;
}
