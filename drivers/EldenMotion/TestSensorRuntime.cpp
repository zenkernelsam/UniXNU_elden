/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual runtime methods; ADSP/memory/pack/SSC and XNU scheduler are modeled
 * for failure/timeout/ownership injection. Real lower layers run separately. */
#include "SensorRuntime.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};
#define CHECK(v) do{checks.fetch_add(1);if(!(v)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#v);std::exit(1);}}while(0)
using IOSimpleLock=std::mutex;using thread_t=void *;using event_t=void *;using boolean_t=int;using wait_result_t=int;
enum {KERN_SUCCESS=0,THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1};
static constexpr auto THREAD_CONTINUE_NULL=nullptr;
static std::vector<void *> allocations;static std::vector<IOSimpleLock *> locks;static std::thread worker;
static std::atomic<uint64_t> clock_ms{1000};static std::atomic<unsigned> wakes{0};static thread_local thread_t thread_id=(void *)2;
static bool irq=false,fail_thread=false;static void *wait_event;static bool inject_wait=false;
static IOSimpleLock *IOSimpleLockAlloc(){auto *p=new IOSimpleLock;locks.push_back(p);return p;}
static void IOSimpleLockLock(IOSimpleLock *p){p->lock();}static void IOSimpleLockUnlock(IOSimpleLock *p){p->unlock();}
static void *IOMallocData(size_t size){void *p=std::calloc(1,size);allocations.push_back(p);return p;}
static void IOFreeData(void *p,size_t){for(auto &v:allocations)if(v==p)v=nullptr;std::free(p);}
static thread_t current_thread(){return thread_id;}static boolean_t ml_at_interrupt_context(){return irq;}
static uint64_t mach_absolute_time(){return clock_ms.load()*19200;}
static void absolutetime_to_nanoseconds(uint64_t time,uint64_t *out){*out=time*625/12;}
static void thread_wakeup(event_t){wakes.fetch_add(1);}
static void assert_wait_timeout(event_t event,int mode,uint32_t ms,int scale){CHECK(mode==1&&ms&&scale==1);wait_event=event;if(inject_wait){inject_wait=false;__atomic_store_n((uint32_t *)event,1u,__ATOMIC_RELEASE);}}
static void clear_wait(thread_t t,int result){CHECK(t==current_thread()&&result==2);}
static void thread_block(decltype(nullptr)){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static void IOSleep(unsigned ms){CHECK(ms==10);clock_ms.fetch_add(ms);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static void thread_terminate(thread_t t){CHECK(t==current_thread());}
static void thread_deallocate(thread_t){}
static int kernel_thread_start(void (*f)(void *,wait_result_t),void *p,thread_t *out){if(fail_thread)return 1;CHECK(!worker.joinable());*out=(void *)1;worker=std::thread([=]{thread_id=(void *)1;f(p,0);});return 0;}
struct FakeMemory {unsigned opens=0,stops=0;};static FakeMemory memory;
struct FakeEndpoint {EldenSensorEndpoint ops{};unsigned opens=0,closes=0;};static FakeEndpoint endpoint;
struct FakeBinding {unsigned opens=0,finishes=0;std::atomic<unsigned> steps{0};};static FakeBinding binding;
static std::atomic<unsigned> sampling_starts{0},sampling_stop{0},sampling_status{0},rpc_stop{0};
static std::atomic<bool> motion_terminal{false},hold_rpc{false},rpc_waiting{false},rpc_release{false};
static std::atomic<bool> hold_motion_start{false},motion_start_entered{false},motion_start_release{false};
static int memory_error=0,endpoint_error=0,binding_error=0,attach_error=0,qrtr_error=0,retire_error=0,pack_error=0,motion_error=0;
static bool require_listener_cleanup=false;
static std::atomic<unsigned> initial_delay{0};static EldenSensorRpcStatus rpc_status{};static EldenSensorPackStatus pack_status{};
static void *observer_context;static void (*observer)(void *,const EldenMotionNativeStatus *);
static EldenMotionBoard installed_board{};
extern "C" int elden_sensor_memory_native_open(const EldenSensorMemoryConfig *c,const EldenSensorMemoryOwner *o,EldenSensorNativeMemory **out){CHECK(c&&o&&o->owner==7&&o->epoch==9);memory.opens++;*out=(EldenSensorNativeMemory *)&memory;return memory_error;}
extern "C" int elden_sensor_memory_native_admit(EldenSensorNativeMemory *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
extern "C" int elden_sensor_memory_native_stop(EldenSensorNativeMemory *p,uint64_t owner,uint64_t epoch){CHECK(p==(EldenSensorNativeMemory *)&memory&&owner==7&&epoch==9);memory.stops++;return retire_error;}
extern "C" int elden_qrtr_ports_prepare(const EldenQrtrPortOps *){return qrtr_error;}
static void endpoint_wake(void *){rpc_release.store(true);wakes.fetch_add(1);}
extern "C" int elden_sensor_adsp_endpoint_open(const EldenSensorAdspAuthority *a,uint32_t size,uint32_t count,EldenSensorAdspEndpoint **out,EldenSensorEndpoint *ops){
    CHECK(a&&a->owner==7&&size==1024&&count==4);endpoint.opens++;*out=(EldenSensorAdspEndpoint *)&endpoint;
    ops->context=&endpoint;ops->owner=7;ops->epoch=9;ops->wake=endpoint_wake;return endpoint_error;
}
extern "C" int elden_sensor_adsp_endpoint_attach(EldenSensorAdspEndpoint *p,EldenSensorMemoryBinding *b,uint64_t owner,uint64_t epoch){CHECK(p==(EldenSensorAdspEndpoint *)&endpoint&&b==(EldenSensorMemoryBinding *)&binding&&owner==7&&epoch==9);return attach_error;}
extern "C" int elden_sensor_adsp_endpoint_close(EldenSensorAdspEndpoint *p,uint64_t owner,uint64_t epoch){CHECK(p==(EldenSensorAdspEndpoint *)&endpoint&&owner==7&&epoch==9);endpoint.closes++;return retire_error;}
extern "C" int elden_sensor_memory_binding_open_prepared(EldenSensorNativeMemory *m,const EldenSensorEndpoint *,const EldenSensorPackSeal *,const EldenSensorPackAuthority *,EldenSensorMemoryBinding **out){
    CHECK(m==(EldenSensorNativeMemory *)&memory&&memory.opens==1);binding.opens++;*out=(EldenSensorMemoryBinding *)&binding;return binding_error;
}
extern "C" int elden_sensor_memory_binding_request_stop(EldenSensorMemoryBinding *b,uint64_t owner,uint64_t epoch){CHECK(b==(EldenSensorMemoryBinding *)&binding&&owner==7&&epoch==9);rpc_stop.fetch_add(1);endpoint_wake(nullptr);return 0;}
extern "C" int elden_sensor_memory_binding_step(EldenSensorMemoryBinding *b){
    CHECK(b==(EldenSensorMemoryBinding *)&binding);binding.steps++;
    unsigned delay=initial_delay.load();if(delay){initial_delay.fetch_sub(1);rpc_status.stage=2;pack_status.state=1;std::this_thread::sleep_for(std::chrono::milliseconds(1));return 1;}
    rpc_status.stage=6;pack_status.state=2;
    if(require_listener_cleanup&&sampling_stop.load()&&!motion_terminal.load()){
        CHECK(rpc_stop.load()==0);motion_terminal=true;
        EldenMotionNativeStatus motion{};motion.owner=7;motion.epoch=9;motion.terminal=1;observer(observer_context,&motion);
    }
    if(hold_rpc.load()&&sampling_starts.load()){rpc_waiting=true;while(!rpc_release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));return 1;
}
extern "C" int elden_sensor_memory_binding_status(EldenSensorMemoryBinding *b,EldenSensorRpcStatus *rpc,EldenSensorPackStatus *pack){CHECK(b==(EldenSensorMemoryBinding *)&binding);*rpc=rpc_status;*pack=pack_status;return pack_error;}
extern "C" int elden_sensor_memory_binding_finish(EldenSensorMemoryBinding *b,uint64_t owner,uint64_t epoch){CHECK(b==(EldenSensorMemoryBinding *)&binding&&owner==7&&epoch==9);CHECK(!sampling_starts.load()||motion_terminal.load());binding.finishes++;return retire_error;}
extern "C" int elden_motion_qrtr_transport(EldenMotionTransport *out){out->owner=7;out->epoch=9;out->max_rx_payload=1024;return 0;}
extern "C" int elden_motion_native_ssc_start(const EldenMotionTransport *t,const EldenMotionBoard *b){
    CHECK(t&&b&&rpc_status.stage>=6&&pack_status.state==2);installed_board=*b;
    if(hold_motion_start.load()){motion_start_entered=true;while(!motion_start_release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    sampling_starts.fetch_add(1);return 0;}
extern "C" int elden_motion_native_ssc_request_stop(uint64_t owner,uint64_t epoch){
    CHECK(owner==7&&epoch==9);unsigned before=sampling_stop.fetch_add(1);
    if(!before&&!motion_error&&!require_listener_cleanup){motion_terminal=true;EldenMotionNativeStatus s{};s.owner=7;s.epoch=9;s.terminal=1;observer(observer_context,&s);}
    return 0;
}
extern "C" int elden_motion_native_ssc_status(EldenMotionNativeStatus *out){sampling_status.fetch_add(1);out->owner=7;out->epoch=9;out->terminal=motion_terminal.load();out->error=motion_error;return 0;}
extern "C" int elden_motion_native_ssc_observe(void *context,uint64_t owner,uint64_t epoch,void (*callback)(void *,const EldenMotionNativeStatus *)){CHECK(owner==7&&epoch==9&&context&&callback);observer_context=context;observer=callback;return 0;}
extern "C" int elden_motion_native_ssc_recovery_stop(){return 1;}
#include "SensorRuntimeMethods.inc"
static int gate(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int pack_gate(void *,uint64_t owner,uint64_t epoch,const EldenSensorPackIdentity *,int cleanup){return gate(nullptr,owner,epoch,cleanup);}
static int reserve(void *,uint64_t,uint64_t,const EldenSensorMemoryConfig *,EldenSensorMemoryLease *){return 0;}
static int install(void *,uint64_t,uint64_t,const EldenSensorMemoryView *){return 0;}
static int observe(void *,uint64_t,uint64_t,const EldenSensorMemoryView *,int){return 0;}
static int withdraw(void *,uint64_t,uint64_t,const EldenSensorMemoryView *){return 0;}
static int cookie(void *,uint64_t,uint64_t,uint64_t *out){*out=18;return 0;}
static int complete(void *,uint64_t,uint64_t,uint64_t){return 0;}
static int retire(void *,uint64_t,uint64_t,uint32_t,int){return 0;}
static void noop(void *){}
static int qr_retire(void *,const EldenQrtrPortStatus *){return 0;}
static int board_qualify(void *,uint64_t,uint64_t,unsigned,const EldenSscAttrs *,EldenMotionSpec *){return 0;}
static int board_time(void *,uint64_t,uint64_t,uint64_t ticks,uint64_t *out){*out=ticks;return 0;}
static EldenSensorRuntimeConfig config(){EldenSensorRuntimeConfig c{};c.memory={0x20000000,0x800000,4,4242};c.memory_owner={nullptr,7,9,gate,reserve,install,observe,withdraw};
    c.adsp={nullptr,7,9,gate,cookie,complete,retire,nullptr};c.rpc_intent_size=1024;c.rpc_intent_count=4;
    c.qrtr={nullptr,7,9,2048,8,noop,noop,gate,qr_retire};c.pack={nullptr,7,9,pack_gate};c.board={nullptr,gate,board_qualify,board_time,100000,0,{0,0}};
    c.seal.version=1;c.seal.bytes=4096;std::memset(c.seal.sha256,1,32);std::memset(c.seal.namespace_sha256,2,32);std::memset(&c.seal.identity,3,sizeof(c.seal.identity));return c;}
template<class F>static void until(F pred){for(unsigned i=0;i<1000&&!pred();i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(pred());}
static EldenSensorRuntimeStatus status(){EldenSensorRuntimeStatus s{};CHECK(!elden_sensor_runtime_status(&s));return s;}
int main(int argc,char **argv){
    CHECK(argc==2);std::string test=argv[1];auto cfg=config();
    if(test=="reject-config"){cfg.seal.identity.adsp_sha256[0]=0;std::memset(cfg.seal.identity.adsp_sha256,0,32);CHECK(elden_sensor_runtime_prepare(&cfg)==-22&&memory.opens==0);}
    else if(test=="early-stop"){CHECK(!elden_sensor_runtime_prepare(&cfg));CHECK(status().stage==1&&memory.opens==1);CHECK(!elden_sensor_runtime_stop(7,9)&&memory.stops==1&&!endpoint.opens);CHECK(elden_sensor_runtime_start(7,9)==-16);}
    else {
        if(test=="memory-fail")memory_error=-71;if(test=="qrtr-fail")qrtr_error=-16;
        int prepared=elden_sensor_runtime_prepare(&cfg);
        if(test=="memory-fail"||test=="qrtr-fail"){CHECK(prepared<0);CHECK(!elden_sensor_runtime_stop(7,9)&&memory.stops==1);}
        else {
            CHECK(!prepared);if(test=="endpoint-fail")endpoint_error=-19;if(test=="binding-fail")binding_error=-74;if(test=="attach-fail")attach_error=-116;
            if(test=="thread-fail")fail_thread=true;if(test=="pack-not-ready")initial_delay=100;
            if(test=="listener-cleanup")require_listener_cleanup=true;
            if(test=="retirement-fail")retire_error=-16;if(test=="motion-fail")motion_error=-16;
            if(test=="stop-during-wait"||test=="observer-wake")hold_rpc=true;
            if(test=="stop-during-motion-start")hold_motion_start=true;
            int start=elden_sensor_runtime_start(7,9);
            if(test=="thread-fail"){CHECK(start==-12&&!worker.joinable());CHECK(!elden_sensor_runtime_stop(7,9)&&memory.stops==1);}
            else {
                CHECK(!start);CHECK(elden_sensor_runtime_start(7,9)==-16);
                if(test=="endpoint-fail"||test=="binding-fail"||test=="attach-fail")until([]{return status().terminal;});
                else {
                    if(test=="pack-not-ready"){until([]{return binding.steps>2;});CHECK(sampling_starts==0);initial_delay=0;}
                    if(test=="stop-during-motion-start"){
                        until([]{return motion_start_entered.load();});CHECK(!elden_sensor_runtime_request_stop(7,9));CHECK(rpc_stop.load()==0);
                        motion_start_release=true;
                    }
                    until([]{return sampling_starts.load()==1;});
                    CHECK(installed_board.admit(installed_board.context,7,10,0)==-116);
                    if(test=="stop-during-wait"||test=="observer-wake")until([]{return rpc_waiting.load();});
                    if(test=="observer-wake"){EldenMotionNativeStatus s{};s.owner=7;s.epoch=9;s.runtime_error=-84;observer(observer_context,&s);}
                    else CHECK(!elden_sensor_runtime_request_stop(7,9));
                    if(test=="motion-fail"){clock_ms.fetch_add(6000);sensor_signal(sensor_runtime);}
                    until([]{return status().terminal;});
                }
                auto s=status();CHECK(s.stage==(test=="motion-fail"||test=="retirement-fail"?8u:7u));
                if(test=="motion-fail")CHECK(binding.finishes==0&&s.cleanup_error==-110);
                if(test=="retirement-fail")CHECK(s.cleanup_error==-16&&binding.finishes==1);
                if(test=="observer-wake")CHECK(s.runtime_error==-84&&rpc_release.load());
                if(test=="endpoint-fail")CHECK(!sampling_starts.load()&&endpoint.closes==1&&memory.stops==1);
                if(test=="binding-fail"||test=="attach-fail")CHECK(!sampling_starts.load()&&binding.finishes==1);
                if(test=="normal"||test=="stop-during-wait"||test=="pack-not-ready"||test=="listener-cleanup")CHECK(sampling_stop.load()>0&&binding.finishes==1&&!s.cleanup_error);
            }
        }
    }
    if(worker.joinable())worker.join();for(auto *p:locks)delete p;for(auto *p:allocations)std::free(p);
    std::printf("PASS runtime %s: %u checks; actual native coordinator, OS/physical peers modeled\n",test.c_str(),checks.load());return 0;
}
