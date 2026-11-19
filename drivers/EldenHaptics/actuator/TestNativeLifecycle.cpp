/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute NativeActuator.inc itself and the actual broker/live/ring/AW chain.
 * IOKit allocation, scheduler, clock and register/delay providers are peers.
 * Deterministic boundary hooks are not proof of live XNU scheduling/MMIO. */
#include "Broker.h"
#include "Live.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
static unsigned checks,scenarios;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
extern "C" {
void *elden_actuator_peer_create(EldenAwChip *,const uint8_t *,size_t);
uint64_t elden_actuator_peer_now(void *);
void elden_actuator_peer_advance(void *,unsigned);
void elden_actuator_peer_fail_stop(void *);
unsigned elden_actuator_peer_starts(void *);
void elden_actuator_peer_destroy(void *);
}
using IOOptionBits=uint32_t;
enum {kIODirectionInOut=1,kIOMemoryKernelUserShared=2,kIOMemoryMapperNone=4,kIOReturnSuccess=0};
static void *kernel_task=(void *)1;
static bool fail_malloc,fail_descriptor,fail_prepare,irq_context,deny_thermal;
static void (*allocation_hook)();static void (*sleep_hook)();static void *clock_peer;
static void *IOMalloc(size_t n){return fail_malloc?nullptr:std::malloc(n);}
static void IOFree(void *p,size_t){std::free(p);}
struct IOLock {std::mutex mutex;};
static void IOLockLock(IOLock *l){l->mutex.lock();}
static void IOLockUnlock(IOLock *l){l->mutex.unlock();}
struct IOMemoryDescriptor {unsigned retains=1;void retain(){retains++;}virtual ~IOMemoryDescriptor()=default;};
struct IOBufferMemoryDescriptor : IOMemoryDescriptor {
    void *bytes=nullptr;
    static IOBufferMemoryDescriptor *inTaskWithOptions(void *task,IOOptionBits options,size_t n,size_t alignment){
        CHECK(task==kernel_task&&options==7&&n==ELDEN_ACTUATOR_BYTES&&alignment==4096);
        if(allocation_hook)allocation_hook();if(fail_descriptor)return nullptr;
        auto *m=new IOBufferMemoryDescriptor;CHECK(!posix_memalign(&m->bytes,alignment,n));return m;
    }
    int prepare(){return fail_prepare?-1:0;}
    void *getBytesNoCopy(){return bytes;}
    ~IOBufferMemoryDescriptor() override{std::free(bytes);}
};
static void *elden_actuator_buffer_bytes(IOBufferMemoryDescriptor *m){return m->getBytesNoCopy();}
static uint64_t mach_absolute_time(){return elden_actuator_peer_now(clock_peer)*96/5;}
static void IOSleep(unsigned ms){elden_actuator_peer_advance(clock_peer,ms*1000);if(sleep_hook)sleep_hook();}
static bool ml_at_interrupt_context(){return irq_context;}
static void log(const char *){}
struct mach_timebase_info_data_t {uint32_t numer=625,denom=12;};
static unsigned requested_mode=1;
static void PE_parse_boot_argn(const char *,void *v,size_t n){CHECK(n==sizeof(unsigned));*(unsigned *)v=requested_mode;}
static void clock_timebase_info(mach_timebase_info_data_t *tb){tb->numer=625;tb->denom=12;}
struct ActuatorSession;
struct Context {
    IOLock *lock=nullptr;EldenAwChip chips[2]={};EldenHapticsBroker broker={};
    ActuatorSession *actuator=nullptr,*retired_actuators=nullptr;
    const EldenActuatorLimits *actuator_profile=nullptr;
    uint32_t actuator_enabled=1,actuator_opens=0,phase=2;void *hardware=nullptr;
};
static uint32_t stop_requested;
static int platform_admit(void *,int){return irq_context?-13:0;}
static bool thermal(Context *){return !deny_thermal;}
static const EldenHapticsGainPoint host_points[]={{0,0},{32767,8192}};
// Positive fixture is HOST-ONLY. Never exported as a calibrated native input.
#define ELDEN_HAPTICS_HAS_ACTUATOR 1
static const EldenActuatorLimits elden_actuator_limits={
    {{host_points,2,8192,24000,64,UINT64_C(1)<<42},{host_points,2,8192,24000,64,UINT64_C(1)<<42}},
    2400,{2400,2400},1000000,10000,512,64,60000,250000,{UINT64_C(1)<<30,UINT64_C(1)<<30},{16,16},3};
