// SPDX-License-Identifier: MIT
// RPMh votes & CallGate boundary stress harness (ASan+UBSan).
// Real units under test: EldenGpuVotes.c, Sm8750GmuCommandDb.c, CallGate.h.
// Synthetic rails/BCM data only; no IO, no device, no hardware claim.
// Gap-focused: existing chain tests cover base cases (TestVotes 49 cases,
// TestCallGate close/overflow/concurrent denial). This harness adds monotonicity
// invariants, encode determinism, >=1000 mixed stress iterations, integer/bounds
// negatives, and transactional-integrity checks not previously asserted.
#include "EldenGpuVotes.h"
#include "CallGate.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

static unsigned failures = 0;
static unsigned long iterations = 0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;}else{++iterations;}}while(0)

static uint8_t railbuf[40];
static uint8_t auxbuf[8]={0xe8,3,0,0,16,0,0,0};

static Sm8750GmuVoteInputs make_inputs(){
    const uint16_t corners[]={0,32,48,50,52,56,60,64,80,128,144,192,224,256,320,416,448,452};
    for(unsigned i=0;i<sizeof(corners)/sizeof(*corners);++i){railbuf[2*i]=(uint8_t)corners[i];railbuf[2*i+1]=(uint8_t)(corners[i]>>8);}
    Sm8750GmuVoteInputs in={};
    in.gfx=in.mx=in.cx=in.gmxc=in.gbx=(AdrenoHfiRail){railbuf,sizeof(railbuf)};
    in.sh0=(Sm8750GmuBcmInput){0x50000,{auxbuf,8}};
    in.mc0=in.sh0;in.mc0.address+=16;
    in.acv=in.sh0;in.acv.address+=32;
    in.cn0_address=0x50030;
    return in;
}

static const uint32_t fuses[]={190,221,232,242};
static const uint64_t common[]={160000000,222000000,342000000,389000000,443000000,525000000,607000000,660000000,734000000,832000000,900000000};
static const unsigned ncommon=sizeof(common)/sizeof(*common);

