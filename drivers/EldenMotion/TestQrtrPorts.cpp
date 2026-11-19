/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute actual original ShengQrtr/QcomQrtr/QrtrPorts/SSC adapter. Thread
 * locks and original ADSP queue/physical retirement are explicit modeled peers. */
#include "QrtrPorts.h"
#include "QrtrTransport.h"
#include "QcomQrtr.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};static unsigned scenarios;
#define CHECK(v) do{checks.fetch_add(1);if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
struct ShengAdspChannel {EldenAdspEpochClient client{};void *context=nullptr;uint32_t size=0,count=0;};
static ShengAdspChannel wire;static EldenAdspChannelStatus observed{};static std::mutex registry_lock,queue_lock;
static std::vector<std::vector<uint8_t>> queued;static bool full_queue=false;static unsigned retired;
static std::atomic<bool> callback_entered{false},callback_release{false},send_entered{false},send_release{false},hold_send{false};
static int retire_result=0,admit_result=0;static std::atomic<bool> hold_receive{false};
extern "C" ShengAdspChannel *elden_adsp_channel_epoch(const char *name,uint32_t size,uint32_t count,int initiator,const EldenAdspEpochClient *client,void *context){
    CHECK(std::strcmp(name,"IPCRTR")==0&&size>=1024&&count==8&&!initiator&&client);wire={*client,context,size,count};return &wire;
}
extern "C" int sheng_adsp_send_epoch(ShengAdspChannel *channel,uint32_t epoch,const void *data,uint32_t size){
    CHECK(channel==&wire&&data&&size<=SHENG_ADSP_MAX_MESSAGE);
    if(hold_send.load()){send_entered=true;while(!send_release.load())std::this_thread::yield();}
    std::lock_guard<std::mutex> guard(queue_lock);if(!observed.connected||observed.closing||observed.epoch!=epoch)return -3;
    if(full_queue)return -1;queued.emplace_back((const uint8_t *)data,(const uint8_t *)data+size);observed.queued++;return 0;
}
extern "C" int elden_adsp_channel_status(ShengAdspChannel *channel,EldenAdspChannelStatus *out){
    CHECK(channel==&wire&&out);std::lock_guard<std::mutex> guard(queue_lock);*out=observed;return 0;
}
extern "C" void sheng_qrtr_start(void);
extern "C" void elden_qrtr_original_service(void);
static void op_lock(void *){registry_lock.lock();}static void op_unlock(void *){registry_lock.unlock();}
static int op_admit(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return admit_result;}
static int op_retire(void *,const EldenQrtrPortStatus *s){CHECK(s&&s->owner==7&&s->epoch==9&&s->closing&&!s->callbacks&&!s->inflight);retired++;return retire_result;}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void clear_queue(){std::lock_guard<std::mutex> guard(queue_lock);queued.clear();observed.queued=0;}
static void packet(uint32_t type,uint32_t port,uint32_t from,const uint8_t *data,uint32_t size,uint32_t confirm=0,uint32_t node=5,uint32_t destination=1){
    uint8_t bytes[1200];uint32_t n=qcom_qrtr_frame(bytes,sizeof bytes,type,node,from,destination,port,data,size);CHECK(n);put32(bytes+16,confirm);
    wire.client.receive(wire.context,observed.epoch,bytes,n);
}
static void hello(){uint8_t body[20]={0};put32(body,QCOM_QRTR_HELLO);packet(QCOM_QRTR_HELLO,QCOM_QRTR_PORT_CTRL,QCOM_QRTR_PORT_CTRL,body,sizeof body);}
static void service(uint32_t type,uint32_t svc=400,uint32_t instance=0x100,uint32_t node=5,uint32_t port=0x9000){
    uint8_t body[20];put32(body,type);put32(body+4,svc);put32(body+8,instance);put32(body+12,node);put32(body+16,port);
    packet(type,QCOM_QRTR_PORT_CTRL,QCOM_QRTR_PORT_CTRL,body,sizeof body);
}
static void start(bool production=true){
    scenarios++;if(production){EldenQrtrPortOps ops{nullptr,7,9,2048,8,op_lock,op_unlock,op_admit,op_retire};CHECK(!elden_qrtr_ports_prepare(&ops));}
    sheng_qrtr_start();CHECK(wire.size==(production?2048u:1024u));observed.epoch=1;observed.connected=1;wire.client.state(wire.context,1,1);hello();service(QCOM_QRTR_NEW_SERVER);clear_queue();
}
struct Seen {unsigned up=0,down=0,received=0;EldenQrtrServer last{};std::vector<uint8_t> data;EldenQrtrPort *port=nullptr;};
static void on_server(void *p,uint64_t owner,uint64_t epoch,const EldenQrtrServer *s,int up){
    CHECK(owner==7&&epoch==9&&s&&s->generation);auto *v=(Seen *)p;if(up)v->up++;else v->down++;v->last=*s;
}
static void on_receive(void *p,uint64_t owner,uint64_t epoch,const EldenQrtrServer *s,const uint8_t *data,uint32_t size){
    CHECK(owner==7&&epoch==9&&s&&data&&size);auto *v=(Seen *)p;v->received++;v->last=*s;v->data.assign(data,data+size);
    if(hold_receive.load()){callback_entered=true;while(!callback_release.load())std::this_thread::yield();}
}
static const EldenQrtrClient client{on_server,on_receive};
static EldenQrtrServer level(EldenQrtrPort *p){EldenQrtrServer s{};CHECK(elden_qrtr_port_level(p,7,9,&s)==1);return s;}
static EldenQrtrPortStatus status(EldenQrtrPort *p){EldenQrtrPortStatus s{};CHECK(!elden_qrtr_port_status(p,&s));return s;}
static EldenQrtrPort *open(Seen &s){s.port=elden_qrtr_port_open(7,9,400,ELDEN_QRTR_ANY_INSTANCE,&client,&s);CHECK(s.port);return s.port;}
static void transport_server(void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s,int up){EldenQrtrServer q{s->service,s->instance,s->node,s->port,s->generation};on_server(p,owner,epoch,&q,up);}
static void transport_receive(void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s,const uint8_t *data,uint32_t size){EldenQrtrServer q{s->service,s->instance,s->node,s->port,s->generation};on_receive(p,owner,epoch,&q,data,size);}
int main(int argc,char **argv){
    CHECK(argc==2);std::string test=argv[1];uint8_t data[480]={1,2,3};
    if(test=="legacy"){
        start(false);EldenQrtrPortOps ops{nullptr,7,9,2048,8,op_lock,op_unlock,op_admit,op_retire};CHECK(elden_qrtr_ports_prepare(&ops)==-16);
        CHECK(!elden_qrtr_port_open(7,9,400,ELDEN_QRTR_ANY_INSTANCE,&client,nullptr));
    }else if(test=="qualified"){
        EldenQrtrPortOps bad{nullptr,7,9,1024,8,op_lock,op_unlock,op_admit,op_retire};CHECK(elden_qrtr_ports_prepare(&bad)==-22);start();
        Seen seen;auto *p=open(seen);auto s=level(p);CHECK(s.service==400&&s.node==5&&s.port==0x9000&&s.generation);
        CHECK(!elden_qrtr_port_send(p,7,9,&s,data,480)&&queued.size()==1);
        auto q=queued[0];CHECK(q.size()==512&&u32(q.data()+12)==status(p).number&&u32(q.data()+24)==5&&u32(q.data()+28)==0x9000);
        CHECK(elden_qrtr_port_send(p,7,9,&s,data,481)==-22);
        clear_queue();packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,480,1);CHECK(seen.received==1&&seen.data.size()==480);
        CHECK(queued.size()==1&&u32(queued[0].data()+4)==QCOM_QRTR_RESUME_TX&&u32(queued[0].data()+12)==status(p).number&&u32(queued[0].data()+24)==5&&u32(queued[0].data()+28)==0x9000);
        CHECK(u32(queued[0].data()+36)==1&&u32(queued[0].data()+40)==status(p).number);
    }else if(test=="flow-debt"){
        start();Seen seen;auto *p=open(seen);full_queue=true;packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,8,1);CHECK(seen.received==1&&queued.empty());
        full_queue=false;packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,8,1);CHECK(seen.received==2);elden_qrtr_original_service();CHECK(queued.size()==1&&u32(queued[0].data()+4)==QCOM_QRTR_RESUME_TX);clear_queue();elden_qrtr_original_service();CHECK(queued.empty());
    }else if(test=="callbacks"){
        start();Seen seen;auto *p=open(seen);auto s=level(p);hold_receive=true;
        std::thread receive([&]{packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,8);});
        while(!callback_entered.load())std::this_thread::yield();CHECK(status(p).callbacks==1);CHECK(elden_qrtr_port_withdraw(p,7,9,&s)==-11&&retired==0);
        callback_release=true;receive.join();hold_receive=false;CHECK(elden_qrtr_port_withdraw(p,7,9,&s)==-11);clear_queue();CHECK(!elden_qrtr_port_withdraw(p,7,9,&s)&&retired==1);
        packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,8);CHECK(seen.received==1);
    }else if(test=="send-race"){
        start();Seen seen;auto *p=open(seen);auto s=level(p);hold_send=true;
        std::thread sender([&]{CHECK(!elden_qrtr_port_send(p,7,9,&s,data,8));});while(!send_entered.load())std::this_thread::yield();
        std::atomic<bool> closer_started{false},closer_done{false};int close_rc=0;
        std::thread closer([&]{closer_started=true;close_rc=elden_qrtr_port_withdraw(p,7,9,&s);closer_done=true;});
        while(!closer_started.load())std::this_thread::yield();CHECK(!closer_done.load()&&retired==0);send_release=true;sender.join();closer.join();hold_send=false;
        CHECK(close_rc==-11&&!status(p).inflight);clear_queue();CHECK(!elden_qrtr_port_withdraw(p,7,9,&s));CHECK(elden_qrtr_port_send(p,7,9,&s,data,8)==-116);
    }else if(test=="retirement"){
        start();Seen seen;auto *p=open(seen);auto s=level(p);CHECK(!elden_qrtr_port_send(p,7,9,&s,data,8));clear_queue();
        CHECK(elden_qrtr_port_withdraw(p,7,9,&s)==-11&&status(p).del_client_queued&&!status(p).terminal);CHECK(queued.size()==1&&u32(queued[0].data()+4)==QCOM_QRTR_DEL_CLIENT&&u32(queued[0].data()+24)==5);
        CHECK(elden_qrtr_port_withdraw(p,7,9,&s)==-11&&retired==0);clear_queue();retire_result=-11;CHECK(elden_qrtr_port_withdraw(p,7,9,&s)==-11&&!status(p).terminal);
        retire_result=0;CHECK(!elden_qrtr_port_withdraw(p,7,9,&s)&&status(p).terminal);CHECK(!elden_qrtr_port_withdraw(p,7,9,&s)&&retired==2);
    }else if(test=="server-generation"){
        start();Seen a,b,c;auto *p=open(a);auto *q=open(b);auto *r=open(c);auto old=level(p);CHECK(level(q).generation==old.generation&&level(r).generation==old.generation);
        service(QCOM_QRTR_NEW_SERVER,401,0x100,5,0x9001);CHECK(level(p).generation==old.generation);
        CHECK(!elden_qrtr_port_send(p,7,9,&old,data,8));clear_queue();service(QCOM_QRTR_DEL_SERVER);CHECK(a.down==1);
        service(QCOM_QRTR_NEW_SERVER);auto fresh=level(p);CHECK(a.up==1&&fresh.generation!=old.generation&&level(q).generation==fresh.generation&&level(r).generation==fresh.generation);
        CHECK(elden_qrtr_port_send(p,7,9,&old,data,8)==-116&&elden_qrtr_port_send(p,7,9,&fresh,data,8)==-116);
    }else if(test=="stale-server-delete"){
        start();Seen seen;auto *p=open(seen);auto old=level(p);service(QCOM_QRTR_NEW_SERVER,400,0x200);auto next=level(p);CHECK(next.generation!=old.generation);
        service(QCOM_QRTR_DEL_SERVER,400,0x100);CHECK(level(p).generation==next.generation&&seen.down==1);
    }else if(test=="link-change"){
        start();Seen seen;auto *p=open(seen);auto old=level(p);CHECK(!elden_qrtr_port_send(p,7,9,&old,data,8));clear_queue();
        observed.epoch=2;observed.connected=0;wire.client.state(wire.context,2,0);observed.epoch=3;observed.connected=1;wire.client.state(wire.context,3,1);hello();service(QCOM_QRTR_NEW_SERVER);clear_queue();
        CHECK(level(p).generation!=old.generation&&elden_qrtr_port_send(p,7,9,&old,data,8)==-116);
        retire_result=-11;CHECK(elden_qrtr_port_withdraw(p,7,9,&old)==-11&&queued.empty());CHECK(retired==1);retire_result=0;CHECK(!elden_qrtr_port_withdraw(p,7,9,&old));
    }else if(test=="foreign-hello"){
        start();Seen seen;auto *p=open(seen);auto old=level(p);uint8_t body[20]={0};put32(body,QCOM_QRTR_HELLO);packet(QCOM_QRTR_HELLO,QCOM_QRTR_PORT_CTRL,QCOM_QRTR_PORT_CTRL,body,sizeof body,0,6);
        EldenQrtrServer s{};CHECK(elden_qrtr_port_level(p,7,9,&s)==0&&seen.down==1);CHECK(elden_qrtr_port_send(p,7,9,&old,data,8)==-116);
    }else if(test=="rx-boundary"){
        start();Seen seen;auto *p=open(seen);packet(QCOM_QRTR_DATA,status(p).number,0x9000,data,8,0,6);CHECK(!seen.received);
        packet(QCOM_QRTR_DATA,status(p).number+20,0x9000,data,8);CHECK(!seen.received);
        uint8_t long_data[1024]={0};packet(QCOM_QRTR_DATA,status(p).number,0x9000,long_data,sizeof long_data);CHECK(seen.received==1&&seen.data.size()==1024);
    }else if(test=="malformed"){
        start();Seen seen;auto *p=open(seen);uint8_t body[8]={0};put32(body,QCOM_QRTR_NEW_SERVER);
        packet(QCOM_QRTR_NEW_SERVER,QCOM_QRTR_PORT_CTRL,QCOM_QRTR_PORT_CTRL,body,sizeof body);EldenQrtrServer s{};CHECK(elden_qrtr_port_level(p,7,9,&s)==-84&&seen.received==0);
    }else if(test=="capacity"){
        start();Seen seen[13];EldenQrtrPort *p[12];for(unsigned i=0;i<12;i++)p[i]=open(seen[i]);CHECK(!elden_qrtr_port_open(7,9,400,ELDEN_QRTR_ANY_INSTANCE,&client,&seen[12]));
        auto s=level(p[0]);CHECK(elden_qrtr_port_withdraw(p[0],7,9,&s)==-11);clear_queue();CHECK(!elden_qrtr_port_withdraw(p[0],7,9,&s));
        CHECK(!elden_qrtr_port_open(7,9,400,ELDEN_QRTR_ANY_INSTANCE,&client,&seen[12]));
    }else if(test=="adapter"){
        start();EldenMotionTransport t{};CHECK(!elden_motion_qrtr_transport(&t)&&t.owner==7&&t.epoch==9&&t.max_rx_payload==1024);
        Seen seen[3];void *p[3];EldenSscServer s[3];const EldenMotionTransportClient callback{transport_server,transport_receive};
        for(unsigned i=0;i<3;i++){p[i]=t.open(t.context,7,9,400,i,&callback,&seen[i]);CHECK(p[i]);CHECK(t.level(t.context,p[i],7,9,&s[i])==1);}
        CHECK(s[0].generation==s[1].generation&&s[1].generation==s[2].generation);CHECK(!t.open(t.context,7,9,400,0,&callback,&seen[0]));
        CHECK(!t.send_epoch(t.context,p[0],7,9,&s[0],data,8));uint32_t port=u32(queued.back().data()+12);clear_queue();packet(QCOM_QRTR_DATA,port,0x9000,data,8);CHECK(seen[0].received==1&&!seen[1].received);
        CHECK(t.withdraw(t.context,p[0],7,9,&s[0])==-11);clear_queue();CHECK(!t.withdraw(t.context,p[0],7,9,&s[0]));
    }else if(test=="stale-identity"){
        start();Seen seen;auto *p=open(seen);auto s=level(p);CHECK(elden_qrtr_port_send(p,7,10,&s,data,8)==-116);CHECK(elden_qrtr_port_withdraw(p,8,9,&s)==-116);
        s.generation++;CHECK(elden_qrtr_port_send(p,7,9,&s,data,8)==-116);admit_result=-13;EldenQrtrServer out{};CHECK(elden_qrtr_port_level(p,7,9,&out)==-13);
    }else if(test=="pad-overflow"){
        uint8_t out[64],body[1]={0};std::memset(out,0xa5,sizeof out);CHECK(!qcom_qrtr_frame(out,UINT32_MAX,QCOM_QRTR_DATA,1,1,5,2,body,UINT32_MAX-32));
        for(auto v:out)CHECK(v==0xa5);
    }else{std::fprintf(stderr,"Unknown case\n");return 2;}
    std::printf("PASS actual QRTR/SSC %s: %u checks; ADSP/locks/remote-owner modeled\n",test.c_str(),checks.load());return 0;
}
