/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The actual48k ring ->C LUT ->AW/GENI composition. No native/motor hardware. */
#define main previous_bus_main
#include "../TestBus.c"
#undef main
#include "Live.h"
typedef struct {
    Model bus;EldenAwChip chips[2];EldenActuatorLive live;EldenActuatorRing kernel,user;
    EldenActuatorHistory history[64];EldenHapticsGainPoint points[2];EldenActuatorLimits limits;
    void *memory;unsigned reserves,finishes,confirmed,denied;
} LivePeer;
static int live_admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){
    LivePeer *p=v;CHECK(owner==11&&epoch==7);return p->denied&&!cleanup?-13:0;
}
static int live_reserve(void *v,uint64_t owner,uint64_t epoch,const uint64_t cost[2],const uint32_t on[2]){
    LivePeer *p=v;CHECK(owner==11&&epoch==7&&cost[0]&&cost[1]&&on[0]&&on[1]);p->reserves++;return 0;
}
static int live_finish(void *v,uint64_t owner,uint64_t epoch,int confirmed,const uint64_t square[2],const uint32_t on[2]){
    LivePeer *p=v;CHECK(owner==11&&epoch==7);
    if(confirmed){CHECK(square&&on&&square[0]<=p->limits.max_squares[0]&&square[1]<=p->limits.max_squares[1]);
        CHECK(on[0]<=p->limits.max_on_frames[0]&&on[1]<=p->limits.max_on_frames[1]);p->confirmed++;}
    p->finishes++;return confirmed?0:-125;
}
static uint64_t live_host(void *v){return ((LivePeer *)v)->bus.now*96/5;}
static LivePeer *setup(const uint8_t *firmware,size_t n){
    LivePeer *p=calloc(1,sizeof(*p));CHECK(p);initial(&p->bus);acquire(&p->bus);p->bus.firmware=firmware;p->bus.firmware_bytes=n;
    EldenAwIo io={&p->bus,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned m=0;m<2;m++){CHECK(!elden_aw_bind(&p->chips[m],&io,(uint8_t)(0x5a+m)));CHECK(!elden_aw_initialize(&p->chips[m],firmware,n,64));p->bus.motor[m].reg[1]=16;}
    p->memory=calloc(1,ELDEN_ACTUATOR_BYTES);CHECK(p->memory);
    CHECK(!elden_actuator_ring_init(&p->kernel,p->memory,ELDEN_ACTUATOR_BYTES,live_host(p)+1,11,7));
    CHECK(!elden_actuator_ring_bind(&p->user,p->memory,ELDEN_ACTUATOR_BYTES,11,7));
    p->points[0]=(EldenHapticsGainPoint){0,0};p->points[1]=(EldenHapticsGainPoint){32767,8192};
    for(unsigned m=0;m<2;m++)p->limits.sample_map[m]=(EldenHapticsCalibration){p->points,2,8192,24000,64,UINT64_C(1)<<42};
    p->limits.window_frames=64;p->limits.max_on_frames[0]=p->limits.max_on_frames[1]=64;p->limits.max_run_us=1000000;
    p->limits.max_gap_us=10000;p->limits.max_missing_frames=512;p->limits.silence_frames=64;
    p->limits.measured_min_payload_bps=60000;p->limits.max_startup_lateness_us=250000;
    p->limits.max_squares[0]=p->limits.max_squares[1]=UINT64_C(1)<<26;p->limits.mask=3;p->limits.gain[0]=p->limits.gain[1]=16;
    return p;
}
static void init(LivePeer *p){
    EldenActuatorLiveIo io={p,live_admit,live_reserve,live_finish,live_host};EldenAwChip *chips[]={&p->chips[0],&p->chips[1]};
    CHECK(!elden_actuator_live_init(&p->live,&p->kernel,chips,&p->limits,p->history,sizeof(p->history),&io));
}
static void input(LivePeer *p){
    int16_t pcm[1024];for(unsigned i=0;i<1024;i++)pcm[i]=(int16_t)((i/2)&1?-4000:4000);
    CHECK(!elden_actuator_ring_write(&p->user,0,pcm,1024));
}
static void destroy(LivePeer *p){free(p->memory);free(p);}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t fw[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);size_t n=fread(fw,1,sizeof(fw),f);CHECK(n==1436&&!ferror(f));CHECK(!fclose(f));
    LivePeer *p=setup(fw,n);init(p);input(p);
    CHECK(!elden_actuator_live_step(&p->live,1));CHECK(p->reserves==1&&p->live.state==EldenActuatorPlaying);
    CHECK(p->bus.motor[0].starts==1&&p->bus.motor[1].starts==1&&p->bus.motor[0].rtp_count==32&&p->bus.motor[1].rtp_count==32);
    CHECK(p->bus.motor[0].rtp[0]>0&&(int8_t)p->bus.motor[0].rtp[1]<0);
    CHECK(!elden_actuator_live_stop(&p->live)&&p->confirmed==1&&!p->chips[0].busy&&!p->chips[1].busy);
    CHECK(p->live.rolling_on[0]==32&&p->live.rolling_on[1]==32);destroy(p);
    for(unsigned fault=0;fault<5;fault++){
        p=setup(fw,n);if(fault==0)p->limits.max_on_frames[0]=p->limits.max_on_frames[1]=32;init(p);input(p);
        CHECK(!elden_actuator_live_step(&p->live,1));
        if(fault==0){
            // This intentionally undersized duty limit does not cover the
            // real two-chip STOP/restore delay. Physical standby is observed,
            // but the exceeded reservation must stay held, not settle green.
            p->bus.now+=1000;
            CHECK(elden_actuator_live_step(&p->live,1)==-13&&p->live.state==EldenActuatorUncertain);
            CHECK(!p->confirmed&&p->live.reserved&&!p->chips[0].busy&&!p->chips[1].busy);
        }
        if(fault==1){p->denied=1;CHECK(elden_actuator_live_step(&p->live,1)==-13&&p->live.state==EldenActuatorTerminal&&p->confirmed==1);}
        if(fault==2){p->bus.now+=10001;CHECK(elden_actuator_live_step(&p->live,1)==-110&&p->confirmed==1);}
        if(fault==3){p->bus.stall=p->bus.drop_cancel=p->bus.drop_abort=1;CHECK(elden_actuator_live_stop(&p->live)<0&&p->live.state==EldenActuatorUncertain&&!p->confirmed&&p->live.reserved);}
        if(fault==4){p->kernel.page->owner=99;CHECK(elden_actuator_live_step(&p->live,1)==-84&&p->confirmed==1);}
        destroy(p);
    }
    // A measured-bandwidth deficit rejects initialization before the chip GO.
    p=setup(fw,n);p->limits.measured_min_payload_bps=40000;EldenActuatorLiveIo io={p,live_admit,live_reserve,live_finish,live_host};EldenAwChip *chips[]={&p->chips[0],&p->chips[1]};
    CHECK(elden_actuator_live_init(&p->live,&p->kernel,chips,&p->limits,p->history,sizeof(p->history),&io)==-22&&!p->bus.motor[0].starts);destroy(p);
    printf("PASS: %u actual Actuator/ring/LUT/GENI/AW checks; dual feed, missing/timing/admission/duty/stop and retained uncertainty; hardware modeled\n",checks);return 0;
}
