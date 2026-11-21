/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define main previous_bus_main
#include "TestBus.c"
#undef main
#include "RtpStream.h"
typedef struct {Model bus;EldenAwChip chips[2];unsigned reserve,denied;uint64_t squares[2];uint32_t on[2];} RtpPeer;
static int r_admit(void *v,uint64_t id,int cleanup){RtpPeer *p=v;CHECK(id==1);return p->denied&&!cleanup?-13:0;}
static int reserve(void *v,uint64_t id,uint32_t duration,const uint64_t square[2],const uint32_t on[2]){
    RtpPeer *p=v;CHECK(id==1&&duration>500000);p->reserve++;for(unsigned i=0;i<2;i++){p->squares[i]=square[i];p->on[i]=on[i];}return 0;
}
static uint64_t r_now(void *v){return ((RtpPeer *)v)->bus.now;}
static RtpPeer *setup(const uint8_t *firmware,size_t bytes){
    RtpPeer *p=calloc(1,sizeof(*p));CHECK(p);initial(&p->bus);acquire(&p->bus);p->bus.firmware=firmware;p->bus.firmware_bytes=bytes;
    EldenAwIo io={&p->bus,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned i=0;i<2;i++){CHECK(!elden_aw_bind(&p->chips[i],&io,(uint8_t)(0x5a+i)));CHECK(!elden_aw_initialize(&p->chips[i],firmware,bytes,64));p->bus.motor[i].reg[1]=16;}
    return p;
}
static EldenRtpLimits profile(void){
    EldenRtpLimits l={1000000,10000,60000,24000,{UINT64_C(1)<<50,UINT64_C(1)<<50},{24000,24000},128,{64,64},{127,127},3};return l;
}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t firmware[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);size_t n=fread(firmware,1,sizeof(firmware),f);CHECK(n==1436&&!ferror(f));CHECK(!fclose(f));
    int8_t samples[128];for(unsigned i=0;i<128;i++)samples[i]=(int8_t)((int)i-64);
    for(unsigned rate=0;rate<3;rate++){
        RtpPeer *p=setup(firmware,n);EldenAwRtp r={0};uint8_t old[7];const uint8_t regs[]={6,7,8,0x46,0x2f,0x30,0x31};
        for(unsigned i=0;i<7;i++)old[i]=p->bus.motor[0].reg[regs[i]];
        uint32_t hz=rate==0?12000:rate==1?24000:48000;
        CHECK(!elden_aw_rtp_prepare(&r,&p->chips[0],hz,32,100000)&&!p->bus.motor[0].starts);
        CHECK(p->bus.motor[0].reg[0x46]%128/32==(rate==0?2u:rate==1?0u:1u));
        CHECK(p->bus.motor[0].reg[0x2f]==0x46&&!p->bus.motor[0].reg[0x30]&&!p->bus.motor[0].reg[0x31]);
        CHECK(!elden_aw_rtp_go(&r)&&p->bus.motor[0].starts==1);
        CHECK(!elden_aw_rtp_feed(&r,samples,64)&&!elden_aw_rtp_feed(&r,samples+64,64));
        CHECK(p->bus.motor[0].rtp_count==128&&!memcmp(p->bus.motor[0].rtp,samples,128));
        p->bus.motor[0].reg[1]=8;unsigned writes=p->bus.motor[0].rtp_count;
        CHECK(elden_aw_rtp_feed(&r,samples,1)==-16&&p->bus.motor[0].rtp_count==writes);
        CHECK(!elden_aw_rtp_stop(&r));for(unsigned i=0;i<7;i++)CHECK(p->bus.motor[0].reg[regs[i]]==old[i]);free(p);
    }
    RtpPeer *p=setup(firmware,n);EldenRtpStream s={0};EldenRtpLimits l=profile();EldenRtpClip clip={{samples,samples},128,24000,3,{32,24},1,0,0,0,{0,0}};
    EldenAwChip *chips[]={&p->chips[0],&p->chips[1]};EldenRtpIo io={p,r_admit,reserve,r_now};
    CHECK(!elden_rtp_stream_start(&s,chips,&io,&l,&clip)&&p->reserve==1);
    CHECK(!elden_rtp_stream_step(&s)&&s.sent[0]==128&&s.sent[1]==128&&s.state==EldenRtpPlaying);
    // Almost-empty while actually playing must never report completion.
    p->bus.motor[0].reg[1]=16;p->bus.motor[1].reg[1]=16;CHECK(!elden_rtp_stream_step(&s));
    // DONE may be read/cleared before the following state read reaches standby.
    for(unsigned i=0;i<2;i++)p->bus.motor[i].reg[2]=1;
    CHECK(!elden_rtp_stream_step(&s));
    for(unsigned i=0;i<2;i++){p->bus.motor[i].reg[0x3f]=0;p->bus.motor[i].reg[2]=0;}
    CHECK(elden_rtp_stream_step(&s)==1&&s.state==EldenRtpComplete&&!s.active);free(p);
    for(unsigned mode=0;mode<4;mode++){
        p=setup(firmware,n);memset(&s,0,sizeof(s));chips[0]=&p->chips[0];chips[1]=&p->chips[1];io.context=p;l=profile();
        if(mode==0){l.measured_min_payload_bps=40000;CHECK(elden_rtp_stream_start(&s,chips,&io,&l,&clip)==-13&&!p->reserve);free(p);continue;}
        CHECK(!elden_rtp_stream_start(&s,chips,&io,&l,&clip));
        if(mode==1){p->bus.motor[0].reg[1]=8;p->bus.motor[1].reg[1]=8;CHECK(!elden_rtp_stream_step(&s));p->bus.now+=10001;CHECK(elden_rtp_stream_step(&s)<0);}
        if(mode==2){p->denied=1;CHECK(elden_rtp_stream_step(&s)==-13&&s.state==EldenRtpFailed);CHECK(elden_rtp_stream_step(&s)==-13);}
        if(mode==3){p->bus.stall=p->bus.drop_cancel=p->bus.drop_abort=1;CHECK(elden_rtp_stream_step(&s)<0&&s.state==EldenRtpHeld&&s.active);}
        free(p);
    }
    // Long continuous admission is not silently capped to the RAM click limit.
    // No60-second sleep or hardware output: just actual preflight/setup/stop.
    int8_t *long_pcm=calloc(1440000,1);CHECK(long_pcm);
    p=setup(firmware,n);memset(&s,0,sizeof(s));chips[0]=&p->chips[0];chips[1]=&p->chips[1];io.context=p;l=profile();l.max_duration_us=60000000;
    EldenRtpClip long_clip={{long_pcm,long_pcm},1440000,24000,3,{32,24},1,0,0,0,{0,0}};
    CHECK(!elden_rtp_stream_start(&s,chips,&io,&l,&long_clip));CHECK(!elden_rtp_stream_stop(&s)&&s.state==EldenRtpCancelled);
    CHECK(p->on[0]==24000&&p->on[1]==24000); // silence still has active motors
    free(p);free(long_pcm);
    printf("PASS: %u actual RTP/GENI/AW checks; rate/threshold/GO/burst/AES/DONE, two-chip feed, measured bandwidth gate and retained failures; hardware modeled\n",checks);
}
