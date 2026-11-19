/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual TouchCoordinator/Flow/Protocol/pen pause with explicit XNU scheduler,
 * DMA/GIC/power/HID peers. No native power provider or tablet operation. */
#include "TouchPower.h"
#include "Flow.h"
#include "NativeIrq.h"
#include "../EldenPen/Delivery.h"
#include "../EldenNvt36536/Protocol.h"
#include "../EldenNvt36536/PointReport.h"
#include "../EldenNvt36536/RunControl.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks,scenarios,waits,blocks,clears,signals,pen_sends,power_requests,delay_count;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
extern "C" {
void *elden_sleep_protocol_create(EldenNvtDevice *,uint8_t *,uint8_t *,const uint8_t *,size_t);
void elden_sleep_protocol_gesture(void *,unsigned);unsigned elden_sleep_protocol_calls(void *);
void elden_sleep_protocol_fault(void *);void elden_sleep_protocol_destroy(void *);
}
using event_t=void *;enum {THREAD_UNINT=1,THREAD_AWAKENED=2,THREAD_CONTINUE_NULL=0};
static uint64_t clock_us=1;static unsigned lease=1;static int native_owner_data_status,elden_init_exec_accepted=1;
static EldenTouchRunControl run_control={EldenTouchRunning};static bool irq_context,power_on=true,deny_gate;
static int hid_error,pen_error,irq_error;static uint32_t pending,armed,boot_mode=1;static event_t last_wait;
static void (*sleep_hook)();static void (*wait_hook)();static bool sleep_reenter;
static std::vector<void *> mem;
struct IOLock{std::mutex mutex;};static std::vector<IOLock *> locks;
static IOLock *IOLockAlloc(){auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->mutex.lock();}static void IOLockUnlock(IOLock *p){p->mutex.unlock();}
static bool ml_at_interrupt_context(){return irq_context;}
static int elden_recovery_armed(){return 1;}
static void PE_parse_boot_argn(const char *n,void *out,size_t size){CHECK(!strcmp(n,"elden-touch-wake")&&size==4);*(unsigned *)out=boot_mode;}
static void thread_wakeup(event_t event){CHECK(event);signals++;}
static void *active_thread=(void *)2;
static void *current_thread(){return active_thread;}
static void assert_wait(event_t event,unsigned mode){CHECK(mode==THREAD_UNINT&&event);waits++;last_wait=event;if(wait_hook)wait_hook();}
static void clear_wait(void *thread,unsigned reason){CHECK(thread==active_thread&&reason==THREAD_AWAKENED);clears++;}
static void thread_block(unsigned continuation){CHECK(continuation==THREAD_CONTINUE_NULL&&last_wait);blocks++;}
static void IOSleep(unsigned ms){clock_us+=ms*1000;if(sleep_hook&&!sleep_reenter){sleep_reenter=true;sleep_hook();sleep_reenter=false;}}
static EldenPenDelivery pen;static EldenPenFrame accepted;
static int pen_send(void *,const EldenPenFrame *f){if(pen_error)return pen_error;pen_sends++;accepted=*f;return 0;}
static int elden_pen_native_pause(){return elden_pen_delivery_pause(&pen,clock_us,pen_send,nullptr);}
static int elden_pen_native_resume(){return elden_pen_delivery_resume(&pen,clock_us);}
static int elden_touch_hid_publish(const elden_nvt_frame *frame){CHECK(frame&&!frame->count);return hid_error;}
enum {EldenTouchSe=0,SE_GENI_STATUS=1,SE_GENI_TX_FIFO_STATUS=2,SE_GENI_RX_FIFO_STATUS=3};
struct Region{unsigned state=0;unsigned read(unsigned reg){CHECK(reg>=1&&reg<=3);return state;}};
struct Context {
    bool dma_mode=false;struct {bool quarantined=false;} platform;
    EldenNvtDevice device={};struct {bool active=false,quarantined=false;} dma_transfer;
    struct {bool poisoned=false,attached=true;uint64_t epoch=7;} route;
    Region region[1];elden_nvt_frame previous={};unsigned errors=0,hidWaits=0;
    uint8_t tx[ELDEN_NVT_FW_CHUNK+1],rx[110];void *protocol=nullptr;
#include "TouchFields.inc"
};
static Context *test_context;static int dma_idle(Context *c){return c->dma_transfer.active?-16:0;}
static int elden_touch_route_observe(decltype(Context::route) *,uint64_t epoch){return epoch==7?0:-16;}
static void delay(void *,unsigned us){CHECK(us==50000);clock_us+=us;delay_count++;}
static int (*irq_gate)(void *,uint64_t,uint64_t,int);static void *irq_gate_context;
extern "C" int elden_wake_native_irq_acquire(uint64_t owner,uint64_t epoch,void *c,int (*g)(void *,uint64_t,uint64_t,int)){
    CHECK(owner==11&&epoch==7&&c&&g);irq_gate=g;irq_gate_context=c;return irq_error?irq_error:g(c,owner,epoch,0);
}
extern "C" int elden_wake_native_irq_arm(uint64_t owner,uint64_t epoch){CHECK(owner==11&&epoch==7);if(irq_error)return irq_error;armed=1;return 0;}
extern "C" int elden_wake_native_irq_disarm(uint64_t owner,uint64_t epoch){CHECK(owner==11&&epoch==7);armed=0;return irq_error;}
extern "C" int elden_wake_native_irq_stop(uint64_t owner,uint64_t epoch){CHECK(owner==11&&epoch==7);armed=0;return irq_error;}
extern "C" int elden_wake_native_irq_take(uint64_t owner,uint64_t epoch,uint32_t *events){CHECK(owner==11&&epoch==7&&events);
    if(irq_error)return irq_error;*events=pending;pending=0;return 0;}
