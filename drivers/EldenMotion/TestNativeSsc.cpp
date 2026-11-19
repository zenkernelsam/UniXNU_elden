/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Runs NativeSscMethods.inc and real manager/codec. Only OS scheduling, shared
 * QRTR physical service, clocks/mount and IOHID are modeled. No native fixture. */
#include "NativeSsc.h"
#include "NativeHid.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};static unsigned scenarios;
#define CHECK(v) do{checks.fetch_add(1);if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using event_t=void *;using thread_t=void *;using wait_result_t=int;using boolean_t=int;
enum {KERN_SUCCESS=0,THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1,kIOReturnSuccess=0,kIOReturnNotReady=2};
static constexpr auto THREAD_CONTINUE_NULL=nullptr;
struct IOSimpleLock {std::mutex lock;};static std::vector<IOSimpleLock *> allocated_locks;static std::vector<void *> allocations;
static IOSimpleLock *IOSimpleLockAlloc(){auto *l=new IOSimpleLock;allocated_locks.push_back(l);return l;}
static void IOSimpleLockLock(IOSimpleLock *l){l->lock.lock();}static void IOSimpleLockUnlock(IOSimpleLock *l){l->lock.unlock();}
static void *IOMallocData(size_t n){void *p=std::calloc(1,n);if(p)allocations.push_back(p);return p;}
static void IOFreeData(void *p,size_t){std::free(p);for(void *&q:allocations)if(q==p)q=nullptr;}
static std::atomic<uint64_t> now_ticks{19200000};static std::thread worker_thread;static bool interrupt_context;
static thread_local thread_t identity_thread=(void *)2;
static thread_t current_thread(){return identity_thread;}
static void thread_terminate(thread_t){CHECK(identity_thread==(void *)1);}
static void thread_deallocate(thread_t){}
static uint64_t mach_absolute_time(){return now_ticks.load();}
struct mach_timebase_info_data_t {uint32_t numer,denom;};
static void clock_timebase_info(mach_timebase_info_data_t *out){out->numer=625;out->denom=12;}
static boolean_t ml_at_interrupt_context(){return interrupt_context;}
static void IOSleep(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
static std::mutex wait_mutex;static std::condition_variable condition;static std::atomic<unsigned> wakes{0};
static unsigned wait_generation;static uint32_t wait_ms;static bool inject_wait_event;
static std::atomic<bool> freeze_scheduler{false},scheduler_waiting{false};
static void thread_wakeup(event_t){wakes.fetch_add(1);condition.notify_all();}
static void assert_wait_timeout(event_t event,int mode,uint32_t ms,int scale){
    CHECK(mode==THREAD_UNINT&&scale==1);wait_generation=wakes.load();wait_ms=ms;
    if(inject_wait_event){inject_wait_event=false;auto *p=(uint32_t *)event;__atomic_fetch_or(p,32u,__ATOMIC_RELEASE);thread_wakeup(event);}
}
static void clear_wait(thread_t t,int result){CHECK(t==current_thread()&&result==THREAD_AWAKENED);}
static void thread_block(decltype(nullptr)){
    scheduler_waiting.store(true);
    while(freeze_scheduler.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    scheduler_waiting.store(false);
    std::unique_lock<std::mutex> lock(wait_mutex);condition.wait_for(lock,std::chrono::milliseconds(wait_ms>10?10:wait_ms),[]{return wakes.load()!=wait_generation;});
}
static int kernel_thread_start(void (*f)(void *,wait_result_t),void *p,thread_t *out){
    CHECK(!worker_thread.joinable());*out=(void *)1;worker_thread=std::thread([f,p]{identity_thread=(void *)1;f(p,0);});return 0;
}
struct Bytes {std::vector<uint8_t> b;};
static void raw(Bytes &b,const Bytes &v){b.b.insert(b.b.end(),v.b.begin(),v.b.end());}
static void fixed(Bytes &b,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)b.b.push_back((uint8_t)(v>>(i*8)));}
static void varint(Bytes &b,uint64_t v){do{b.b.push_back((uint8_t)((v&127)|(v>127?128:0)));v>>=7;}while(v);}
static void field(Bytes &b,unsigned f,unsigned wire,uint64_t v){varint(b,(uint64_t)f<<3|wire);if(!wire)varint(b,v);else fixed(b,v,wire==1?8:4);}
static void nested(Bytes &b,unsigned f,const Bytes &v){varint(b,(uint64_t)f<<3|2);varint(b,v.b.size());raw(b,v);}
static void tlv(Bytes &b,unsigned tag,const Bytes &v){b.b.push_back((uint8_t)tag);fixed(b,v.b.size(),2);raw(b,v);}
static EldenMotionSuid fixture_ids[2]={{0x1234,0x9876},{0x5678,0x8765}};
static Bytes sid(EldenMotionSuid id){Bytes b;field(b,1,1,id.low);field(b,2,1,id.high);return b;}
static Bytes reply(uint16_t txn,uint64_t client){Bytes body,v;fixed(v,0,4);tlv(body,2,v);v={};fixed(v,client,8);tlv(body,0x10,v);v={};fixed(v,0,4);tlv(body,0x11,v);
    Bytes b;b.b.push_back(2);fixed(b,txn,2);fixed(b,0x20,2);fixed(b,body.b.size(),2);raw(b,body);return b;}
static Bytes report(EldenMotionSuid id,uint32_t msg,const Bytes &payload,uint64_t stamp,uint64_t client){Bytes e,ev;field(ev,1,5,msg);field(ev,2,1,stamp);nested(ev,3,payload);nested(e,1,sid(id));nested(e,2,ev);
    Bytes body,v;fixed(v,client,8);tlv(body,1,v);v={};fixed(v,e.b.size(),2);raw(v,e);tlv(body,2,v);Bytes b;b.b.push_back(4);fixed(b,0,2);fixed(b,0x21,2);fixed(b,body.b.size(),2);raw(b,body);return b;}
static Bytes text_datum(const char *s){Bytes text,d,v;text.b.assign(s,s+std::strlen(s));nested(d,2,text);nested(v,1,d);return v;}
static Bytes scalar_datum(unsigned kind,uint64_t val){Bytes d,v;field(d,kind,kind==3?5:kind==4?1:0,val);nested(v,1,d);return v;}
static void attr(Bytes &out,unsigned id,const Bytes &value){Bytes a;field(a,1,0,id);nested(a,2,value);nested(out,1,a);}
static Bytes attributes(unsigned kind){Bytes p;attr(p,0,text_datum("MODEL"));attr(p,1,text_datum("PEER"));attr(p,2,text_datum(kind==1?"accel":"gyro"));attr(p,3,scalar_datum(5,1));attr(p,5,text_datum(kind==1?"sns_accel.proto":"sns_gyro.proto"));
    Bytes rates;for(uint32_t r:{0x41c80000u,0x42480000u}){Bytes d;field(d,3,5,r);nested(rates,1,d);}attr(p,6,rates);
    Bytes range,pair,sub;for(uint32_t v:{0xc1800000u,0x41800000u}){Bytes d;field(d,3,5,v);nested(pair,1,d);}nested(sub,1,pair);nested(range,1,sub);attr(p,11,range);attr(p,16,scalar_datum(4,0));attr(p,21,scalar_datum(5,1));return p;}
struct Port {EldenMotionTransportClient client{};void *context=nullptr;unsigned conn=0;bool closed=false;};
struct RouterPeer {
    Port ports[3];std::atomic<unsigned> opens{0},closed{0},configs{0},disables{0},samples{0},hid_starts{0},hid_stops{0};
    std::atomic<bool> announced{true},fail_withdraw{false},suppress_disable{false},qualify_fail{false},ready_hid{false};
    EldenSscServer server{400,1,2,80,5};EldenMotionNativeProducer producer{};std::mutex producer_lock;
    unsigned open_fail=99;
};
static RouterPeer *peer;
static int physical(void *p,uint64_t owner,uint64_t epoch,int cleanup){CHECK(p==peer&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static void *open_port(void *p,uint64_t owner,uint64_t epoch,uint32_t service,unsigned conn,const EldenMotionTransportClient *client,void *context){
    CHECK(p==peer&&owner==7&&epoch==9&&service==400&&conn<3);if(conn==peer->open_fail)return nullptr;
    Port &port=peer->ports[conn];port.client=*client;port.context=context;port.conn=conn;peer->opens.fetch_add(1);
    if(peer->announced.load())client->server(context,7,9,&peer->server,1);return &port;
}
static int level(void *p,void *handle,uint64_t owner,uint64_t epoch,EldenSscServer *out){CHECK(p==peer&&handle&&owner==7&&epoch==9);if(!peer->announced.load())return 0;*out=peer->server;return 1;}
static void push(Port &port,const Bytes &b){port.client.receive(port.context,7,9,&peer->server,b.b.data(),(uint32_t)b.b.size());}
static int send(void *p,void *handle,uint64_t owner,uint64_t epoch,const EldenSscServer *server,const uint8_t *data,uint32_t size){
    CHECK(p==peer&&handle&&owner==7&&epoch==9&&server->generation==5&&size>12&&data[0]==0);
    auto *port=(Port *)handle;uint16_t txn=data[1]|(uint16_t)data[2]<<8;const uint8_t *request=data+12;uint32_t msg=0;unsigned found=0;
    for(uint32_t at=0;at+5<size-12;at++)if(request[at]==0x15){msg=request[at+1]|(uint32_t)request[at+2]<<8|(uint32_t)request[at+3]<<16|(uint32_t)request[at+4]<<24;found++;}
    CHECK(found==1);if(msg==10){peer->disables.fetch_add(1);if(peer->suppress_disable.load())return 0;}
    push(*port,reply(txn,100+port->conn));
    if(msg==512){
        bool gyro=false;for(uint32_t at=0;at+4<size;at++)if(!std::memcmp(data+at,"gyro",4))gyro=true;
        unsigned kind=gyro?2:1;const char *type=gyro?"gyro":"accel";Bytes payload,text;text.b.assign(type,type+std::strlen(type));nested(payload,1,text);nested(payload,2,sid(fixture_ids[kind-1]));
        push(*port,report({UINT64_C(0xabababababababab),UINT64_C(0xabababababababab)},768,payload,100,100));
    }else if(msg==1)push(*port,report(fixture_ids[port->conn-1],128,attributes(port->conn),110,100+port->conn));
    else if(msg==513){
        peer->configs.fetch_add(1);Bytes payload;field(payload,1,5,0x3f800000);field(payload,1,5,0);field(payload,1,5,0);field(payload,2,0,3);
        push(*port,report(fixture_ids[port->conn-1],1025,payload,now_ticks.load()-192,100+port->conn));
    }
    return 0;
}
static int withdraw(void *p,void *handle,uint64_t owner,uint64_t epoch,const EldenSscServer *){CHECK(p==peer&&handle&&owner==7&&epoch==9);if(peer->fail_withdraw.load())return -16;auto *port=(Port *)handle;CHECK(!port->closed);port->closed=true;peer->closed.fetch_add(1);return 0;}
static int qualify(void *p,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *a,EldenMotionSpec *s){CHECK(p==peer&&owner==7&&epoch==9&&kind>=1&&kind<=2&&a->range_count==1);if(peer->qualify_fail.load())return -13;
    s->unit=kind==1?EldenMotionMetersPerSecond2:EldenMotionRadiansPerSecond;s->max_abs_q16=16*65536;s->mount[0]=1;s->mount[1]=2;s->mount[2]=3;return 0;}
static int map(void *p,uint64_t owner,uint64_t epoch,uint64_t stamp,uint64_t *out){CHECK(p==peer&&owner==7&&epoch==9&&stamp);*out=stamp;return 0;}
extern "C" int elden_motion_native_hid_start(const EldenMotionNativeProducer *p){CHECK(p&&p->admit&&p->demand);CHECK(!p->admit(p->context,7,9,1,0)&&!p->admit(p->context,7,9,2,0));std::lock_guard<std::mutex> lock(peer->producer_lock);peer->producer=*p;peer->hid_starts.fetch_add(1);return 0;}
extern "C" int elden_motion_native_hid_bind(uint64_t owner,uint64_t epoch){CHECK(owner==7&&epoch==9);std::lock_guard<std::mutex> lock(peer->producer_lock);if(!peer->ready_hid.load()){peer->ready_hid.store(true);CHECK(!peer->producer.demand(peer->producer.context,7,9,1,25));}return 1;}
extern "C" int elden_motion_native_hid_packet(const EldenMotionSample *s){CHECK(s&&s->kind==1&&s->unit==EldenMotionG&&s->q16[0]==6682);CHECK(!peer->producer.admit(peer->producer.context,7,9,1,0));peer->samples.fetch_add(1);return 0;}
extern "C" int elden_motion_native_hid_stop(uint64_t owner,uint64_t epoch){CHECK(owner==7&&epoch==9);peer->hid_stops.fetch_add(1);return peer->producer.demand?peer->producer.demand(peer->producer.context,7,9,1,0):0;}
#include "NativeSscMethods.inc"
static void start(RouterPeer &p){peer=&p;now_ticks.store(19200000);wakes.store(0);
    EldenMotionTransport t{&p,7,9,1024,physical,open_port,level,send,withdraw};EldenMotionBoard b{&p,physical,qualify,map,100000,0,{0,0}};
    CHECK(!elden_motion_native_ssc_start(&t,&b));}
template<class F>static void wait_for(F predicate){for(unsigned i=0;i<500&&!predicate();i++){now_ticks.fetch_add(19200);std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(predicate());}
static void cleanup(){if(worker_thread.joinable())worker_thread.join();motion_context=nullptr;motion_attempted=0;for(auto *p:allocated_locks)delete p;allocated_locks.clear();for(void *p:allocations)std::free(p);allocations.clear();}
int main(){
    {
        scenarios++;RouterPeer p;start(p);wait_for([&]{return p.samples.load()==1;});CHECK(p.hid_starts==1&&p.configs==1);
        CHECK(elden_motion_native_ssc_request_stop(7,10)==EldenMotionStale);CHECK(!elden_motion_native_ssc_stop(7,9));CHECK(p.closed==3&&p.disables==2);
        EldenMotionNativeStatus s;CHECK(!elden_motion_native_ssc_status(&s)&&s.terminal&&s.stage==NativeStopped&&s.samples==1&&s.runtime_error==0);CHECK(elden_motion_native_ssc_recovery_stop());cleanup();
    }
    {
        scenarios++;RouterPeer p;p.open_fail=1;start(p);wait_for([&]{EldenMotionNativeStatus s;return !elden_motion_native_ssc_status(&s)&&s.terminal;});
        CHECK(p.closed==1&&p.hid_starts==0);EldenMotionNativeStatus s;CHECK(!elden_motion_native_ssc_status(&s)&&s.runtime_error==EldenSscAbsent&&s.error==0);cleanup();
    }
    {
        scenarios++;RouterPeer p;p.qualify_fail.store(true);start(p);wait_for([&]{EldenMotionNativeStatus s;return !elden_motion_native_ssc_status(&s)&&s.terminal;});CHECK(p.closed==3&&p.hid_starts==0);cleanup();
    }
    {
        scenarios++;RouterPeer p;start(p);wait_for([&]{return p.samples.load()==1;});p.fail_withdraw.store(true);
        CHECK(!elden_motion_native_ssc_request_stop(7,9));
        wait_for([&]{EldenMotionNativeStatus s;return !elden_motion_native_ssc_status(&s)&&s.stage==NativeStopping;});
        now_ticks.fetch_add(UINT64_C(115200000));signal(motion_context,1);
        wait_for([&]{EldenMotionNativeStatus s;return !elden_motion_native_ssc_status(&s)&&s.terminal;});CHECK(p.closed==0&&!elden_motion_native_ssc_recovery_stop());
        CHECK(motion_context->handles[0]&&motion_context->handles[1]&&motion_context->handles[2]);cleanup();
    }
    {
        scenarios++;RouterPeer p;p.announced.store(false);freeze_scheduler.store(true);start(p);wait_for([&]{return p.opens.load()==3&&scheduler_waiting.load();});
        Bytes packet=reply(1,100);for(unsigned i=0;i<17;i++)p.ports[0].client.receive(p.ports[0].context,7,9,&p.server,packet.b.data(),(uint32_t)packet.b.size());
        freeze_scheduler.store(false);condition.notify_all();
        wait_for([&]{EldenMotionNativeStatus s;return !elden_motion_native_ssc_status(&s)&&s.terminal;});CHECK(p.closed==3&&p.hid_starts==0);cleanup();
    }
    {
        scenarios++;RouterPeer p;start(p);wait_for([&]{return p.samples.load()==1;});identity_thread=(void *)1;CHECK(elden_motion_native_ssc_stop(7,9)==EldenSscBusy);identity_thread=(void *)2;
        auto *c=motion_context;
        // This direct wait exercise runs after real worker termination below;
        // the injected event is the only operation allowed after assert_wait.
        CHECK(!elden_motion_native_ssc_stop(7,9));if(worker_thread.joinable())worker_thread.join();
        __atomic_store_n(&c->pending,0u,__ATOMIC_RELEASE);inject_wait_event=true;CHECK(wait_events(c,1)==32);cleanup();
    }
    std::printf("PASS %u actual native worker/manager/router/HID/stop scenarios, %u checks; OS/shared QRTR/MMIO/clock/HID peers modeled\n",scenarios,checks.load());return 0;
}
