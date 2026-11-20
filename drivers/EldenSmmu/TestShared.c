/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual shared batch, original4K programmer/graph and touch route checks. */
#include "Shared.h"
#include "DmaRoute.h"
#include "EldenTouchBoard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
/* Test-only full platform reset, no production registry reset API. */
#include "BootRoute.c"
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"shared line%u %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint32_t regs[0x100000/4];uint64_t clock;unsigned read_fail,write_fail,reads,writes,applied_error,early_closed,route_corrupt;
    uint32_t irq[3],quiesced[3],memory[3],touch_held;EldenSmmuShared shared;EldenSmmuSharedConfig cfg;
    uint64_t pool[3][4096];EldenSmmuSharedMemory frames[3];} Model;
static Model m;
static EldenTouchDmaRouteOwner touch;
static int admit(void *p,uint64_t owner,uint64_t epoch,int cleanup){CHECK(p==&m&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int early(void *p,uint64_t owner,uint64_t epoch){CHECK(p==&m&&owner==7&&epoch==9);return m.early_closed?-16:0;}
static int idle(void *p,uint64_t owner,uint64_t epoch,unsigned group){CHECK(p==&m&&owner==7&&epoch==9&&group<3);return m.quiesced[group]?0:-16;}
static int memory(void *p,uint64_t owner,uint64_t epoch,unsigned group,const EldenSmmuSharedMemory *b,int cleanup){
    CHECK(p==&m&&owner==7&&epoch==9&&group<3&&b&&cleanup<=1);return m.memory[group]?0:-13;}
static int irq(void *p,uint64_t owner,uint64_t epoch,unsigned group,uint32_t bank,int action){CHECK(p==&m&&owner==7&&epoch==9&&group<3&&bank>=7&&bank<83&&action>=0&&action<=3);
    if(action==1){CHECK(!m.irq[group]);m.irq[group]=1;}else if(!action)m.irq[group]=0;else if(action==2&&!m.irq[group])return -19;return 0;}
static uint64_t now(void *p){CHECK(p==&m);return m.clock;}
static int bank(void *p,uint64_t owner,uint64_t epoch,unsigned group,const EldenSmmuSharedLease *lease,int cleanup){
    CHECK(p==&m&&owner==7&&epoch==9&&group<3&&lease&&lease->bank>=7&&lease->asid&&lease->route_epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int rd32(void *p,size_t off,uint32_t *out){CHECK(p==&m&&off<sizeof(m.regs)&&!(off&3));m.reads++;m.clock++;
    if(m.read_fail==m.reads)return -5;*out=m.regs[off/4];return 0;}
static int rd64(void *p,size_t off,uint64_t *out){CHECK(p==&m&&off<sizeof(m.regs)-4&&!(off&7));m.reads++;m.clock++;
    if(m.read_fail==m.reads)return -5;*out=(uint64_t)m.regs[off/4]|((uint64_t)m.regs[off/4+1]<<32);return 0;}
static int wr32(void *p,size_t off,uint32_t value){CHECK(p==&m&&off<sizeof(m.regs)&&!(off&3));m.writes++;m.clock++;
    if(m.write_fail==m.writes&&!m.applied_error)return -5;
    if(off>=0x80000&&off%4096==0x58)m.regs[off/4]&=~value;else m.regs[off/4]=value;
    if(off==0x820&&value==0x80001c00){
        if(m.route_corrupt==1)m.regs[(0x87000+0x20)/4]^=0x1000;
        if(m.route_corrupt==2)m.pool[1][0]^=0x1000;
    }
    return m.write_fail==m.writes?-5:0;}
static int wr64(void *p,size_t off,uint64_t value){CHECK(p==&m&&off<sizeof(m.regs)-4&&!(off&7));m.writes++;m.clock++;
    if(m.write_fail==m.writes&&!m.applied_error)return -5;
    m.regs[off/4]=(uint32_t)value;m.regs[off/4+1]=(uint32_t)(value>>32);return m.write_fail==m.writes?-5:0;}
static int barrier(void *p){CHECK(p==&m);return 0;}
static int peer_route(void *p,uint64_t owner,uint64_t epoch,uint32_t offset,uint32_t base,uint32_t *expected){CHECK(p==&m&&owner==7&&epoch==9);
    return elden_touch_route_expected(&touch,37,offset,base,expected);}
static int descriptor(void *p,uint64_t pa,uint64_t *out){CHECK(p==&m&&out);
    for(unsigned group=0;group<3;group++){uint64_t base=UINT64_C(0x980000000)+group*0x100000;
        if(pa>=base&&pa-base<sizeof(m.pool[group])){*out=m.pool[group][(pa-base)/8];return 0;}}
    return -14;}
static void reset(unsigned groups){cases++;memset(&m,0,sizeof(m));state=EldenSmmuBootOpen;owner=NULL;intended=published=(EldenSmmuBootRoute){0};
    memset(planned_set,0,sizeof(planned_set));memset(published_set,0,sizeof(published_set));set_epoch=set_count=set_mode=reservation_ready=0;memset(&touch,0,sizeof(touch));
    m.regs[0]=ELDEN_TOUCH_DMA_CR0;m.regs[0x20/4]=ELDEN_TOUCH_DMA_ID0;m.regs[0x24/4]=ELDEN_TOUCH_DMA_ID1;m.regs[0x28/4]=ELDEN_TOUCH_DMA_ID2;
    const uint32_t active[]=ELDEN_TOUCH_DMA_ACTIVE_SMR;
    for(unsigned slot=0;slot<132;slot++){m.regs[(0x800+4*slot)/4]=slot<7?active[slot]:0;m.regs[(0xc00+4*slot)/4]=slot<7?slot:ELDEN_TOUCH_DMA_UNUSED_S2CR;}
    for(unsigned bank=0;bank<83;bank++){m.regs[(0x1000+4*bank)/4]=bank<7?0x1f000:0;m.regs[(0x1800+4*bank)/4]=bank<7?1:0;}
    m.regs[0x80000/4]=0xe0;m.cfg=(EldenSmmuSharedConfig){7,9,groups,4,{{0x20000000,0x800000},{0x30000000,0x800000},{0x40000000,0x800000}}};
    for(unsigned i=0;i<3;i++){m.quiesced[i]=m.memory[i]=1;EldenSmmuSharedMemory *b=&m.frames[i];
        b->config=(EldenWifiSmmu4KConfig){32,36,UINT64_C(0x980000000)+i*0x100000,2048};
        b->ranges[0]=(EldenWifiSmmu4KRange){m.cfg.apertures[i].iova,UINT64_C(0x900000000)+i*0x1000000,0x800000,3,0,3};
        b->range_count=1;b->capacity_pages=8;b->io=(EldenWifiSmmu4KTableIo){&m,descriptor};
        b->data_physical=b->ranges[0].physical;b->data_bytes=b->ranges[0].bytes;
        CHECK(!EldenWifiSmmu4KBuild(&b->config,b->ranges,b->range_count,m.pool[i],8,&b->tables));}
}
static int open(void){EldenSmmuSharedOps ops={&m,admit,early,idle,memory,irq,peer_route,now,{&m,0x100000,rd32,rd64,wr32,wr64,barrier},bank};
    return elden_smmu_shared_open(&m.shared,&m.cfg,&ops);}
static void install_all(void){for(unsigned group=0;group<3;group++)if(m.cfg.groups&(1u<<group))CHECK(!elden_smmu_shared_install(&m.shared,7,9,group,&m.frames[group]));}
static uint32_t touch_read(void *p,unsigned off){CHECK(p==&m&&m.touch_held&&off<sizeof(m.regs));return m.regs[off/4];}
static int touch_write(void *p,unsigned off,uint32_t value){CHECK(p==&m&&m.touch_held&&(off==0xc1c||off==0x81c));m.regs[off/4]=value;return 0;}
static int touch_claim(void *p,uint64_t epoch){CHECK(p==&m&&epoch==37&&!m.touch_held);m.touch_held=1;return 0;}
static void touch_drop(void *p,uint64_t epoch){CHECK(p==&m&&epoch==37&&m.touch_held);m.touch_held=0;}
static int touch_idle(void *p){CHECK(p==&m&&m.touch_held);return 0;}
static void attach_touch(void){EldenTouchRouteIo io={&m,touch_read,touch_write,touch_claim,touch_drop,touch_idle};
    CHECK(!elden_touch_route_attach(&touch,&io,37,0x180000000,0x180004000,16384));CHECK(!elden_touch_route_observe(&touch,37));
    for(unsigned group=0;group<3;group++)if(m.cfg.groups&(1u<<group))CHECK(!elden_smmu_shared_observe(&m.shared,7,9,group,0));
    CHECK(!elden_touch_route_detach(&touch,37));CHECK(m.regs[0x80000/4]==0xe0&&m.regs[0x81c/4]==0&&m.regs[0xc1c/4]==0);}
static void normal(void){for(unsigned groups=1;groups<8;groups++){
    reset(groups);CHECK(!open());EldenSmmuSharedStatus status;CHECK(!elden_smmu_shared_status(&m.shared,&status));
    CHECK(status.route_count==((groups&1)!=0)+((groups&2)!=0)+((groups&4)?4u:0u));
    install_all();CHECK(!elden_smmu_shared_commit(&m.shared,7,9));CHECK(!elden_smmu_shared_status(&m.shared,&status)&&status.active==groups&&status.translation_mode==2);
    CHECK(!elden_smmu_boot_route_seal());for(unsigned group=0;group<3;group++)if(groups&(1u<<group))CHECK(!elden_smmu_shared_observe(&m.shared,7,9,group,0));
    attach_touch();for(unsigned group=0;group<3;group++)if(groups&(1u<<group)){
        CHECK(!elden_smmu_shared_observe(&m.shared,7,9,group,0));}
    }
    reset(7);CHECK(!open());install_all();CHECK(!elden_smmu_shared_commit(&m.shared,7,9));CHECK(!elden_smmu_boot_route_seal());
    for(unsigned group=0;group<3;group++){CHECK(!elden_smmu_shared_retire(&m.shared,7,9,group));CHECK(!elden_smmu_shared_retire(&m.shared,7,9,group));
        CHECK(m.regs[(0x80000+4096*(7+group))/4]==0);}
}
static void refusals(void){reset(3);m.cfg.apertures[1]=m.cfg.apertures[0];CHECK(open()==-22&&!m.writes);
    reset(2);m.early_closed=1;CHECK(open()==-16&&!m.writes);reset(2);m.regs[0x80000/4]|=1;CHECK(open()==-95&&!m.writes);
    reset(2);m.regs[0x800/4]^=1;CHECK(open()==-116&&!m.writes);reset(2);m.cfg.groups=8;CHECK(open()==-22&&!m.writes);
    reset(4);m.cfg.sensor_session=2;CHECK(open()==-22&&!m.writes);reset(3);CHECK(!open());
    CHECK(!elden_smmu_shared_install(&m.shared,7,9,0,&m.frames[0]));m.frames[1].ranges[0].physical=m.frames[0].ranges[0].physical;
    CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])==-22);CHECK(elden_smmu_shared_commit(&m.shared,7,9)==-16);
    reset(2);CHECK(!open());CHECK(!elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1]));m.pool[1][0]^=0x1000;
    CHECK(elden_smmu_shared_commit(&m.shared,7,9)<0&&elden_smmu_boot_route_state()==EldenSmmuBootPoisoned);
}
static void failures(void){reset(2);CHECK(!open());install_all();unsigned actual_writes=m.writes;
    for(unsigned applied=0;applied<2;applied++)for(unsigned fail=1;fail<=actual_writes;fail++){
    reset(2);CHECK(!open());m.write_fail=fail;m.applied_error=applied;CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])<0);
    CHECK(elden_smmu_boot_route_state()==EldenSmmuBootPoisoned);CHECK(!elden_smmu_shared_admit(&m.shared,7,9,1,1));}
    for(unsigned applied=0;applied<2;applied++)for(unsigned fail=1;fail<=2;fail++){
        reset(2);CHECK(!open());install_all();m.write_fail=m.writes+fail;m.applied_error=applied;CHECK(elden_smmu_shared_commit(&m.shared,7,9)<0);
        CHECK(elden_smmu_boot_route_state()==EldenSmmuBootPoisoned);}
    reset(2);CHECK(!open());install_all();CHECK(!elden_smmu_shared_commit(&m.shared,7,9));m.read_fail=m.reads+1;
    CHECK(elden_smmu_shared_observe(&m.shared,7,9,1,0)==-5);CHECK(!elden_smmu_shared_admit(&m.shared,7,9,1,1));
}
static unsigned go;static int batch_rc,seal_rc;static EldenSmmuBootRoute batch[6];
static void *batch_reserve(void *p){while(!__atomic_load_n(&go,__ATOMIC_ACQUIRE)){}batch_rc=elden_smmu_boot_routes_reserve(p,9,batch,6);return NULL;}
static void *batch_seal(void *p){(void)p;while(!__atomic_load_n(&go,__ATOMIC_ACQUIRE)){}seal_rc=elden_smmu_boot_route_seal();return NULL;}
static void batch_tests(void){
    for(unsigned session=3;session<=8;session++){
        reset(7);for(unsigned i=0;i<6;i++)batch[i]=(EldenSmmuBootRoute){8+i,i<2?7+i:9,0x80000000u|(i==0?0x1401:i==1?0x1c00:(unsigned[]){0x1000,0x1040,0x1060,0x1080}[i-2]+session),i<2?7+i:9,0x1f300,1};
        CHECK(!elden_smmu_boot_routes_reserve(&m,9,batch,6));CHECK(!elden_smmu_boot_routes_commit(&m,9,batch,6));CHECK(!elden_smmu_boot_route_seal());
        uint32_t value;CHECK(!elden_smmu_boot_routes_peek(&m,9,0x800+4*13,0,&value)&&value==batch[5].smr);
        CHECK(elden_smmu_boot_routes_peek(&m,10,0x820,0,&value)==-16);CHECK(elden_smmu_boot_route_commit(&m,batch)==-16);
    }
    for(unsigned test=0;test<11;test++){
        reset(7);EldenSmmuBootRoute bad[6];memcpy(bad,batch,sizeof(bad));unsigned count=6;
        if(test==0)bad[0].smr=0x80001402;if(test==1)bad[0].smr|=0x10000;
        if(test==2)bad[1].slot=bad[0].slot;if(test==3)bad[1].bank=bad[0].bank;
        if(test==4)bad[5].bank++;if(test==5)bad[5].smr=0x80001005;
        if(test==6)count=5;if(test==7)bad[2].smr=0x80001001;
        if(test==8)bad[4].smr=bad[3].smr;if(test==9)bad[0].slot=7;if(test==10)bad[0].bank=6;
        CHECK(elden_smmu_boot_routes_reserve(&m,9,bad,count)==-22&&elden_smmu_boot_route_state()==EldenSmmuBootOpen);
    }
    for(unsigned i=0;i<100;i++){
        reset(7);go=0;batch_rc=seal_rc=99;pthread_t a,b;
        CHECK(!pthread_create(&a,NULL,batch_reserve,&m));CHECK(!pthread_create(&b,NULL,batch_seal,NULL));
        __atomic_store_n(&go,1u,__ATOMIC_RELEASE);CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(b,NULL));
        CHECK((batch_rc==0&&seal_rc==-16)||(batch_rc==-16&&seal_rc==0));
        if(!batch_rc){CHECK(!elden_smmu_boot_routes_commit(&m,9,batch,6));CHECK(!elden_smmu_boot_route_seal());}
        uint32_t value;CHECK(!elden_smmu_boot_route_expected(0x820,0,&value)&&value==(batch_rc?0:batch[0].smr));
    }
}
static void fresh_tests(void){
    reset(2);CHECK(!open());m.frames[1].ranges[0].physical=UINT64_MAX&~UINT64_C(4095);CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])==-22&&!m.writes);
    reset(2);CHECK(!open());m.frames[1].ranges[0].physical=m.frames[1].config.table_physical;CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])==-22&&!m.writes);
    reset(2);CHECK(!open());m.regs[0x820/4]=0x80001234;CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])<0&&!m.writes);
    reset(2);CHECK(!open());m.regs[(0x80000+4096*8)/4]=1;m.regs[(0x1000+4*8)/4]=0x1f300;m.regs[(0x1800+4*8)/4]=1;
    m.regs[(0x80000+4096*8+0x24)/4]=1u<<16;CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])<0&&!m.writes);
    reset(3);CHECK(!open());CHECK(!elden_smmu_shared_install(&m.shared,7,9,0,&m.frames[0]));
    /* A peer allocation's unmapped guard still owns its PA. The new camera
     * range does not overlap the Wi-Fi leaves, but its retained backing does. */
    m.frames[1].data_physical=m.frames[0].data_physical;m.frames[1].data_bytes=0x1800000;
    unsigned before=m.writes;CHECK(elden_smmu_shared_install(&m.shared,7,9,1,&m.frames[1])==-22&&m.writes==before);
    reset(2);CHECK(!open());install_all();CHECK(!elden_smmu_shared_commit(&m.shared,7,9));CHECK(!elden_smmu_boot_route_seal());
    EldenSmmuSharedLease lease;CHECK(elden_smmu_shared_lease(&m.shared,7,9,1,&lease)==-16);CHECK(elden_smmu_shared_early(&m.shared,7,9)==-16);
    for(unsigned corrupt=1;corrupt<=2;corrupt++){
        reset(2);CHECK(!open());install_all();m.route_corrupt=corrupt;
        CHECK(elden_smmu_shared_commit(&m.shared,7,9)<0&&elden_smmu_boot_route_state()==EldenSmmuBootPoisoned);
        CHECK(elden_smmu_shared_admit(&m.shared,7,9,1,0)<0);
    }
}
int main(void){normal();refusals();failures();batch_tests();fresh_tests();printf("PASS sharedSMMU: %u scenarios/%u checks; actual batch/4K/touch, physical/MMIO/IRQ peers modeled\n",cases,checks);return 0;}
