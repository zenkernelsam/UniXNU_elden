/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual original ADSP registry and fixed GLINK protocol. OS locks/scheduling,
 * SMEM FIFO/doorbells are modeled. No physical DSP/DMA stop acceptance. */
#include "AdspChannel.h"
#include "QcomGlink.h"
#if ELDEN_TEST_MOTION_QRTR
#include "QrtrTransport.h"
#include "QcomQrtr.h"
extern "C" void sheng_qrtr_start(void);
#endif
#if ELDEN_TEST_SENSOR_ENDPOINT
#include "AdspEndpoint.h"
#endif
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};static unsigned scenarios;
#define CHECK(v) do{checks.fetch_add(1);if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using IOLock=std::mutex;using vm_size_t=size_t;
static std::vector<void *> allocations;
static void *IOMallocData(size_t n){void *p=std::calloc(1,n);if(p)allocations.push_back(p);return p;}
static void IOFreeData(void *p,size_t){for(void *&value:allocations)if(value==p)value=nullptr;std::free(p);}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}
static unsigned wakes;static void wake(uint32_t bits){CHECK(bits==16);wakes++;}
#if ELDEN_TEST_SENSOR_ENDPOINT
using IOSimpleLock=std::mutex;using boolean_t=int;using event_t=void *;
static std::vector<IOSimpleLock *> endpoint_locks;static uint64_t test_ticks=19200000;
static IOSimpleLock *IOSimpleLockAlloc(){auto *p=new IOSimpleLock;endpoint_locks.push_back(p);return p;}
static void IOSimpleLockLock(IOSimpleLock *p){p->lock();}static void IOSimpleLockUnlock(IOSimpleLock *p){p->unlock();}
static boolean_t ml_at_interrupt_context(){return false;}static uint64_t mach_absolute_time(){return test_ticks;}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *ns){*ns=ticks*625/12;}
enum {THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1};
static constexpr auto THREAD_CONTINUE_NULL=nullptr;
static void thread_wakeup(event_t){wakes++;}static void *current_thread(){return (void *)1;}
static void assert_wait_timeout(event_t,int mode,uint32_t ms,int scale){CHECK(mode==1&&ms&&scale==1);}
static void clear_wait(void *p,int result){CHECK(p==(void *)1&&result==THREAD_AWAKENED);}
static void thread_block(decltype(nullptr)){}
static unsigned memory_state,endpoint_received,endpoint_retired,endpoint_completed;static uint64_t endpoint_cookie;
extern "C" int elden_sensor_memory_binding_channel(EldenSensorMemoryBinding *b,uint64_t owner,uint64_t epoch,int up){CHECK(b==(EldenSensorMemoryBinding *)1&&owner==7&&epoch==9&&(up==0||up==1));memory_state=up?1:0;return 0;}
extern "C" int elden_sensor_memory_binding_receive(EldenSensorMemoryBinding *b,uint64_t owner,uint64_t epoch,const uint8_t *data,uint32_t size){CHECK(b==(EldenSensorMemoryBinding *)1&&owner==7&&epoch==9&&data&&size>=12);endpoint_received++;return 0;}
#endif
constexpr unsigned kClients=QCOM_GLINK_MAX_CHANNELS,kQueue=8,kEvSend=16;
struct Message {uint32_t length,epoch;uint8_t data[SHENG_ADSP_MAX_MESSAGE];};
#include "AdspChannelStruct.inc"
static ShengAdspChannel gClients[kClients];static uint32_t gClientCount;static IOLock client_lock;static IOLock *gClientLock=&client_lock;
static QcomGlink gGlink,remote;static bool gGlinkStarted;static int32_t native_adsp_audio[8];
static uint32_t fifo_indexes[4];static uint8_t fifo_local[4096],fifo_remote[4096];
static std::atomic<bool> block_kick{false},kick_entered{false},release_kick{false};
static void kick(void *){if(block_kick.load()){kick_entered.store(true);while(!release_kick.load())std::this_thread::yield();}}
static std::atomic<bool> callback_entered{false},callback_release{false};
struct Seen {unsigned up=0,down=0,received=0;uint32_t epoch=0;std::vector<uint8_t> last;};
static void epoch_state(void *p,uint32_t epoch,int up){auto *s=(Seen *)p;s->epoch=epoch;if(up)s->up++;else s->down++;}
static void epoch_receive(void *p,uint32_t epoch,const uint8_t *data,uint32_t size){auto *s=(Seen *)p;s->received++;s->epoch=epoch;s->last.assign(data,data+size);}
static void held_receive(void *p,uint32_t epoch,const uint8_t *data,uint32_t size){callback_entered=true;while(!callback_release.load())std::this_thread::yield();epoch_receive(p,epoch,data,size);}
static void old_state(void *p,int up){epoch_state(p,0,up);}
static void old_receive(void *p,const uint8_t *data,uint32_t size){epoch_receive(p,0,data,size);}
static void remote_state(void *p,QcomGlinkChannel *,int up){epoch_state(p,0,up);}
static void remote_receive(void *p,QcomGlinkChannel *,const uint8_t *data,uint32_t size){epoch_receive(p,0,data,size);}
#include "AdspRegistryMethods.inc"
#include "AdspPublicMethods.inc"
#if ELDEN_TEST_SENSOR_ENDPOINT
#include "AdspEndpointMethods.inc"
static int ep_admit(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int ep_reserve(void *,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(owner==7&&epoch==9);endpoint_cookie++;*out=endpoint_cookie<<4|2;return 0;}
static int ep_complete(void *,uint64_t owner,uint64_t epoch,uint64_t cookie){CHECK(owner==7&&epoch==9&&cookie==(endpoint_cookie<<4|2));endpoint_completed++;return 0;}
static int ep_retire(void *,uint64_t owner,uint64_t epoch,uint32_t e,int pending){CHECK(owner==7&&epoch==9&&e==1&&(pending==0||pending==1));endpoint_retired++;return 0;}
#endif
static const EldenAdspEpochClient owned={epoch_state,epoch_receive};static const ShengAdspClient legacy={old_state,old_receive};
#if ELDEN_TEST_MOTION_QRTR
static std::mutex qrtr_lock;static unsigned qrtr_received,qrtr_retired;static EldenSscServer qrtr_server{};
static void qr_lock(void *){qrtr_lock.lock();}static void qr_unlock(void *){qrtr_lock.unlock();}
static int qr_admit(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int qr_retire(void *,const EldenQrtrPortStatus *s){CHECK(s&&s->closing&&!s->callbacks&&!s->inflight&&s->del_client_queued);qrtr_retired++;return 0;}
static void qr_server(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *s,int up){CHECK(owner==7&&epoch==9&&s&&(up==0||up==1));qrtr_server=*s;}
static void qr_receive(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *s,const uint8_t *data,uint32_t size){CHECK(owner==7&&epoch==9&&s&&data&&size==8);qrtr_received++;qrtr_server=*s;}
static uint32_t qr_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void qr_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
#endif
static const QcomGlinkClient remoter={remote_state,remote_receive};
static void reset(){
#if ELDEN_TEST_SENSOR_ENDPOINT
    for(auto *p:endpoint_locks)delete p;endpoint_locks.clear();memory_state=endpoint_received=endpoint_retired=endpoint_completed=0;endpoint_cookie=0;
#endif
    for(void *p:allocations)std::free(p);allocations.clear();std::memset(gClients,0,sizeof(gClients));gClientCount=0;wakes=0;gGlinkStarted=false;std::memset(fifo_indexes,0,sizeof(fifo_indexes));
    std::memset(fifo_local,0,sizeof(fifo_local));std::memset(fifo_remote,0,sizeof(fifo_remote));
    block_kick=false;kick_entered=false;release_kick=false;
    callback_entered=false;callback_release=false;
    qcom_glink_init(&gGlink,{&fifo_indexes[1],&fifo_indexes[0],fifo_local,sizeof(fifo_local)},{&fifo_indexes[3],&fifo_indexes[2],fifo_remote,sizeof(fifo_remote)},kick,nullptr);
    qcom_glink_init(&remote,{&fifo_indexes[3],&fifo_indexes[2],fifo_remote,sizeof(fifo_remote)},{&fifo_indexes[1],&fifo_indexes[0],fifo_local,sizeof(fifo_local)},kick,nullptr);
    scenarios++;
}
static void round(){CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();pumpSends();CHECK(qcom_glink_poll(&remote)>=0);}
static void settle(){for(unsigned i=0;i<30;i++)round();}
static QcomGlinkChannel *connect(ShengAdspChannel *c,Seen *seen){
    auto *storage=(uint8_t *)IOMallocData(4*1024);auto *other=qcom_glink_channel(&remote,c->name,1024,4,storage,&remoter,seen,1);CHECK(other);
    gGlinkStarted=true;applyClients();settle();CHECK(c->connected&&c->glink->up&&other->up);return other;
}
static EldenAdspChannelStatus status(ShengAdspChannel *c){EldenAdspChannelStatus s{};CHECK(!elden_adsp_channel_status(c,&s));return s;}
static void inject(QcomGlinkFifo &fifo,uint16_t opcode,uint16_t p1,uint32_t p2){
    uint8_t command[8]={(uint8_t)opcode,(uint8_t)(opcode>>8),(uint8_t)p1,(uint8_t)(p1>>8),(uint8_t)p2,(uint8_t)(p2>>8),(uint8_t)(p2>>16),(uint8_t)(p2>>24)};
    uint32_t head=*fifo.head;for(auto byte:command){fifo.data[head]=byte;head=(head+1)%fifo.length;}*fifo.head=head;
}
int main(){
    reset();Seen a,b;auto *c=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&a);CHECK(c&&gClientCount==1);
    auto *r=connect(c,&b);CHECK(a.up==1&&a.epoch==1&&status(c).epoch==1);
    uint8_t packet[40]={2,1,2,3};CHECK(!sheng_adsp_send_epoch(c,1,packet,sizeof(packet)));settle();CHECK(b.received==1&&b.last.size()==40);
    CHECK(qcom_glink_send(&remote,r,packet,16)==0);settle();CHECK(a.received==1&&a.epoch==1&&a.last.size()==16);
    CHECK(sheng_adsp_send_epoch(c,2,packet,sizeof(packet))==-3&&status(c).queued==0);
    CHECK(!elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&a));CHECK(!sheng_adsp_channel("fastrpcglink-apps-dsp",1024,4,0,&legacy,&a));
    CHECK(!elden_adsp_channel_close(c,1));CHECK(sheng_adsp_send_epoch(c,1,packet,40)==-3);CHECK(!status(c).fenced);
    settle();auto closed=status(c);CHECK(closed.closing&&closed.close_sent&&closed.close_acked&&closed.peer_closed&&closed.fenced&&!closed.queued&&!closed.callbacks);
    CHECK(!elden_adsp_channel_close(c,1));CHECK(!elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&a));
    reset();Seen old,other;auto *lc=sheng_adsp_channel("PMIC_RTR_ADSP_APPS",1024,4,0,&legacy,&old);CHECK(lc);auto *lr=connect(lc,&other);
    CHECK(!sheng_adsp_send(lc,packet,16));settle();CHECK(other.received==1);inject(remote.tx,QCOM_GLINK_CLOSE,lr->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);CHECK(old.down==1&&!lc->connected&&!lc->owned_status.closing);
    reset();Seen late,opened;auto *remote_storage=(uint8_t *)IOMallocData(4096);auto *rr=qcom_glink_channel(&remote,"fastrpcglink-apps-dsp",1024,4,remote_storage,&remoter,&opened,1);CHECK(rr);
    gGlinkStarted=true;settle();CHECK(gGlink.unknown_opens==1&&!rr->up);
    auto *late_channel=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&late);CHECK(late_channel);applyClients();settle();CHECK(late.up==1&&rr->up);
    reset();Seen queued,remote_seen;auto *qc=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&queued);CHECK(qc);connect(qc,&remote_seen);
    for(unsigned i=0;i<8;i++)CHECK(!sheng_adsp_send_epoch(qc,1,packet,40));CHECK(sheng_adsp_send_epoch(qc,1,packet,40)==-1);CHECK(!elden_adsp_channel_close(qc,1));settle();CHECK(status(qc).fenced&&remote_seen.received==0);
    reset();Seen unanswered,peer;auto *un=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&unanswered);connect(un,&peer);CHECK(!elden_adsp_channel_close(un,1));serviceOwnedChannels();CHECK(status(un).close_sent&&!status(un).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,un->glink->lcid+1,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(!status(un).close_acked&&!status(un).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,un->glink->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(un).close_acked&&!status(un).peer_closed&&!status(un).fenced);
    reset();Seen never;auto *n=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&never);CHECK(n&&!elden_adsp_channel_close(n,0));CHECK(status(n).fenced&&!n->glink&&!gGlinkStarted);
    gGlinkStarted=true;applyClients();serviceOwnedChannels();CHECK(status(n).fenced&&!n->glink);
    reset();Seen pending;auto *drain=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&pending);CHECK(drain);gGlinkStarted=true;applyClients();drain->owned_status.callbacks=1;CHECK(!elden_adsp_channel_close(drain,0));serviceOwnedChannels();CHECK(!status(drain).fenced);drain->owned_status.callbacks=0;serviceOwnedChannels();CHECK(status(drain).fenced);
    reset();Seen bad;auto *broken=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&bad);Seen br;connect(broken,&br);CHECK(!elden_adsp_channel_close(broken,1));gGlink.broken=1;serviceOwnedChannels();CHECK(status(broken).error==QcomGlinkCorrupt&&!status(broken).fenced);
    reset();Seen many;for(unsigned i=0;i<kClients;i++){char name[32];std::snprintf(name,sizeof name,"channel%u",i);CHECK(elden_adsp_channel_epoch(name,64,2,0,&owned,&many));}
    CHECK(!elden_adsp_channel_epoch("overflow",64,2,0,&owned,&many));
    reset();Seen initiating,closing_peer;auto *ic=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,1,&owned,&initiating);CHECK(ic);
    auto *is=(uint8_t *)IOMallocData(4096);auto *ip=qcom_glink_channel(&remote,ic->name,1024,4,is,&remoter,&closing_peer,0);CHECK(ip);
    gGlinkStarted=true;applyClients();for(unsigned i=0;i<5;i++){CHECK(qcom_glink_poll(&gGlink)>=0);CHECK(qcom_glink_poll(&remote)>=0);}
    CHECK(!elden_adsp_channel_close(ic,status(ic).epoch));settle();CHECK(status(ic).fenced);
    reset();Seen partial;auto *pc=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&partial);CHECK(pc);gGlinkStarted=true;applyClients();
    for(unsigned i=0;i<4;i++){CHECK(qcom_glink_poll(&gGlink)>=0);CHECK(qcom_glink_poll(&remote)>=0);}CHECK(gGlink.version_agreed);
    pc->glink->remote_open=1;pc->glink->rcid=9;pc->glink->ack_owed=1;CHECK(!elden_adsp_channel_close(pc,0));serviceOwnedChannels();CHECK(status(pc).close_sent&&!status(pc).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,pc->glink->lcid,0);inject(remote.tx,QCOM_GLINK_CLOSE,9,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(pc).fenced&&partial.up==0);
    reset();Seen only_peer,missing_ack;auto *op=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&only_peer);auto *or_=connect(op,&missing_ack);
    CHECK(!elden_adsp_channel_close(op,1));serviceOwnedChannels();CHECK(status(op).close_sent);
    inject(remote.tx,QCOM_GLINK_CLOSE,or_->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();
    CHECK(status(op).peer_closed&&!status(op).close_acked&&!status(op).fenced);serviceOwnedChannels();CHECK(!status(op).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,op->glink->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(op).fenced);
    reset();Seen peer_first,peer_done;auto *pf=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&peer_first);auto *pfr=connect(pf,&peer_done);
    inject(remote.tx,QCOM_GLINK_CLOSE,pfr->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);
    CHECK(!pf->connected&&pf->glink->elden_ever_opened&&pf->glink->elden_close_sent&&!pf->glink->elden_close_acked);
    CHECK(!elden_adsp_channel_close(pf,status(pf).epoch));serviceOwnedChannels();CHECK(status(pf).peer_closed&&!status(pf).close_acked&&!status(pf).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,pf->glink->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(pf).fenced);
    reset();Seen in_send,send_peer;auto *pump=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&in_send);connect(pump,&send_peer);
    CHECK(!sheng_adsp_send_epoch(pump,1,packet,40));block_kick=true;std::thread sender([]{pumpSends();});
    while(!kick_entered.load())std::this_thread::yield();CHECK(status(pump).pumping==1&&!elden_adsp_channel_close(pump,1)&&!status(pump).fenced);
    release_kick=true;sender.join();block_kick=false;settle();CHECK(status(pump).fenced&&send_peer.received==1);
    reset();Seen blocked_local,blocked_peer;auto *back=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&blocked_local);auto *back_peer=connect(back,&blocked_peer);
    CHECK(!elden_adsp_channel_close(back,1));fifo_indexes[0]=24;fifo_indexes[1]=0;serviceOwnedChannels();CHECK(!status(back).close_sent&&!status(back).fenced);
    fifo_indexes[0]=fifo_indexes[1];settle();CHECK(status(back).fenced);
    reset();Seen blocked_ack,remote_ack;auto *ack=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&owned,&blocked_ack);auto *ack_peer=connect(ack,&remote_ack);(void)back_peer;
    CHECK(!elden_adsp_channel_close(ack,1));serviceOwnedChannels();fifo_indexes[0]=24;fifo_indexes[1]=0;
    inject(remote.tx,QCOM_GLINK_CLOSE,ack_peer->lcid,0);uint32_t previous=fifo_indexes[2];CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(fifo_indexes[2]==previous&&!status(ack).peer_closed&&!status(ack).fenced);
    fifo_indexes[0]=fifo_indexes[1];CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(ack).peer_closed&&!status(ack).close_acked&&!status(ack).fenced);
    inject(remote.tx,QCOM_GLINK_CLOSE_ACK,ack->glink->lcid,0);CHECK(qcom_glink_poll(&gGlink)>=0);serviceOwnedChannels();CHECK(status(ack).fenced);
    reset();Seen active_callback,callback_peer;const EldenAdspEpochClient held={epoch_state,held_receive};
    auto *cb=elden_adsp_channel_epoch("fastrpcglink-apps-dsp",1024,4,0,&held,&active_callback);auto *cb_peer=connect(cb,&callback_peer);
    CHECK(!qcom_glink_send(&remote,cb_peer,packet,16));std::thread receiver([]{CHECK(qcom_glink_poll(&gGlink)>=0);});
    while(!callback_entered.load())std::this_thread::yield();CHECK(status(cb).callbacks==1&&!elden_adsp_channel_close(cb,1)&&!status(cb).fenced);
    callback_release=true;receiver.join();settle();CHECK(status(cb).fenced&&active_callback.received==1);