static const EldenActuatorLimits *elden_actuator_profile_get(){return &elden_actuator_limits;}
#include "NativeActuator.inc"
static Context *current;
static void round_hook(){(void)actuator_round(current);}
static void phase_hook(){CHECK(!current->broker.current_kind);current->phase=3;stop_requested=1;}
static void close_preparation_hook(){sleep_hook=round_hook;
    CHECK(current->actuator&&!current->actuator->prepared&&!current->broker.current_kind);
    CHECK(!actuator_close(current,current->actuator->owner,current->actuator->epoch));(void)actuator_round(current);
}
static void busy_hook(){sleep_hook=round_hook;auto *s=current->actuator;
    CHECK(actuator_request(current,s->owner,s->epoch,0)==-16);(void)actuator_round(current);
}
static uint64_t broker_time(void *v){return elden_actuator_peer_now(((Context *)v)->hardware);}
static void broker_lock(void *v,int take){if(take)IOLockLock(((Context *)v)->lock);else IOLockUnlock(((Context *)v)->lock);}
static int broker_admit(void *,int cleanup){return !cleanup&&deny_thermal?-13:0;}
static void *broker_alloc(void *,size_t n){return std::malloc(n);}
static void broker_free(void *,void *p,size_t){std::free(p);}
static Context *setup(const uint8_t *fw,size_t n){
    scenarios++;fail_malloc=fail_descriptor=fail_prepare=irq_context=deny_thermal=false;
    allocation_hook=nullptr;sleep_hook=round_hook;stop_requested=0;requested_mode=1;
    auto *c=new Context;c->actuator_profile=&elden_actuator_limits;c->lock=new IOLock;c->hardware=elden_actuator_peer_create(c->chips,fw,n);clock_peer=c->hardware;current=c;
    EldenHapticsBrokerIo io={c,broker_admit,broker_lock,broker_time,broker_alloc,broker_free};EldenAwChip *chips[]={&c->chips[0],&c->chips[1]};
    EldenSharedBudgetLimits l={100000,{100000,100000},100000,{UINT64_C(1)<<31,UINT64_C(1)<<31},3};
    CHECK(!elden_broker_init(&c->broker,&io,chips,nullptr,&l,nullptr,nullptr,0));return c;
}
static void destroy(Context *c){
    sleep_hook=nullptr;allocation_hook=nullptr;
    for(auto *s=c->retired_actuators;s;){auto *next=s->next;delete s->memory;if(s->history)IOFree(s->history,s->history_bytes);delete s;s=next;}
    elden_actuator_peer_destroy(c->hardware);delete c->lock;delete c;current=nullptr;clock_peer=nullptr;
}
static void play(Context *c){auto *s=c->actuator;CHECK(s&&s->ready);
    int16_t pcm[256];for(unsigned i=0;i<256;i++)pcm[i]=(int16_t)((i/2)&1?-4000:4000);
    EldenActuatorRing user={};CHECK(!elden_actuator_ring_bind(&user,s->memory->bytes,ELDEN_ACTUATOR_BYTES,s->owner,s->epoch));
    CHECK(!elden_actuator_ring_write(&user,s->ring.taken,pcm,256));
    elden_actuator_peer_advance(c->hardware,1500); // new due frames after StartIO's quiet prefetch
    CHECK(!actuator_round(c)&&s->live.state==EldenActuatorPlaying&&s->budget_run);
    CHECK(elden_actuator_peer_starts(c->hardware)==2);
}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t fw[4096];FILE *f=std::fopen(argv[1],"rb");CHECK(f);size_t n=std::fread(fw,1,sizeof(fw),f);CHECK(n==1436&&!std::ferror(f));CHECK(!std::fclose(f));
    uint64_t owner=99,epoch=99,info[8];Context *c=setup(fw,n);
    c->actuator_enabled=0;requested_mode=0;actuator_mode(c);CHECK(!c->actuator_enabled);
    requested_mode=1;actuator_mode(c);CHECK(c->actuator_enabled);c->actuator_enabled=0;
    CHECK(actuator_open(c,&owner,&epoch)==-19&&!c->retired_actuators&&!c->broker.mail.owner);
    c->actuator_enabled=1;c->phase=1;CHECK(actuator_open(c,&owner,&epoch)==-19&&!c->broker.mail.owner);destroy(c);
    for(unsigned fault=0;fault<4;fault++){
        c=setup(fw,n);fail_malloc=fault==0;fail_descriptor=fault==1;fail_prepare=fault==2;
        if(fault==3)allocation_hook=phase_hook;
        CHECK(actuator_open(c,&owner,&epoch)==(fault==3?-125:-12));
        CHECK(!c->actuator&&!c->broker.mail.owner&&!elden_actuator_peer_starts(c->hardware));destroy(c);
    }
    c=setup(fw,n);sleep_hook=close_preparation_hook;
    CHECK(actuator_open(c,&owner,&epoch)==-125&&!c->actuator&&!c->broker.current_kind);destroy(c);
    c=setup(fw,n);deny_thermal=true;
    CHECK(actuator_open(c,&owner,&epoch)==-125&&c->actuator&&c->actuator->closing);
    CHECK(!actuator_round(c)&&!c->actuator&&!c->broker.current_kind);destroy(c);
    c=setup(fw,n);CHECK(!actuator_open(c,&owner,&epoch));auto *first=c->actuator;
    CHECK(first->ready&&first->claimed&&first->prepared==1&&c->broker.current_kind==3);
    CHECK(!actuator_info(c,owner,epoch,info)&&info[0]==ELDEN_ACTUATOR_MAGIC&&info[2]==ELDEN_ACTUATOR_BYTES);
    auto *mapped=actuator_memory(c,owner,epoch);CHECK(mapped==first->memory&&mapped->retains==2);
    EldenActuatorRing premature={};CHECK(!elden_actuator_ring_bind(&premature,first->memory->bytes,ELDEN_ACTUATOR_BYTES,owner,epoch));
    int16_t premature_pcm[128];for(auto &v:premature_pcm)v=4000;
    CHECK(!elden_actuator_ring_write(&premature,first->ring.taken,premature_pcm,128));
    first->desired=1; // publication boundary before request_seq, no acknowledged StartIO
    CHECK(!actuator_round(c)&&!first->running&&first->live.state==EldenActuatorIdle&&!elden_actuator_peer_starts(c->hardware));
    first->desired=0;
    irq_context=true;CHECK(actuator_request(c,owner,epoch,1)==-125);irq_context=false;
    sleep_hook=busy_hook;CHECK(!actuator_request(c,owner,epoch,1)&&first->ring.generation==2&&first->ack_seq==1&&!first->control_busy);
    play(c);uint64_t playing_generation=first->ring.generation;
    CHECK(!actuator_request(c,owner,epoch,1)&&first->running&&first->ring.generation==playing_generation&&first->live.state==EldenActuatorPlaying);
    CHECK(!actuator_request(c,owner,epoch,0)&&!first->budget_run&&first->live.state==EldenActuatorIdle);
    CHECK(!elden_shared_budget_active(&c->broker.budget)&&c->broker.budget.slots[0].on_us[0]>0);
    uint64_t old_owner=owner,old_epoch=epoch;void *old_memory=first->memory->bytes;
    CHECK(!actuator_close(c,owner,epoch)&&!actuator_memory(c,owner,epoch)&&actuator_info(c,owner,epoch,info)==-19);
    CHECK(!actuator_round(c)&&!c->actuator&&!c->broker.current_kind);
    CHECK(!actuator_open(c,&owner,&epoch)&&owner!=old_owner&&c->actuator->memory->bytes!=old_memory&&first->closed==1);
    CHECK(actuator_close(c,old_owner,old_epoch)==-116&&c->actuator->owner==owner&&c->broker.live_owner==owner);
    // A prior map remains retained, but cannot supply the new owner's frames.
    EldenActuatorRing prior={};CHECK(!elden_actuator_ring_bind(&prior,old_memory,ELDEN_ACTUATOR_BYTES,old_owner,old_epoch));
    int16_t sample=100;CHECK(!elden_actuator_ring_write(&prior,prior.taken,&sample,1));
    int16_t output=99;CHECK(elden_actuator_ring_read(&c->actuator->ring,prior.taken,&output)==-61&&output==99);
    CHECK(!actuator_close(c,owner,epoch)&&!actuator_round(c));destroy(c);
    c=setup(fw,n);CHECK(!actuator_open(c,&owner,&epoch));sleep_hook=nullptr;
    CHECK(actuator_request(c,owner,epoch,1)==-110&&c->actuator->closing&&!c->actuator->desired);
    CHECK(!actuator_round(c)&&!c->actuator&&!c->broker.current_kind);destroy(c);
    c=setup(fw,n);sleep_hook=nullptr;CHECK(actuator_open(c,&owner,&epoch)==-110&&c->actuator->closing);
    CHECK(!actuator_round(c)&&!c->actuator&&!c->broker.current_kind);destroy(c);
    c=setup(fw,n);CHECK(!actuator_open(c,&owner,&epoch));CHECK(!actuator_request(c,owner,epoch,1));play(c);
    elden_actuator_peer_fail_stop(c->hardware);
    CHECK(actuator_request(c,owner,epoch,0)<0&&c->actuator->live.state==EldenActuatorUncertain);
    CHECK(c->actuator->memory&&c->actuator->budget_run&&elden_shared_budget_active(&c->broker.budget)&&c->broker.current_kind==3);
    CHECK(actuator_round(c)<0&&c->actuator->closed==2&&actuator_open(c,&owner,&epoch)<0);destroy(c);
    c=setup(fw,n);
    for(unsigned i=0;i<8;i++){CHECK(!actuator_open(c,&owner,&epoch));CHECK(!actuator_close(c,owner,epoch)&&!actuator_round(c));}
    CHECK(actuator_open(c,&owner,&epoch)==-12&&c->actuator_opens==8&&!c->broker.mail.owner);destroy(c);
    c=setup(fw,n);CHECK(!actuator_open(c,&owner,&epoch));CHECK(!actuator_request(c,owner,epoch,1));play(c);
    c->phase=3;stop_requested=1;CHECK(!actuator_shutdown(c)&&!c->actuator&&!c->broker.current_kind&&!elden_shared_budget_active(&c->broker.budget));destroy(c);
    std::printf("PASS: %u actual native Actuator lifecycle scenarios/%u checks; preparation/publication/ack/timeout/close/reconnect/retention; OS and registers modeled\n",scenarios,checks);return 0;
}
