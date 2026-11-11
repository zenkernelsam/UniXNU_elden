/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaRoute.h"
#include "EldenTouchBoard.h"
#define SLOT_SMR (0x800u+4*ELDEN_TOUCH_DMA_SLOT)
#define SLOT_S2CR (0xc00u+4*ELDEN_TOUCH_DMA_SLOT)
#define NEW_SMR (0x80000000u|ELDEN_TOUCH_SE_DMA_SID)
static int reject(EldenTouchDmaRouteOwner *p,int error){
    p->error=error;if(p->writes)p->poisoned=true;return error;
}
static bool span(uint64_t a,size_t n,unsigned bits){
    return a&&n&&bits&&bits<64&&a<(UINT64_C(1)<<bits)&&n<=(UINT64_C(1)<<bits)-a;
}
/* 0=boot baseline, 1=owned active route, 2=owned invalid SMR tombstone. */
static int snapshot(EldenTouchDmaRouteOwner *p,unsigned state){
    const EldenTouchRouteIo *io=&p->io;
    if(io->read(io->context,0)!=ELDEN_TOUCH_DMA_CR0||
       io->read(io->context,0x20)!=ELDEN_TOUCH_DMA_ID0||
       io->read(io->context,0x24)!=ELDEN_TOUCH_DMA_ID1||
       io->read(io->context,0x28)!=ELDEN_TOUCH_DMA_ID2)return -95;
    /* Global fault status is observed, never acknowledged or suppressed. */
    if(io->read(io->context,0x48))return -5;
    const uint32_t active[]=ELDEN_TOUCH_DMA_ACTIVE_SMR;
    for(unsigned i=0;i<ELDEN_TOUCH_DMA_SLOTS;i++){
        uint32_t smr=i<7?active[i]:0,s2cr=i<7?i:ELDEN_TOUCH_DMA_UNUSED_S2CR;
        if(state&&i==ELDEN_TOUCH_DMA_SLOT){smr=state==1?NEW_SMR:0;s2cr=ELDEN_TOUCH_DMA_BANK;}
        if(io->read(io->context,0x800+4*i)!=smr||io->read(io->context,0xc00+4*i)!=s2cr)return -16;
    }
    for(unsigned i=0;i<ELDEN_TOUCH_DMA_BANKS;i++){
        if(io->read(io->context,0x1000+4*i)!=(i<7?0x1f000u:0)||
           io->read(io->context,0x1800+4*i)!=(i<7?1u:0))return -16;
    }
    /* Translation enable is SCTLR.M, not the entire SCTLR. Stock preserves
     * other control bits (UFS live evidence includes e0). Never rewrite them.
     * Capture the complete value once and require it unchanged thereafter. */
    uint32_t sctlr=io->read(io->context,ELDEN_TOUCH_DMA_CB0_OFFSET);
    if(sctlr==UINT32_MAX||(sctlr&1)){p->cb0_sctlr=sctlr;return -95;}
    if(p->cb0_seen&&sctlr!=p->cb0_sctlr)return -16;
    p->cb0_sctlr=sctlr;p->cb0_seen=true;
    const unsigned widths[]={32,36,40,42,44,48};
    for(unsigned shift=0;shift<=8;shift+=4){
        unsigned code=(ELDEN_TOUCH_DMA_ID2>>shift)&15;
        if(code>=6||!span(p->tx,p->bytes,widths[code])||!span(p->rx,p->bytes,widths[code]))return -22;
    }
    return io->idle(io->context);
}
static int put(EldenTouchDmaRouteOwner *p,unsigned offset,uint32_t value){
    if(offset!=SLOT_SMR&&offset!=SLOT_S2CR)return reject(p,-22);
    ++p->writes;int rc=p->io.write(p->io.context,offset,value);
    if(rc)return reject(p,rc);
    if(p->io.read(p->io.context,offset)!=value)return reject(p,-5);
    return 0;
}
int elden_touch_route_attach(EldenTouchDmaRouteOwner *p,const EldenTouchRouteIo *io,
                            uint64_t epoch,uint64_t tx,uint64_t rx,size_t bytes){
    if(!p||!io||!io->read||!io->write||!io->claim||!io->drop||!io->idle||!epoch||
       !tx||!rx||!bytes||bytes>UINT64_MAX-tx||bytes>UINT64_MAX-rx||
       !(tx+bytes<=rx||rx+bytes<=tx))return -22;
    if(p->attempted||p->claimed||p->attached||p->poisoned)return -16;
    p->io=*io;p->epoch=epoch;p->tx=tx;p->rx=rx;p->bytes=bytes;p->attempted=true;p->stage=1;
    int rc=io->claim(io->context,epoch);if(rc)return reject(p,rc);
    p->claimed=true;rc=snapshot(p,false);
    if(rc){io->drop(io->context,epoch);p->claimed=false;return reject(p,rc);}
    p->stage=2;
    rc=put(p,SLOT_S2CR,ELDEN_TOUCH_DMA_BANK);if(rc)return rc;
    p->stage=3;rc=put(p,SLOT_SMR,NEW_SMR);if(rc)return rc;
    p->attached=true;p->stage=4;rc=snapshot(p,true);if(rc)return reject(p,rc);
    p->stage=5;p->error=0;return 0;
}
int elden_touch_route_observe(EldenTouchDmaRouteOwner *p,uint64_t epoch){
    if(!p||!p->claimed||!p->attached||p->poisoned||epoch!=p->epoch)return -16;
    int rc=snapshot(p,true);return rc?reject(p,rc):0;
}
int elden_touch_route_detach(EldenTouchDmaRouteOwner *p,uint64_t epoch){
    int rc=elden_touch_route_observe(p,epoch);if(rc)return rc;
    p->stage=6;rc=put(p,SLOT_SMR,0);if(rc)return rc;
    /* v28 hardware: writing the firmware's invalid-slot reset readout
     * 0300006e aborts. After SMR invalidation leave the proven S2CR value0;
     * no stream matches it. Never replay opaque/reserved reset readouts. */
    p->stage=7;rc=snapshot(p,2);if(rc)return reject(p,rc);
    p->attached=false;p->detached=true;p->stage=8;
    p->io.drop(p->io.context,p->epoch);p->claimed=false;p->error=0;return 0;
}
