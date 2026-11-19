/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Executes AdspEndpointMethods.inc; actual ADSP registry/GLINK integration is
 * independently exercised in TestAdspChannels. Here OS/channel/memory peers
 * permit exact epoch/response/wait/retirement failure injection. */
#include "AdspEndpoint.h"
#include "AdspChannel.h"
#include "QcomGlink.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
static unsigned checks,scenarios;
#define CHECK(v) do{checks++;if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using boolean_t=int;using IOSimpleLock=std::mutex;using event_t=void *;
enum {THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1};
static constexpr auto THREAD_CONTINUE_NULL=nullptr;
static bool irq;static boolean_t ml_at_interrupt_context(){return irq;}
static uint64_t now_ticks=19200000;static uint64_t mach_absolute_time(){return now_ticks;}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *out){*out=ticks*625/12;}
static std::vector<void *> allocations;static std::vector<IOSimpleLock *> locks;
static void *IOMallocData(size_t bytes){void *p=std::calloc(1,bytes);if(p)allocations.push_back(p);return p;}
static IOSimpleLock *IOSimpleLockAlloc(){auto *p=new IOSimpleLock;locks.push_back(p);return p;}
static void IOSimpleLockLock(IOSimpleLock *p){p->lock();}static void IOSimpleLockUnlock(IOSimpleLock *p){p->unlock();}
static unsigned woke,blocked,cleared;static bool inject_event;static event_t waiting;
static void thread_wakeup(event_t){woke++;}
static void *current_thread(){return (void *)1;}
static void assert_wait_timeout(event_t event,int mode,uint32_t ms,int scale){CHECK(mode==THREAD_UNINT&&ms&&scale==1);waiting=event;if(inject_event){inject_event=false;__atomic_store_n((uint32_t *)event,1u,__ATOMIC_RELEASE);}}
static void clear_wait(void *t,int r){CHECK(t==(void *)1&&r==THREAD_AWAKENED);cleared++;}
static void thread_block(decltype(nullptr)){CHECK(waiting);blocked++;}
struct ShengAdspChannel {EldenAdspEpochClient client{};void *context=nullptr;EldenAdspChannelStatus status{};};
static ShengAdspChannel channel;static int send_error;static unsigned sent,closed_calls,delivered,state_delivered;static bool up_at_open;
extern "C" ShengAdspChannel *elden_adsp_channel_epoch(const char *name,uint32_t size,uint32_t count,int initiator,const EldenAdspEpochClient *client,void *p){
    CHECK(!std::strcmp(name,"fastrpcglink-apps-dsp")&&size==1024&&count==4&&!initiator&&client);
    channel={};channel.client=*client;channel.context=p;if(up_at_open){channel.status.epoch=1;channel.status.connected=1;client->state(p,1,1);}return &channel;
}
extern "C" int elden_adsp_channel_status(ShengAdspChannel *c,EldenAdspChannelStatus *out){CHECK(c==&channel&&out);*out=c->status;return 0;}
extern "C" int elden_adsp_channel_close(ShengAdspChannel *c,uint32_t epoch){CHECK(c==&channel&&epoch==c->status.epoch);closed_calls++;c->status.closing=1;return 0;}
extern "C" int sheng_adsp_send_epoch(ShengAdspChannel *c,uint32_t epoch,const void *p,uint32_t n){CHECK(c==&channel&&epoch==1&&p&&n==40);if(send_error)return send_error;sent++;return 0;}
extern "C" int elden_sensor_memory_binding_channel(EldenSensorMemoryBinding *p,uint64_t owner,uint64_t epoch,int up){CHECK(p==(EldenSensorMemoryBinding *)1&&owner==7&&epoch==9&&(up==0||up==1));state_delivered++;return 0;}
extern "C" int elden_sensor_memory_binding_receive(EldenSensorMemoryBinding *p,uint64_t owner,uint64_t epoch,const uint8_t *data,uint32_t size){CHECK(p==(EldenSensorMemoryBinding *)1&&owner==7&&epoch==9&&data&&size>=12);delivered++;return 0;}
#include "AdspEndpointMethods.inc"
static uint64_t next_cookie;static unsigned retired,completed;static int retirement_error,admission_error;
static int authority(void *p,uint64_t owner,uint64_t epoch,int cleanup){CHECK(p==(void *)2&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return admission_error;}
static int reserve(void *p,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(p==(void *)2&&owner==7&&epoch==9);next_cookie++;*out=next_cookie<<4|2;return 0;}
static int complete(void *p,uint64_t owner,uint64_t epoch,uint64_t cookie){CHECK(p==(void *)2&&owner==7&&epoch==9&&cookie==(next_cookie<<4|2));completed++;return 0;}
static int retire(void *p,uint64_t owner,uint64_t epoch,uint32_t e,int pending){CHECK(p==(void *)2&&owner==7&&epoch==9&&e==1&&(pending==0||pending==1));if(retirement_error)return retirement_error;retired++;return 0;}
static void trace(void *,const char *){}
static void reset(){for(auto *p:locks)delete p;locks.clear();for(void *p:allocations)std::free(p);allocations.clear();channel={};send_error=0;sent=closed_calls=delivered=state_delivered=retired=completed=woke=blocked=cleared=0;next_cookie=0;retirement_error=admission_error=0;up_at_open=irq=inject_event=false;waiting=nullptr;scenarios++;}
static EldenSensorAdspEndpoint *open(EldenSensorEndpoint &endpoint,bool attach=true){
    EldenSensorAdspAuthority a{(void *)2,7,9,authority,reserve,complete,retire,trace};EldenSensorAdspEndpoint *p=nullptr;
    CHECK(!elden_sensor_adsp_endpoint_open(&a,1024,4,&p,&endpoint)&&p);
    if(attach)CHECK(!elden_sensor_adsp_endpoint_attach(p,(EldenSensorMemoryBinding *)1,7,9));return p;
}
static void online(){channel.status.epoch=1;channel.status.connected=1;channel.client.state(channel.context,1,1);}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static EldenSensorAdspStatus status(EldenSensorAdspEndpoint *p){EldenSensorAdspStatus s{};CHECK(!elden_sensor_adsp_endpoint_status(p,&s));return s;}
static void request(EldenSensorEndpoint &e,uint64_t &cookie){CHECK(!e.reserve_cookie(e.context,7,9,&cookie));uint8_t data[40]={0};put64(data+8,cookie);CHECK(!e.send_epoch(e.context,7,9,data,sizeof(data)));}
int main(){
    reset();EldenSensorEndpoint e{};auto *p=open(e);CHECK(state_delivered==1&&status(p).connected==0);online();uint64_t cookie=0;request(e,cookie);CHECK(sent==1&&status(p).pending);
    CHECK(e.complete_cookie(e.context,7,9,cookie)==-116&&completed==0);uint8_t data[16]={0};put64(data,cookie+16);channel.client.receive(channel.context,1,data,16);CHECK(delivered==0&&status(p).stale==1);
    put64(data,cookie);channel.client.receive(channel.context,2,data,16);CHECK(delivered==0&&status(p).stale==2);channel.client.receive(channel.context,1,data,16);CHECK(delivered==1&&status(p).received==1);
    channel.client.receive(channel.context,1,data,16);CHECK(delivered==1);CHECK(!e.complete_cookie(e.context,7,9,cookie)&&completed==1&&!status(p).pending);
    CHECK(elden_sensor_adsp_endpoint_close(p,7,9)==-11&&closed_calls==1&&retired==0);channel.status.close_sent=channel.status.close_acked=channel.status.peer_closed=channel.status.fenced=1;
    retirement_error=-16;CHECK(elden_sensor_adsp_endpoint_close(p,7,9)==-16&&retired==0&&!status(p).terminal);retirement_error=0;CHECK(!elden_sensor_adsp_endpoint_close(p,7,9)&&retired==1&&status(p).terminal);
    CHECK(!elden_sensor_adsp_endpoint_close(p,7,9)&&retired==1);
    irq=true;CHECK(elden_sensor_adsp_endpoint_close(p,7,9)==-16);irq=false;
    reset();e={};up_at_open=true;p=open(e);CHECK(status(p).connected==1&&status(p).channel_epoch==1&&state_delivered==1);
    reset();e={};p=open(e);online();request(e,cookie);channel.client.state(channel.context,2,0);CHECK(status(p).error==-116&&!status(p).connected&&status(p).channel_epoch==1);CHECK(e.admit(e.context,7,9,0)==-125);
    reset();e={};p=open(e);online();request(e,cookie);send_error=-1;uint8_t msg[40]={0};put64(msg+8,cookie);CHECK(e.send_epoch(e.context,7,9,msg,40)==-11&&status(p).error==-11);
    reset();e={};p=open(e);online();CHECK(e.reserve_cookie(e.context,7,10,&cookie)==-116);CHECK(!e.reserve_cookie(e.context,7,9,&cookie));uint64_t extra;CHECK(e.reserve_cookie(e.context,7,9,&extra)==-16);
    reset();e={};p=open(e);online();uint8_t short_reply[8]={0};channel.client.receive(channel.context,1,short_reply,8);CHECK(status(p).malformed==1&&status(p).error==-84);
    reset();e={};p=open(e);e.wait(e.context,1);CHECK(blocked==0); // attach notification already queued
    inject_event=true;e.wait(e.context,1);CHECK(cleared==1&&blocked==0);e.wait(e.context,1);CHECK(blocked==1);
    reset();e={};p=open(e);online();channel.status.error=QcomGlinkCorrupt; // don't substitute physical stop on corruption
    CHECK(elden_sensor_adsp_endpoint_close(p,7,9)==-11);CHECK(elden_sensor_adsp_endpoint_close(p,7,9)==QcomGlinkCorrupt&&!status(p).terminal);
    reset();e={};p=open(e,false);online();CHECK(e.reserve_cookie(e.context,7,9,&cookie)==-19);CHECK(!elden_sensor_adsp_endpoint_attach(p,(EldenSensorMemoryBinding *)1,7,9));CHECK(elden_sensor_adsp_endpoint_attach(p,(EldenSensorMemoryBinding *)1,7,9)==-16);
    unsigned count=scenarios;reset();std::printf("PASS %u actual epoch-safe sensor endpoint/response/attach/wait/retirement scenarios, %u checks; OS/channel/memory owner peers modeled\n",count,checks);return 0;
}
