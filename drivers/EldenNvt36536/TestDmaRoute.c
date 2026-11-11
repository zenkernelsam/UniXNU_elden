/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaRoute.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SMR7 0x81cu
#define S2CR7 0xc1cu
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
typedef struct {
    uint32_t regs[0x80004/4],original[0x80004/4];
    unsigned reads,writes,claim,drop,fail_write,offset[8],value[8];
    bool held,deny_claim,busy,silent_drop,applied_error;
} Model;
static Model m;
static uint32_t rd(void *p,unsigned off){
    CHECK(p==&m&&m.held&&!(off&3)&&(off<0x2000||off==0x80000));++m.reads;
    return m.regs[off/4];
}
static int wr(void *p,unsigned off,uint32_t value){
    CHECK(p==&m&&m.held&&(off==SMR7||off==S2CR7)&&m.writes<8);
    CHECK(off!=S2CR7||value==0); /* real hardware aborts on reset readout */
    m.offset[m.writes]=off;m.value[m.writes]=value;++m.writes;
    if(m.writes==m.fail_write){
        if(m.applied_error)m.regs[off/4]=value;
        return m.silent_drop?0:-5;
    }
    m.regs[off/4]=value;return 0;
}
static int claim(void *p,uint64_t epoch){CHECK(p==&m&&epoch==37&&!m.held);++m.claim;if(m.deny_claim)return -16;m.held=true;return 0;}
static void drop(void *p,uint64_t epoch){CHECK(p==&m&&epoch==37&&m.held);++m.drop;m.held=false;}
static int idle(void *p){CHECK(p==&m&&m.held);return m.busy?-16:0;}
static EldenTouchRouteIo io={&m,rd,wr,claim,drop,idle};
static void fresh(const char *fixture){
    memset(&m,0,sizeof(m));FILE *f=fopen(fixture,"rb");CHECK(f!=NULL);
    CHECK(fread(m.regs,1,0x2000,f)==0x2000&&fgetc(f)==EOF);fclose(f);
    // v25 did not read CB0. Both disabled/enabled models are tested; zero is
    // a synthetic qualification input, never labeled as new live evidence.
    m.regs[0x80000/4]=0;memcpy(m.original,m.regs,sizeof(m.regs));
}
static int attach(EldenTouchDmaRouteOwner *p){return elden_touch_route_attach(p,&io,37,0x180000000ULL,0x180004000ULL,16384);}
static void successful(const char *fixture){
    fresh(fixture);EldenTouchDmaRouteOwner p={0};CHECK(!attach(&p));
    CHECK(p.attached&&p.claimed&&!p.poisoned&&m.writes==2&&m.held);
    CHECK(m.offset[0]==S2CR7&&m.value[0]==0&&m.offset[1]==SMR7&&m.value[1]==0x800000a3);
    CHECK(!elden_touch_route_observe(&p,37)&&m.writes==2);
    CHECK(elden_touch_route_observe(&p,36)==-16&&m.writes==2&&!p.poisoned);
    CHECK(!elden_touch_route_detach(&p,37));
    CHECK(p.detached&&!p.attached&&!p.claimed&&!m.held&&m.drop==1&&m.writes==3);
    CHECK(m.offset[2]==SMR7&&m.value[2]==0&&m.regs[S2CR7/4]==0);
    m.original[S2CR7/4]=0; /* invalid SMR plus safe S2CR, not opaque reset */
    CHECK(!memcmp(m.regs,m.original,sizeof(m.regs)));CHECK(attach(&p)==-16);
    fresh(fixture);memset(&p,0,sizeof(p));m.regs[0x80000/4]=0xe0;
    CHECK(!attach(&p)&&p.cb0_seen&&p.cb0_sctlr==0xe0);
    CHECK(!elden_touch_route_detach(&p,37)&&m.regs[0x80000/4]==0xe0);
    fresh(fixture);memset(&p,0,sizeof(p));m.regs[0x80000/4]=0x0a9f00e0;
    CHECK(!attach(&p));m.regs[0x80000/4]=0xa0;
    CHECK(elden_touch_route_observe(&p,37)!=0&&p.poisoned&&m.writes==2);
}
static void refused(const char *fixture){
    for(unsigned test=0;test<10;test++){
        fresh(fixture);EldenTouchDmaRouteOwner p={0};
        if(test==0)m.deny_claim=true;
        if(test==1)m.regs[0x20/4]^=1;
        if(test==2)m.regs[SMR7/4]=0x800000a3;
        if(test==3)m.regs[(0x800+131*4)/4]=0x800000a3; // no128-slot shortcut
        if(test==4)m.regs[0x1000/4]^=1;
        if(test==5)m.regs[0x80000/4]=1;
        if(test==6)m.regs[0x48/4]=1;
        if(test==7)m.busy=true;
        if(test==8)m.regs[0xc00/4]=2; // foreign UFS route
        if(test==9)m.regs[0x80000/4]=0xffffffff;
        CHECK(attach(&p)!=0&&!p.attached&&!p.poisoned&&!m.writes&&!m.held);
        CHECK(m.drop==(test?1u:0u));
    }
    fresh(fixture);EldenTouchDmaRouteOwner p={0};
    CHECK(elden_touch_route_attach(&p,&io,37,1ULL<<40,(1ULL<<40)+16384,16384)==-22);
    CHECK(!m.writes&&!m.held);
}
static void write_failures(const char *fixture){
    for(unsigned failure=1;failure<=3;failure++)for(unsigned style=0;style<3;style++){
        fresh(fixture);EldenTouchDmaRouteOwner p={0};m.fail_write=failure;
        m.silent_drop=style==1;m.applied_error=style==2;
        int result=attach(&p);
        if(failure>2){CHECK(!result);result=elden_touch_route_detach(&p,37);}
        CHECK(result!=0&&p.poisoned&&p.claimed&&m.held&&!m.drop&&m.writes==failure);
        unsigned writes=m.writes;CHECK(elden_touch_route_detach(&p,37)==-16&&m.writes==writes);
        CHECK(attach(&p)==-16);
        // Test teardown only. Production keeps this lease through real reset.
    }
    fresh(fixture);EldenTouchDmaRouteOwner p={0};CHECK(!attach(&p));m.busy=true;
    CHECK(elden_touch_route_detach(&p,37)!=0&&p.poisoned&&m.writes==2&&m.held);
    fresh(fixture);memset(&p,0,sizeof(p));CHECK(!attach(&p));m.regs[0xc00/4]^=1;
    CHECK(elden_touch_route_observe(&p,37)!=0&&p.poisoned&&m.held&&m.writes==2);
}
int main(int argc,char **argv){
    CHECK(argc==2);successful(argv[1]);refused(argv[1]);write_failures(argv[1]);
    printf("Touch SMMU route: %u assertions using captured v25 globals; CB0 and failures modeled\n",checks);
}