#if ELDEN_TEST_SENSOR_ENDPOINT
    reset();Seen dsp;EldenSensorAdspAuthority authority{nullptr,7,9,ep_admit,ep_reserve,ep_complete,ep_retire,nullptr};EldenSensorAdspEndpoint *ep=nullptr;EldenSensorEndpoint ops{};
    CHECK(!elden_sensor_adsp_endpoint_open(&authority,1024,4,&ep,&ops));CHECK(!elden_sensor_adsp_endpoint_attach(ep,(EldenSensorMemoryBinding *)1,7,9));
    auto *actual=connect(ep->channel,&dsp);CHECK(memory_state==1);uint64_t cookie=0;CHECK(!ops.reserve_cookie(ops.context,7,9,&cookie));
    uint8_t invoke[40]={0};for(unsigned i=0;i<8;i++)invoke[8+i]=(uint8_t)(cookie>>(i*8));CHECK(!ops.send_epoch(ops.context,7,9,invoke,40));settle();CHECK(dsp.received==1&&dsp.last.size()==40);
    uint8_t response[16]={0};for(unsigned i=0;i<8;i++)response[i]=(uint8_t)(cookie>>(i*8));CHECK(!qcom_glink_send(&remote,actual,response,16));settle();CHECK(endpoint_received==1);
    CHECK(!ops.complete_cookie(ops.context,7,9,cookie)&&endpoint_completed==1);CHECK(ops.withdraw(ops.context,7,9)==-11);settle();CHECK(!ops.withdraw(ops.context,7,9)&&endpoint_retired==1);
    CHECK(!ops.withdraw(ops.context,7,9)&&endpoint_retired==1);