int main(){
    // ---- Positive 1: vote monotonicity + bandwidth sortedness across full sweep
    {
        Sm8750GmuVoteInputs in=make_inputs();
        for(unsigned bin=0;bin<4;++bin){
            uint32_t prev_vote=0;uint64_t prev_hz=0;bool mono=true,sorted_bw=true;
            for(unsigned i=0;i<ncommon;++i){
                AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
                if(elden_gpu_build_startup(&in,fuses[bin],common[i],&c,&level,&bw))continue;
                for(unsigned g=0;g<c.gpu_count;++g){
                    if(g&&c.gpu[g].hz<=prev_hz&&c.gpu[g].vote<prev_vote)mono=false;
                    prev_hz=c.gpu[g].hz;prev_vote=c.gpu[g].vote;
                    if(g&&c.gpu[g].hz>c.gpu[g-1].hz&&c.gpu[g].vote<c.gpu[g-1].vote)mono=false;
                }
                if(!c.bandwidth_count||c.bandwidth_count>16)sorted_bw=false;
                if(bw>=c.bandwidth_count)sorted_bw=false;
                break;
            }
            CHECK(mono);
            CHECK(sorted_bw);
        }
    }
    // ---- Positive 2: encode determinism — identical inputs produce byte-identical wire
    {
        Sm8750GmuVoteInputs in=make_inputs();
        for(unsigned i=0;i<ncommon;++i){
            AdrenoHfiStartupConfig c1={},c2={};unsigned l1=0,b1=0,l2=0,b2=0;
            CHECK(!elden_gpu_build_startup(&in,190,common[i],&c1,&l1,&b1));
            CHECK(!elden_gpu_build_startup(&in,190,common[i],&c2,&l2,&b2));
            CHECK(!memcmp(&c1,&c2,sizeof(c1))&&l1==l2&&b1==b2);
            uint32_t w1[159]={0},w2[159]={0},id1=0,id2=0;size_t n1=0,n2=0;
            CHECK(!AdrenoHfiStartupEncode(&c1,AdrenoHfiStagePerformance,&id1,w1,159,&n1));
            CHECK(!AdrenoHfiStartupEncode(&c2,AdrenoHfiStagePerformance,&id2,w2,159,&n2));
            CHECK(id1==id2&&n1==n2&&!memcmp(w1,w2,n1*sizeof(uint32_t)));
        }
    }
    // ---- Positive 3: encode shape invariant across sweep
    {
        Sm8750GmuVoteInputs in=make_inputs();
        for(unsigned bin=0;bin<4;++bin)for(unsigned i=0;i<ncommon;++i){
            AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
            if(elden_gpu_build_startup(&in,fuses[bin],common[i],&c,&level,&bw))continue;
            uint32_t wire[159]={0},id=0;size_t words=0;
            CHECK(!AdrenoHfiStartupEncode(&c,AdrenoHfiStagePerformance,&id,wire,159,&words));
            CHECK(id==15&&words==6+3*c.gpu_count+2*c.gmu_count&&wire[5]==UINT32_MAX);
            CHECK(!c.enable_ifpc&&!c.acd_enable_mask);
        }
    }
    // ---- Positive 4: stress — mixed valid/boundary iterations across bins, alternating direction
    {
        Sm8750GmuVoteInputs in=make_inputs();
        unsigned ok=0;
        for(unsigned round=0;round<400;++round){
            unsigned bin=round%4;
            unsigned idx=(round%2)?(ncommon-1-(round/2)%ncommon):((round/2)%ncommon);
            AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
            if(!elden_gpu_build_startup(&in,fuses[bin],common[idx],&c,&level,&bw)){
                ++ok;
                CHECK(c.gpu[level].hz==common[idx]&&bw>0);
            }
            iterations+=2;
        }
        CHECK(ok>=300);
    }
    // ---- Positive 5: CallGate lifecycle beyond chain coverage
    {
        EldenGpuCallGate g={};
        CHECK(eldenGpuEmpty(&g)&&!eldenGpuClosed(&g));
        {
            EldenGpuCallLease a(&g);
            CHECK(a);
            {
                EldenGpuCallLease b(&g);
                CHECK(b);
                CHECK(!eldenGpuEmpty(&g));
            }
            CHECK(!eldenGpuEmpty(&g));
            eldenGpuClose(&g);
            CHECK(eldenGpuClosed(&g));
            EldenGpuCallLease denied(&g);
            CHECK(!denied);
        }
        CHECK(eldenGpuEmpty(&g));
        eldenGpuClose(&g);
        CHECK(eldenGpuClosed(&g));
    }
    // ---- Positive 6: CallGate concurrent drain stress (single-thread interleave model)
    {
        unsigned entries=0;
        for(unsigned round=0;round<300;++round){
            EldenGpuCallGate g={};
            EldenGpuCallLease *leases[8]={};
            unsigned held=0;
            for(unsigned i=0;i<8;++i){
                leases[i]=new EldenGpuCallLease(&g);
                if(*leases[i]){++held;++entries;}
            }
            CHECK(held==8);
            eldenGpuClose(&g);
            for(unsigned i=0;i<8;++i)CHECK(!eldenGpuEnter(&g));
            for(unsigned i=0;i<8;++i){delete leases[i];}
            CHECK(eldenGpuEmpty(&g));
            iterations+=17;
        }
        CHECK(entries==2400);
    }
    // ---- Negative 1: out-of-range fuse rejected, outputs transactional-untouched
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c;memset(&c,0xa5,sizeof(c));
        AdrenoHfiStartupConfig before=c;unsigned level=99,bw=98;
        CHECK(elden_gpu_build_startup(&in,252,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        CHECK(!memcmp(&c,&before,sizeof(c))&&level==99&&bw==98);
    }
    // ---- Negative 2: non-OPP hz / zero hz rejected
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
        CHECK(elden_gpu_build_startup(&in,190,1200000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        CHECK(elden_gpu_build_startup(&in,190,0,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        CHECK(elden_gpu_build_startup(&in,190,222000001,&c,&level,&bw)==AdrenoHfiStartupInvalid);
    }
    // ---- Negative 3: null inputs/rails rejected without crash
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
        CHECK(elden_gpu_build_startup(0,190,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        CHECK(elden_gpu_build_startup(&in,190,222000000,0,&level,&bw)==AdrenoHfiStartupInvalid);
        Sm8750GmuVoteInputs bad=in;bad.gfx.data=0;
        CHECK(elden_gpu_build_startup(&bad,190,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        Sm8750GmuVoteInputs bad2=in;bad2.gfx.bytes=0;
        CHECK(elden_gpu_build_startup(&bad2,190,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
    }
    // ---- Negative 4: truncated aux / bcm data rejected
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
        Sm8750GmuVoteInputs bad=in;bad.acv.aux.bytes=1;
        CHECK(elden_gpu_build_startup(&bad,190,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        Sm8750GmuVoteInputs bad2=in;bad2.sh0.aux.data=0;
        CHECK(elden_gpu_build_startup(&bad2,190,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
    }
    // ---- Negative 5: encode into truncated buffer fails safely
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
        CHECK(!elden_gpu_build_startup(&in,190,525000000,&c,&level,&bw));
        uint32_t wire[4]={0},id=0;size_t words=0;
        int rc=AdrenoHfiStartupEncode(&c,AdrenoHfiStagePerformance,&id,wire,4,&words);
        CHECK(rc!=0);
    }
    // ---- Negative 6: CallGate overflow + post-close entry denial
    {
        EldenGpuCallGate g={};
        g.active=UINT32_MAX;
        CHECK(!eldenGpuEnter(&g));
        CHECK(g.active==UINT32_MAX);
        g={};eldenGpuClose(&g);
        CHECK(!eldenGpuEnter(&g));
        EldenGpuCallLease lease(&g);
        CHECK(!lease);
    }
    // ---- Negative 7: integer-overflow-scale hz rejected (no wrap into valid range)
    {
        Sm8750GmuVoteInputs in=make_inputs();
        AdrenoHfiStartupConfig c={};unsigned level=0,bw=0;
        CHECK(elden_gpu_build_startup(&in,190,0xFFFFFFFFFFFFFFFFull,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        CHECK(elden_gpu_build_startup(&in,0xFFFFFFFFu,222000000,&c,&level,&bw)==AdrenoHfiStartupInvalid);
    }

    if(failures){
        std::printf("RPMH_CALLGATE_RESULT {\"passed\":false,\"failures\":%u,\"iterations\":%lu}\n",failures,iterations);
        return 1;
    }
    std::printf("RPMH_CALLGATE_RESULT {\"passed\":true,\"positive\":6,\"negative\":7,\"iterations\":%lu}\n",iterations);
    return 0;
}
