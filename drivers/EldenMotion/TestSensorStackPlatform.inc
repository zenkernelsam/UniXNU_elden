/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* OS thread/clock/channel/HID/DSP peers for a composed actual runtime test.
 * Uses actual memory allocator, file cache/SHA, RPC, endpoint, QRTR and SSC.
 * Shared ADSP/GLINK protocol itself has separate actual-method qualification. */
#include "SensorRuntime.h"
#include "NativeHid.h"
#include "QcomQrtr.h"
#include <condition_variable>
#include <deque>
using IOSimpleLock=std::mutex;using thread_t=void *;using event_t=void *;using wait_result_t=int;
enum {KERN_SUCCESS=0,THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1,kIOReturnNotReady=2};
static constexpr auto THREAD_CONTINUE_NULL=nullptr;
static std::mutex stack_alloc_lock,stack_wait_lock;static std::condition_variable stack_wait;
static std::vector<std::thread> stack_threads;static std::vector<IOSimpleLock *> stack_locks;
static std::atomic<uint64_t> stack_ms{1000};static std::atomic<unsigned> stack_wakes{0};
static thread_local thread_t stack_identity=(void *)2;static thread_local unsigned stack_wait_generation;static thread_local uint32_t stack_wait_ms;
struct mach_timebase_info_data_t {uint32_t numer,denom;};
static thread_t current_thread(){return stack_identity;}
static uint64_t mach_absolute_time(){return stack_ms.load()*19200;}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *out){*out=ticks*625/12;}
static void clock_timebase_info(mach_timebase_info_data_t *out){out->numer=625;out->denom=12;}
static IOSimpleLock *IOSimpleLockAlloc(){auto *p=new IOSimpleLock;std::lock_guard<std::mutex> guard(stack_alloc_lock);stack_locks.push_back(p);return p;}
static void IOSimpleLockLock(IOSimpleLock *p){p->lock();}static void IOSimpleLockUnlock(IOSimpleLock *p){p->unlock();}
static void thread_wakeup(event_t){stack_wakes.fetch_add(1);stack_wait.notify_all();}
static void assert_wait_timeout(event_t,int mode,uint32_t ms,int scale){CHECK(mode==1&&ms&&scale==1);stack_wait_generation=stack_wakes.load();stack_wait_ms=ms;}
static void clear_wait(thread_t t,int result){CHECK(t==current_thread()&&result==2);}
static void thread_block(decltype(nullptr)){std::unique_lock<std::mutex> lock(stack_wait_lock);stack_wait.wait_for(lock,std::chrono::milliseconds(stack_wait_ms>5?5:stack_wait_ms),[]{return stack_wakes.load()!=stack_wait_generation;});}
static void IOSleep(unsigned ms){stack_ms.fetch_add(ms);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static void thread_deallocate(thread_t){}static void thread_terminate(thread_t t){CHECK(t==current_thread());}
static int kernel_thread_start(void (*entry)(void *,wait_result_t),void *arg,thread_t *out){
    std::lock_guard<std::mutex> guard(stack_alloc_lock);uintptr_t id=0x10+stack_threads.size();*out=(void *)id;
    stack_threads.emplace_back([=]{stack_identity=(void *)id;entry(arg,0);});return 0;
}
struct ShengAdspChannel {std::string name;EldenAdspEpochClient client{};void *context=nullptr;EldenAdspChannelStatus status{};};
static ShengAdspChannel stack_rpc_channel,stack_qrtr_channel;static std::mutex stack_channel_lock;
extern "C" void sheng_qrtr_start(void);
extern "C" void elden_qrtr_original_service(void);
static void stack_qmi_send(uint32_t local_port,const uint8_t *data,uint32_t size);
static unsigned stack_endpoint_retired;static std::atomic<unsigned> stack_samples{0};static bool stack_fail_retire;
struct StackRequest {uint32_t port;std::vector<uint8_t> bytes;};
static std::mutex stack_dsp_lock;static std::condition_variable stack_dsp_event;
static std::deque<StackRequest> stack_dsp_requests;static bool stack_dsp_stopping;static std::thread stack_dsp;
extern "C" ShengAdspChannel *elden_adsp_channel_epoch(const char *name,uint32_t size,uint32_t count,int initiator,const EldenAdspEpochClient *client,void *context){
    CHECK(name&&size>=64&&count&&!initiator&&client);auto *c=!std::strcmp(name,"IPCRTR")?&stack_qrtr_channel:&stack_rpc_channel;
    CHECK(c->name.empty());c->name=name;c->client=*client;c->context=context;c->status.epoch=1;c->status.connected=1;
    if(c==&stack_rpc_channel)client->state(context,1,1);return c;
}
extern "C" int elden_adsp_channel_status(ShengAdspChannel *c,EldenAdspChannelStatus *out){std::lock_guard<std::mutex> guard(stack_channel_lock);CHECK(c&&out);*out=c->status;return 0;}
extern "C" int elden_adsp_channel_close(ShengAdspChannel *c,uint32_t epoch){
    std::lock_guard<std::mutex> guard(stack_channel_lock);CHECK(c&&epoch==1);c->status.closing=1;
    c->status.close_sent=c->status.close_acked=c->status.peer_closed=c->status.fenced=1;c->status.connected=0;return 0;
}
extern "C" int sheng_adsp_send_epoch(ShengAdspChannel *c,uint32_t epoch,const void *pointer,uint32_t size){
    CHECK(c&&epoch==1&&pointer&&size<=SHENG_ADSP_MAX_MESSAGE);
    const auto *data=(const uint8_t *)pointer;
    if(c==&stack_qrtr_channel){
        CHECK(size>=32);uint32_t kind=0,port=0;std::memcpy(&kind,data+4,4);std::memcpy(&port,data+12,4);
        if(kind==QCOM_QRTR_DATA){uint32_t payload=0;std::memcpy(&payload,data+20,4);CHECK(payload<=size-32);
            std::lock_guard<std::mutex> guard(stack_dsp_lock);stack_dsp_requests.push_back({port,std::vector<uint8_t>(data+32,data+32+payload)});stack_dsp_event.notify_all();}
        return 0;
    }
    CHECK(size==40&&model->arena);uint32_t handle=0,scalar=0;std::memcpy(&handle,data+16,4);std::memcpy(&scalar,data+20,4);
    // Actual listener next2 blocks until remote request or explicit stop. It
    // must be interrupted by actual observer/binding wake after SSC shutdown.
    if(handle==3&&((scalar>>24)&31)==4)return 0;
    if(!handle&&((scalar>>24)&31)==0){uint32_t value=0x77;std::memcpy(model->arena+0x800,&value,4);value=0;std::memcpy(model->arena+0x804,&value,4);}
    uint8_t response[16]={0};std::memcpy(response,data+8,8);c->client.receive(c->context,1,response,sizeof response);return 0;
}
static int stack_cookie_reserve(void *,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(owner==7&&epoch==9);*out=(++model->cookie<<4)|2;return 0;}
static int stack_cookie_complete(void *,uint64_t owner,uint64_t epoch,uint64_t cookie){CHECK(owner==7&&epoch==9&&cookie==(model->cookie<<4|2));model->completed++;return 0;}
static int stack_rpc_retire(void *,uint64_t owner,uint64_t epoch,uint32_t channel_epoch,int pending){
    CHECK(owner==7&&epoch==9&&channel_epoch==1&&(pending==0||pending==1));EldenMotionNativeStatus motion{};
    CHECK(!elden_motion_native_ssc_status(&motion)&&motion.terminal&&!motion.error);
    if(stack_fail_retire)return -16;stack_endpoint_retired++;model->bus_withdraws++;return 0;
}
static std::mutex stack_registry_mutex;
static void stack_qr_lock(void *){stack_registry_mutex.lock();}static void stack_qr_unlock(void *){stack_registry_mutex.unlock();}
static int stack_qr_retire(void *,const EldenQrtrPortStatus *s){CHECK(s&&s->closing&&!s->inflight&&!s->callbacks&&s->del_client_queued);return 0;}
static EldenMotionNativeProducer stack_hid_producer{};static std::mutex stack_hid_lock;
extern "C" int elden_motion_native_hid_start(const EldenMotionNativeProducer *p){CHECK(p&&p->owner==7&&p->epoch==9);std::lock_guard<std::mutex> lock(stack_hid_lock);stack_hid_producer=*p;return 0;}
extern "C" int elden_motion_native_hid_bind(uint64_t owner,uint64_t epoch){CHECK(owner==7&&epoch==9);std::lock_guard<std::mutex> lock(stack_hid_lock);CHECK(!stack_hid_producer.demand(stack_hid_producer.context,7,9,1,25));return 1;}
extern "C" int elden_motion_native_hid_packet(const EldenMotionSample *s){CHECK(s&&s->kind==1&&s->unit==EldenMotionG&&s->q16[0]==6682);stack_samples.fetch_add(1);return 0;}
extern "C" int elden_motion_native_hid_stop(uint64_t owner,uint64_t epoch){CHECK(owner==7&&epoch==9);return stack_hid_producer.demand?stack_hid_producer.demand(stack_hid_producer.context,owner,epoch,1,0):0;}
#include "AdspEndpointMethods.inc"
#include "NativeSscMethods.inc"
#include "SensorRuntimeMethods.inc"
static int stack_qualify(void *,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *,EldenMotionSpec *out){CHECK(owner==7&&epoch==9);out->unit=kind==1?EldenMotionMetersPerSecond2:EldenMotionRadiansPerSecond;out->max_abs_q16=16*65536;out->mount[0]=1;out->mount[1]=2;out->mount[2]=3;return 0;}
static int stack_time(void *,uint64_t owner,uint64_t epoch,uint64_t stamp,uint64_t *out){CHECK(owner==7&&epoch==9);*out=stamp;return 0;}
struct StackBytes {std::vector<uint8_t> b;};
static void stack_fixed(StackBytes &b,uint64_t v,unsigned count){for(unsigned i=0;i<count;i++)b.b.push_back((uint8_t)(v>>(8*i)));}
static void stack_var(StackBytes &b,uint64_t v){do{b.b.push_back((uint8_t)((v&127)|(v>127?128:0)));v>>=7;}while(v);}
static void stack_field(StackBytes &b,unsigned f,unsigned wire,uint64_t value){stack_var(b,(uint64_t)f<<3|wire);if(!wire)stack_var(b,value);else stack_fixed(b,value,wire==1?8:4);}
static void stack_nested(StackBytes &b,unsigned f,const StackBytes &value){stack_var(b,(uint64_t)f<<3|2);stack_var(b,value.b.size());b.b.insert(b.b.end(),value.b.begin(),value.b.end());}
static void stack_tlv(StackBytes &b,unsigned tag,const StackBytes &value){b.b.push_back((uint8_t)tag);stack_fixed(b,value.b.size(),2);b.b.insert(b.b.end(),value.b.begin(),value.b.end());}
static StackBytes stack_sid(EldenMotionSuid s){StackBytes b;stack_field(b,1,1,s.low);stack_field(b,2,1,s.high);return b;}
static EldenMotionSuid stack_suids[2]={{0x1234,0x9876},{0x5678,0x8765}};
static StackBytes stack_reply(uint16_t txn,uint64_t client){StackBytes body,v;stack_fixed(v,0,4);stack_tlv(body,2,v);v={};stack_fixed(v,client,8);stack_tlv(body,0x10,v);v={};stack_fixed(v,0,4);stack_tlv(body,0x11,v);
    StackBytes out;out.b.push_back(2);stack_fixed(out,txn,2);stack_fixed(out,0x20,2);stack_fixed(out,body.b.size(),2);out.b.insert(out.b.end(),body.b.begin(),body.b.end());return out;}
static StackBytes stack_report(EldenMotionSuid suid,unsigned message,const StackBytes &payload,uint64_t timestamp,uint64_t client){
    StackBytes event,envelope;stack_field(event,1,5,message);stack_field(event,2,1,timestamp);stack_nested(event,3,payload);stack_nested(envelope,1,stack_sid(suid));stack_nested(envelope,2,event);
    StackBytes body,v;stack_fixed(v,client,8);stack_tlv(body,1,v);v={};stack_fixed(v,envelope.b.size(),2);v.b.insert(v.b.end(),envelope.b.begin(),envelope.b.end());stack_tlv(body,2,v);
    StackBytes out;out.b.push_back(4);stack_fixed(out,0,2);stack_fixed(out,0x21,2);stack_fixed(out,body.b.size(),2);out.b.insert(out.b.end(),body.b.begin(),body.b.end());return out;
}
static StackBytes stack_text(const char *text){StackBytes str,datum,value;str.b.assign(text,text+std::strlen(text));stack_nested(datum,2,str);stack_nested(value,1,datum);return value;}
static StackBytes stack_scalar(unsigned kind,uint64_t v){StackBytes datum,value;stack_field(datum,kind,kind==3?5:kind==4?1:0,v);stack_nested(value,1,datum);return value;}
static void stack_attribute(StackBytes &out,unsigned id,const StackBytes &value){StackBytes attr;stack_field(attr,1,0,id);stack_nested(attr,2,value);stack_nested(out,1,attr);}
static StackBytes stack_attributes(unsigned kind){
    StackBytes p;stack_attribute(p,0,stack_text("MODEL"));stack_attribute(p,1,stack_text("PEER"));stack_attribute(p,2,stack_text(kind==1?"accel":"gyro"));
    stack_attribute(p,3,stack_scalar(5,1));stack_attribute(p,5,stack_text(kind==1?"sns_accel.proto":"sns_gyro.proto"));
    StackBytes rates;for(uint32_t rate:{0x41c80000u,0x42480000u}){StackBytes datum;stack_field(datum,3,5,rate);stack_nested(rates,1,datum);}stack_attribute(p,6,rates);
    StackBytes range,pair,sub;for(uint32_t f:{0xc1800000u,0x41800000u}){StackBytes datum;stack_field(datum,3,5,f);stack_nested(pair,1,datum);}stack_nested(sub,1,pair);stack_nested(range,1,sub);
    stack_attribute(p,11,range);stack_attribute(p,16,stack_scalar(4,0));stack_attribute(p,21,stack_scalar(5,1));return p;
}
static void stack_deliver_qmi(uint32_t port,const StackBytes &bytes){uint8_t packet[1200];uint32_t size=qcom_qrtr_frame(packet,sizeof packet,QCOM_QRTR_DATA,5,0x9000,1,port,bytes.b.data(),(uint32_t)bytes.b.size());CHECK(size);stack_qrtr_channel.client.receive(nullptr,1,packet,size);}
static void stack_qmi_send(uint32_t local_port,const uint8_t *data,uint32_t size){
    CHECK(size>12&&data[0]==0&&local_port>=0x4000&&local_port<0x4003);unsigned conn=local_port-0x4000;uint16_t txn=data[1]|(uint16_t)data[2]<<8;uint32_t message=0;unsigned found=0;
    for(uint32_t at=12;at+5<=size;at++)if(data[at]==0x15){message=data[at+1]|(uint32_t)data[at+2]<<8|(uint32_t)data[at+3]<<16|(uint32_t)data[at+4]<<24;found++;}
    CHECK(found==1);stack_deliver_qmi(local_port,stack_reply(txn,100+conn));
    if(message==512){bool gyro=false;for(uint32_t i=0;i+4<=size;i++)if(!std::memcmp(data+i,"gyro",4))gyro=true;unsigned kind=gyro?2:1;
        StackBytes payload,type;const char *name=gyro?"gyro":"accel";type.b.assign(name,name+std::strlen(name));stack_nested(payload,1,type);stack_nested(payload,2,stack_sid(stack_suids[kind-1]));
        stack_deliver_qmi(local_port,stack_report({UINT64_C(0xabababababababab),UINT64_C(0xabababababababab)},768,payload,100,100));
    }else if(message==1){CHECK(conn>0);stack_deliver_qmi(local_port,stack_report(stack_suids[conn-1],128,stack_attributes(conn),110,100+conn));}
    else if(message==513){StackBytes payload;stack_field(payload,1,5,0x3f800000);stack_field(payload,1,5,0);stack_field(payload,1,5,0);stack_field(payload,2,0,3);
        stack_deliver_qmi(local_port,stack_report(stack_suids[conn-1],1025,payload,mach_absolute_time()-192,100+conn));}
}
static int stack_runtime_test(int argc,char **argv){
    CHECK(argc==3);stack_fail_retire=std::strcmp(argv[2],"retire-fail")==0;Peer p;model=&p;pack(p);stack_ms=1000;
    EldenSensorRuntimeConfig cfg{};cfg.memory={0x20000000,0x800000,4,4242};cfg.memory_owner={&p,7,9,bus_gate,route_reserve,route_install,route_observe,route_withdraw};
    cfg.adsp={&p,7,9,bus_gate,stack_cookie_reserve,stack_cookie_complete,stack_rpc_retire,bus_trace};cfg.rpc_intent_size=1024;cfg.rpc_intent_count=4;
    cfg.qrtr={&p,7,9,2048,8,stack_qr_lock,stack_qr_unlock,bus_gate,stack_qr_retire};cfg.seal=p.seal;cfg.pack={&authority_token,7,9,authority};
    cfg.board={&p,bus_gate,stack_qualify,stack_time,100000,0,{0,0}};CHECK(!elden_sensor_runtime_prepare(&cfg));
    CHECK(memory_reserves==1&&memory_installs==1);sheng_qrtr_start();stack_qrtr_channel.client.state(stack_qrtr_channel.context,1,1);
    uint8_t payload[20]={0},packet[80];
    uint32_t command=QCOM_QRTR_HELLO;std::memcpy(payload,&command,4);uint32_t n=qcom_qrtr_frame(packet,sizeof packet,command,5,QCOM_QRTR_PORT_CTRL,1,QCOM_QRTR_PORT_CTRL,payload,20);
    stack_qrtr_channel.client.receive(nullptr,1,packet,n);uint32_t fields[5]={QCOM_QRTR_NEW_SERVER,400,0x100,5,0x9000};std::memcpy(payload,fields,20);
    n=qcom_qrtr_frame(packet,sizeof packet,QCOM_QRTR_NEW_SERVER,5,QCOM_QRTR_PORT_CTRL,1,QCOM_QRTR_PORT_CTRL,payload,20);stack_qrtr_channel.client.receive(nullptr,1,packet,n);
    stack_dsp=std::thread([]{for(;;){StackRequest request;{std::unique_lock<std::mutex> lock(stack_dsp_lock);stack_dsp_event.wait(lock,[]{return stack_dsp_stopping||!stack_dsp_requests.empty();});
        if(stack_dsp_stopping&&stack_dsp_requests.empty())break;request=std::move(stack_dsp_requests.front());stack_dsp_requests.pop_front();}
        stack_qmi_send(request.port,request.bytes.data(),(uint32_t)request.bytes.size());}});
    CHECK(!elden_sensor_runtime_start(7,9));
    for(unsigned i=0;i<1000&&!stack_samples.load();i++){stack_ms.fetch_add(1);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(stack_samples.load()==1&&memory_reserves==1&&memory_installs==1&&p.opens==1);
    int stopped=elden_sensor_runtime_stop(7,9);CHECK(stopped==(stack_fail_retire?-16:0));
    for(auto &thread:stack_threads)if(thread.joinable())thread.join();EldenSensorRuntimeStatus status{};
    CHECK(!elden_sensor_runtime_status(&status)&&status.terminal&&status.motion_stopped&&status.motion.samples==1);
    CHECK(stack_endpoint_retired==(stack_fail_retire?0u:1u)&&memory_withdraws==(stack_fail_retire?0u:1u));
    CHECK(memory_descriptors.size()==2&&memory_aliases.size()==1);
    {std::lock_guard<std::mutex> lock(stack_dsp_lock);stack_dsp_stopping=true;}stack_dsp_event.notify_all();stack_dsp.join();
    for(auto *l:stack_locks)delete l;memory_test_reset();for(auto *l:locks)delete l;for(void *a:allocated)std::free(a);
    std::printf("PASS composed sensor runtime %s, %u checks; actual allocator/cache/SHA/RPC/endpoint/QRTR/SSC; OS/DSP/transport/physical/HID peers modeled\n",argv[2],checks.load());return 0;
}