#endif
#if ELDEN_TEST_MOTION_QRTR
    // Actual original ADSP registry/queue/GLINK paired with the real sole
    // QRTR producer and SSC adapter. Physical DSP/retirement are modeled.
    reset();Seen qr_peer;EldenQrtrPortOps qr_ops{nullptr,7,9,2048,8,qr_lock,qr_unlock,qr_admit,qr_retire};
    CHECK(!elden_qrtr_ports_prepare(&qr_ops));sheng_qrtr_start();CHECK(gClientCount==1&&!std::strcmp(gClients[0].name,"IPCRTR"));
    auto *qr_channel=&gClients[0];auto *qr_remote=connect(qr_channel,&qr_peer);uint8_t qr_body[20]={0},qr_packet[128];
    qr_put32(qr_body,QCOM_QRTR_HELLO);uint32_t qr_size=qcom_qrtr_frame(qr_packet,sizeof qr_packet,QCOM_QRTR_HELLO,5,QCOM_QRTR_PORT_CTRL,1,QCOM_QRTR_PORT_CTRL,qr_body,sizeof qr_body);
    CHECK(qr_size&&!qcom_glink_send(&remote,qr_remote,qr_packet,qr_size));settle();
    qr_put32(qr_body,QCOM_QRTR_NEW_SERVER);qr_put32(qr_body+4,400);qr_put32(qr_body+8,0x100);qr_put32(qr_body+12,5);qr_put32(qr_body+16,0x9000);
    qr_size=qcom_qrtr_frame(qr_packet,sizeof qr_packet,QCOM_QRTR_NEW_SERVER,5,QCOM_QRTR_PORT_CTRL,1,QCOM_QRTR_PORT_CTRL,qr_body,sizeof qr_body);
    CHECK(!qcom_glink_send(&remote,qr_remote,qr_packet,qr_size));settle();EldenMotionTransport qr_transport{};
    CHECK(!elden_motion_qrtr_transport(&qr_transport));const EldenMotionTransportClient qr_client={qr_server,qr_receive};void *qr_handles[3];EldenSscServer qr_servers[3];
    for(unsigned i=0;i<3;i++){qr_handles[i]=qr_transport.open(qr_transport.context,7,9,400,i,&qr_client,nullptr);CHECK(qr_handles[i]&&qr_transport.level(qr_transport.context,qr_handles[i],7,9,&qr_servers[i])==1);}
    CHECK(qr_servers[0].generation==qr_servers[1].generation&&qr_servers[1].generation==qr_servers[2].generation);
    uint8_t qr_data[8]={1,2,3};CHECK(!qr_transport.send_epoch(qr_transport.context,qr_handles[0],7,9,&qr_servers[0],qr_data,sizeof qr_data));settle();
    CHECK(qr_peer.last.size()==40&&qr_u32(qr_peer.last.data()+4)==QCOM_QRTR_DATA);uint32_t qr_port=qr_u32(qr_peer.last.data()+12);
    qr_size=qcom_qrtr_frame(qr_packet,sizeof qr_packet,QCOM_QRTR_DATA,5,0x9000,1,qr_port,qr_data,sizeof qr_data);qr_put32(qr_packet+16,1);
    CHECK(!qcom_glink_send(&remote,qr_remote,qr_packet,qr_size));settle();CHECK(qrtr_received==1&&qrtr_server.generation==qr_servers[0].generation);
    CHECK(qr_u32(qr_peer.last.data()+4)==QCOM_QRTR_RESUME_TX&&qr_u32(qr_peer.last.data()+36)==1&&qr_u32(qr_peer.last.data()+40)==qr_port);
    CHECK(qr_transport.withdraw(qr_transport.context,qr_handles[0],7,9,&qr_servers[0])==-11);settle();
    CHECK(!qr_transport.withdraw(qr_transport.context,qr_handles[0],7,9,&qr_servers[0])&&qrtr_retired==1);
    CHECK(qr_u32(qr_peer.last.data()+4)==QCOM_QRTR_DEL_CLIENT&&qr_u32(qr_peer.last.data()+24)==5);
#endif
    unsigned count=scenarios;reset();std::printf("PASS %u actual original ADSP/GLINK epoch/queue/callback/close/late-register scenarios, %u checks; SMEM/locks/doorbells modeled\n",count,checks.load());return 0;
}