extern "C" const void *elden_wake_native_irq_event(){return irq_gate?&pending:nullptr;}
extern "C" int elden_wake_native_irq_pending(uint64_t owner,uint64_t epoch){CHECK(owner==11&&epoch==7);
    if(irq_error)return irq_error;return pending?1:0;}
#include "TouchCoordinator.inc"
static int power_admit(void *,uint64_t owner,uint64_t epoch,unsigned phase){CHECK(owner==11&&epoch==7&&phase<=EldenWakeAdmissionCleanup);
    if(deny_gate)return -13;return phase==EldenWakeAdmissionResume&&!power_on?-19:0;}
static int power_wake(void *,uint64_t owner,uint64_t epoch,unsigned gesture){CHECK(owner==11&&epoch==7&&gesture==15);
    CHECK(elden_touch_wake_request(owner,epoch,0,1)==-16); // actual synchronous same-thread call refused
    power_requests++;return 0;}
static void worker_round(){void *old=active_thread;active_thread=(void *)1;(void)wake_round(test_context);active_thread=old;} // API caller checks result
static void pending_during_wait(){wait_hook=nullptr;pending=1;}
static void stop_during_wait(){wait_hook=nullptr;run_control.state=EldenTouchStopping;}
static void busy_during_wait(){sleep_hook=worker_round;CHECK(elden_touch_wake_request(11,7,1,1)==-16);worker_round();}
static void destroy(Context *c){elden_sleep_protocol_destroy(c->protocol);delete c;for(auto *l:locks)delete l;locks.clear();}
static Context *setup(const uint8_t *firmware,size_t n,unsigned mode=1,bool install=true){
    scenarios++;wake_touch_active=nullptr;wake_power_installed=0;installed_wake_power={};lease=1;run_control.state=EldenTouchRunning;
    pending=armed=0;clock_us=1;irq_context=deny_gate=false;power_on=true;hid_error=pen_error=irq_error=0;irq_gate=nullptr;irq_gate_context=nullptr;
    waits=blocks=clears=signals=pen_sends=power_requests=delay_count=0;wait_hook=nullptr;sleep_hook=worker_round;sleep_reenter=false;boot_mode=mode;
    auto *c=new Context;c->protocol=elden_sleep_protocol_create(&c->device,c->tx,c->rx,firmware,n);test_context=c;
    CHECK(!elden_pen_delivery_init(&pen,100000));EldenPenFrame down={};down.kind=EldenPenCoordinate;down.tip=down.in_range=1;down.pressure=50;
    CHECK(!elden_pen_delivery_feed(&pen,&down,clock_us,pen_send,nullptr));c->previous.count=1;
    if(install){EldenTouchWakePower p={nullptr,11,7,power_admit,power_wake};CHECK(!elden_touch_wake_install_power(&p));CHECK(elden_touch_wake_install_power(&p)==-16);}
    EldenNvtImage image;CHECK(!elden_nvt_image(&image,firmware,n));active_thread=(void *)1;
    CHECK(wake_bind(c,&image)==(mode==1&&!install?-19:0));active_thread=(void *)2;return c;
}
int main(int argc,char **argv){
    CHECK(argc==2);FILE *f=std::fopen(argv[1],"rb");CHECK(f);CHECK(!std::fseek(f,0,SEEK_END));long bytes=std::ftell(f);CHECK(bytes>0);std::rewind(f);
    auto *fw=(uint8_t *)std::malloc((size_t)bytes);CHECK(fw&&std::fread(fw,1,(size_t)bytes,f)==(size_t)bytes);CHECK(!std::fclose(f));
    auto *c=setup(fw,(size_t)bytes);sleep_hook=busy_during_wait;
    CHECK(!elden_touch_wake_request(11,7,1,1)&&c->wake_flow.state==EldenWakeFlowGesture&&c->wake_paused&&pen.paused&&!accepted.tip&&!c->previous.count);
    CHECK(delay_count==1&&armed&&blocks==1);unsigned transfers=elden_sleep_protocol_calls(c->protocol);
    CHECK(wake_round(c)==1&&elden_sleep_protocol_calls(c->protocol)==transfers); // sleeps with no periodic SPI
    pending=1;elden_sleep_protocol_gesture(c->protocol,15);
    active_thread=(void *)1;int gesture_result=wake_round(c);active_thread=(void *)2;
    CHECK(gesture_result==1&&power_requests==1&&c->wake_flow.state==EldenWakeFlowRequested&&!armed&&c->wake_paused);
    pending=1;unsigned before_block=blocks,before_clear=clears;
    CHECK(wake_round(c)==1&&blocks==before_block+1&&clears==before_clear&&last_wait==&c->wake_sequence); // no stale-IRQ spin after wake request
    power_on=false;CHECK(elden_touch_wake_request(11,7,0,1)==-19&&c->wake_flow.state==EldenWakeFlowRequested&&!c->wake_closed);
    power_on=true;CHECK(!elden_touch_wake_request(11,7,0,1)&&c->wake_flow.state==EldenWakeFlowNormal&&!c->wake_paused&&!pen.paused);
    CHECK(elden_sleep_protocol_calls(c->protocol)>transfers);EldenTouchWakeStatus status;CHECK(!elden_touch_wake_status(11,7,&status)&&status.gestures==1&&!status.paused);
    // Same wake thread owns irreversible terminal shutdown as well.
    run_control.state=EldenTouchStopping;CHECK(!wake_stop(c)&&c->wake_flow.state==EldenWakeFlowStopped);destroy(c);
    c=setup(fw,(size_t)bytes);pen_error=-99;CHECK(elden_touch_wake_request(11,7,1,1)==-99&&c->wake_closed&&c->wake_paused&&pen.release_pending);
    CHECK(elden_touch_wake_request(11,7,0,1)==-19);destroy(c);
    c=setup(fw,(size_t)bytes);c->dma_mode=true;c->dma_transfer.active=true;transfers=elden_sleep_protocol_calls(c->protocol);
    CHECK(elden_touch_wake_request(11,7,1,1)==-16&&!c->wake_paused&&elden_sleep_protocol_calls(c->protocol)==transfers);destroy(c);
    c=setup(fw,(size_t)bytes);CHECK(!elden_touch_wake_request(11,7,1,1));wait_hook=pending_during_wait;
    CHECK(wake_round(c)==1&&clears==1&&pending==1);destroy(c);
    c=setup(fw,(size_t)bytes);CHECK(!elden_touch_wake_request(11,7,1,1));wait_hook=stop_during_wait;
    CHECK(wake_round(c)==1&&clears==1&&run_control.state==EldenTouchStopping);CHECK(!wake_stop(c));destroy(c);
    c=setup(fw,(size_t)bytes);sleep_hook=nullptr;CHECK(elden_touch_wake_request(11,7,1,1)==-110&&c->wake_closed&&!c->wake_paused);
    CHECK(wake_round(c)==-110);destroy(c);
    c=setup(fw,(size_t)bytes);CHECK(!elden_touch_wake_request(11,7,1,1));irq_error=-13;
    CHECK(wake_round(c)==-13&&c->wake_closed);destroy(c);
    c=setup(fw,(size_t)bytes);CHECK(!elden_touch_wake_request(11,7,1,0)&&c->wake_flow.state==EldenWakeFlowDeep&&!irq_gate);
    pending=1;before_block=blocks;CHECK(wake_round(c)==1&&blocks==before_block+1&&last_wait==&c->wake_sequence);destroy(c);
    c=setup(fw,(size_t)bytes,0,false);transfers=elden_sleep_protocol_calls(c->protocol);
    CHECK(!c->wake_enabled&&!wake_round(c)&&!wake_stop(c)&&!waits&&!signals&&elden_sleep_protocol_calls(c->protocol)==transfers);
    CHECK(elden_touch_wake_request(11,7,1,1)==-22);destroy(c);
    c=setup(fw,(size_t)bytes,1,false);CHECK(!c->wake_enabled&&!wake_touch_active&&!waits);destroy(c);
    c=setup(fw,(size_t)bytes);CHECK(!elden_touch_wake_request(11,7,1,1));transfers=elden_sleep_protocol_calls(c->protocol);
    elden_sleep_protocol_fault(c->protocol);CHECK(elden_touch_wake_request(11,7,0,1)==-110&&c->wake_closed&&c->wake_paused&&c->device.poisoned&&!c->device.ready);
    CHECK(wake_round(c)==-110&&elden_sleep_protocol_calls(c->protocol)==transfers+1);destroy(c);
    c=setup(fw,(size_t)bytes);hid_error=-71;transfers=elden_sleep_protocol_calls(c->protocol);
    CHECK(elden_touch_wake_request(11,7,1,1)==-71&&c->wake_closed&&c->previous.count==1&&elden_sleep_protocol_calls(c->protocol)==transfers);
    CHECK(!armed&&accepted.tip);destroy(c);
    std::free(fw);std::printf("PASS: %u actual touch coordinator scenarios/%u checks; serial pause-release-SPI/gesture/request/real-power-gate/firmware restore, event sleep and retained faults; OS/DMA/IRQ/power/HID peers modeled\n",scenarios,checks);return 0;
}
