/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual broker/mailbox/queue/budget/GENI/AW/RTP, with physical/OS peers modeled. */
#define main previous_bus_main
#include "TestBus.c"
#undef main
#include "Broker.h"
typedef struct {
    Model hardware;EldenAwChip chips[2];EldenHapticsBroker broker;
    unsigned lock,allocations,releases,fail_alloc,denied;
    int8_t pcm[128];EldenHapticsEffect effects[1];
    EldenHapticsClickPoint points[2];
} BrokerPeer;
static const char digest[]="0000000000000000000000000000000000000000000000000000000000000000";
static int b_admit(void *v,int cleanup){BrokerPeer *p=v;CHECK(!p->lock);return p->denied&&!cleanup?-13:0;}
static void b_lock(void *v,int acquire){BrokerPeer *p=v;CHECK(p->lock==(unsigned)!acquire);p->lock=(unsigned)acquire;}
static uint64_t b_now(void *v){return ((BrokerPeer *)v)->hardware.now;}
static void *b_allocate(void *v,size_t n){BrokerPeer *p=v;CHECK(!p->lock&&n<=1440000);p->allocations++;return p->allocations==p->fail_alloc?NULL:malloc(n);}
static void b_release(void *v,void *memory,size_t n){BrokerPeer *p=v;CHECK(!p->lock&&memory&&n==128);p->releases++;free(memory);}
static BrokerPeer *setup(const uint8_t *fw,size_t n,int calibrated){
    BrokerPeer *p=calloc(1,sizeof(*p));CHECK(p);Model *m=&p->hardware;initial(m);acquire(m);m->firmware=fw;m->firmware_bytes=n;
    EldenAwIo io={m,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned i=0;i<2;i++){CHECK(!elden_aw_bind(&p->chips[i],&io,(uint8_t)(0x5a+i)));CHECK(!elden_aw_initialize(&p->chips[i],fw,n,64));m->motor[i].reg[1]=16;}
    for(unsigned i=0;i<128;i++)p->pcm[i]=(int8_t)(i&1?12:-12);
    p->effects[0]=(EldenHapticsEffect){1,128,3,{16,16},{p->pcm,p->pcm},"peer-effect",digest,digest};
    p->points[0]=(EldenHapticsClickPoint){0,{0,0}};p->points[1]=(EldenHapticsClickPoint){32767,{16,16}};
    EldenHapticsQueueLimits ram={p->points,20000,1000000,1000000,{100000,100000},100000,{UINT64_C(1)<<40,UINT64_C(1)<<40},32,2,{64,64},3};
    EldenSharedBudgetLimits shared={1000000,{1000000,1000000},1000000,{UINT64_C(1)<<42,UINT64_C(1)<<42},3};
    EldenRtpLimits rtp={60000000,10000,60000,24000,{UINT64_C(1)<<40,UINT64_C(1)<<40},{24000,24000},32,{64,64},{127,127},3};
    EldenHapticsBrokerIo bio={p,b_admit,b_lock,b_now,b_allocate,b_release};EldenAwChip *chips[]={&p->chips[0],&p->chips[1]};
    CHECK(!elden_broker_init(&p->broker,&bio,chips,calibrated?&ram:NULL,calibrated?&shared:NULL,calibrated?&rtp:NULL,calibrated?p->effects:NULL,calibrated?1:0));return p;
}
static void complete_rtp(BrokerPeer *p){
    CHECK(!elden_broker_step(&p->broker));CHECK(p->broker.rtp.sent[0]==128&&p->broker.rtp.sent[1]==128);
    for(unsigned i=0;i<2;i++){p->hardware.motor[i].reg[0x3f]=0;p->hardware.motor[i].reg[2]=1;}
    CHECK(!elden_broker_step(&p->broker));
}
static void open(BrokerPeer *p,uint64_t *owner,uint64_t *epoch){CHECK(!elden_broker_open(&p->broker,owner,epoch));}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t fw[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);size_t n=fread(fw,1,sizeof(fw),f);CHECK(n==1436&&!ferror(f));CHECK(!fclose(f));
    uint64_t owner,epoch;EldenHapticsResult result;BrokerPeer *p=setup(fw,n,0);open(p,&owner,&epoch);
    unsigned writes=p->hardware.reg_writes;CHECK(elden_broker_submit_ram(&p->broker,owner,epoch,1,0,10000,3,1,32767)==-13);
    CHECK(elden_broker_submit_effect(&p->broker,owner,epoch,1,1,0,32767)==-13&&p->hardware.reg_writes==writes);
    CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);
    // Back-to-back RAM starts use fresh budget time after the previous STOP,
    // and cannot report a spurious clock rollback from the queue timestamp.
    p=setup(fw,n,1);open(p,&owner,&epoch);
    CHECK(!elden_broker_submit_ram(&p->broker,owner,epoch,1,0,10000,1,1,32767));CHECK(!elden_broker_step(&p->broker));
    CHECK(!elden_broker_submit_ram(&p->broker,owner,epoch,2,0,10000,1,1,32767));p->hardware.now+=10000;
    CHECK(!elden_broker_step(&p->broker)&&!p->broker.budget.clock_failed&&p->hardware.motor[0].starts==2);
    CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_step(&p->broker));CHECK(!elden_broker_stop(&p->broker));free(p);
    p=setup(fw,n,1);open(p,&owner,&epoch);
    CHECK(!elden_broker_submit_ram(&p->broker,owner,epoch,1,0,10000,3,1,32767));CHECK(!elden_broker_step(&p->broker)&&p->broker.current_kind==1);
    CHECK(!elden_broker_submit_effect(&p->broker,owner,epoch,2,1,0,16384));CHECK(!elden_broker_step(&p->broker)&&!p->allocations);
    p->hardware.now+=10000;CHECK(!elden_broker_step(&p->broker)&&p->broker.current_kind==2);
    CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsComplete);
    complete_rtp(p);CHECK(!elden_broker_result(&p->broker,owner,epoch,2,&result)&&result.state==EldenHapticsComplete);
    CHECK(p->allocations==2&&p->releases==2&&!elden_shared_budget_active(&p->broker.budget));
    CHECK(!elden_broker_close(&p->broker,owner,epoch));uint64_t prior=owner;open(p,&owner,&epoch);CHECK(owner>prior);
    // Reconnects retain the previous RAM and RTP cost entries.
    unsigned used=0;for(unsigned i=0;i<64;i++)used+=p->broker.budget.slots[i].used;CHECK(used==2);
    CHECK(!elden_broker_submit_effect(&p->broker,owner,epoch,1,1,0,32767));CHECK(!elden_broker_step(&p->broker));
    CHECK(!elden_broker_cancel(&p->broker,owner,epoch,1)&&!elden_broker_step(&p->broker));
    CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsCancelled&&p->releases==4);
    CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);
    // All cancellation sites, second buffer allocation, admission and STOP uncertainty.
    for(unsigned fault=0;fault<4;fault++){
        p=setup(fw,n,1);open(p,&owner,&epoch);if(fault==0)p->fail_alloc=2;
        CHECK(!elden_broker_submit_effect(&p->broker,owner,epoch,1,1,0,32767));int rc=elden_broker_step(&p->broker);
        if(fault==0){CHECK(rc==-12&&!p->broker.current_kind&&p->releases==1&&!p->hardware.motor[0].starts);CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.error==-12);free(p);continue;}
        CHECK(!rc&&p->broker.current_kind==2);
        if(fault==1){CHECK(!elden_broker_close(&p->broker,owner,epoch));CHECK(elden_broker_open(&p->broker,&owner,&epoch)==-16);CHECK(!elden_broker_step(&p->broker)&&p->releases==2&&!p->broker.mail.owner);}
        if(fault==2){p->denied=1;CHECK(elden_broker_step(&p->broker)==-13&&p->releases==2);CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);}
        if(fault==3){CHECK(!elden_broker_close(&p->broker,owner,epoch));p->hardware.stall=p->hardware.drop_cancel=p->hardware.drop_abort=1;
            CHECK(elden_broker_step(&p->broker)<0&&p->broker.held&&p->broker.mail.closing&&!p->releases&&p->broker.pcm[0]);
            CHECK(elden_shared_budget_active(&p->broker.budget));CHECK(elden_broker_open(&p->broker,&owner,&epoch)==-125);
            // Intentionally held model allocations are retained by production;
            // host cleanup frees them only after the simulated process ends.
            free(p->broker.pcm[0]);free(p->broker.pcm[1]);}
        free(p);
    }
    // Older RAM-only mailbox consumer cannot accidentally execute an effect.
    EldenHapticsMailbox mail={0};CHECK(!elden_haptics_mail_open(&mail,1,1));EldenHapticsRequest request={1,1,1,0,1000000,3,1,{0,0},0};
    CHECK(!elden_haptics_mail_submit_effect(&mail,&request,32767,1));uint16_t intensity;uint32_t effect;
    CHECK(elden_haptics_mail_take(&mail,&request,&intensity)==-13);
    CHECK(!elden_haptics_mail_take_native(&mail,&request,&intensity,&effect)&&effect==1&&intensity==32767);
    printf("PASS: %u actual broker/GENI/AW/RTP checks; shared RAM/RTP budget, serial dispatch, immutable effect IDs, client close, allocation and retained STOP\n",checks);return 0;
}
