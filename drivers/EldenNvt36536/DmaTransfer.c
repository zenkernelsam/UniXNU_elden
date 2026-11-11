/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Wrap the pinned, hardware-used sheng GENI SE-DMA core. Board isolation and
 * caller-output publication are stricter than the reusable core's contract. */
#include "DmaTransfer.h"
#include "EldenTouchResources.h"
#include <string.h>
#define ACTIVE_MASK ((1u<<0)|(1u<<12))
#define MAX_POLL_COUNT 1000000u
static bool range(uint64_t base,size_t size){return base&&size&&size<=UINT64_MAX-base;}
static bool disjoint(uint64_t a,size_t an,uint64_t b,size_t bn){
    return range(a,an)&&range(b,bn)&&(a+an<=b||b+bn<=a);
}
static bool cpu_separate(const void *a,size_t an,const void *b,size_t bn){
    return disjoint((uint64_t)(uintptr_t)a,an,(uint64_t)(uintptr_t)b,bn);
}
static int layout(const struct geni_spi *s,const EldenTouchDmaContract *c){
    if(!s||!c||!s->se.rd||!s->se.wr||!s->se.delay_us||!s->use_dma||
       !s->cur_speed_hz||s->cur_speed_hz>10000000u||(s->cur_bpw!=0&&s->cur_bpw!=8)||s->last_mode||s->last_cs||
       (s->poll_only!=1&&s->poll_only!=2)||!s->poll_limit||s->poll_limit>MAX_POLL_COUNT||
       c->sid!=ELDEN_TOUCH_SE_DMA_SID||!c->epoch||!c->check||!c->staging||!c->staging_bytes)
        return GENI_ERR_PARAM;
    const struct geni_dma *d=&s->dma;
    size_t line=c->cache_line_bytes;
    if(line<32||line>4096||(line&(line-1))||
       ((uintptr_t)d->tx_virt&(line-1))||((uintptr_t)d->rx_virt&(line-1))||
       (d->size&(line-1)))return GENI_ERR_PARAM;
    if(!d->tx_virt||!d->rx_virt||!d->clean||!d->clean_inval||!d->inval||
       !disjoint(d->tx_phys,d->size,d->rx_phys,d->size)||
       !cpu_separate(d->tx_virt,d->size,d->rx_virt,d->size)||
       !cpu_separate(c->staging,c->staging_bytes,d->tx_virt,d->size)||
       !cpu_separate(c->staging,c->staging_bytes,d->rx_virt,d->size))return GENI_ERR_PARAM;
    return 0;
}
/* Compare fields, not padding or per-transfer diagnostic counters. A later
 * setup/buffer/callback change requires a new externally qualified epoch. */
static bool unchanged(const struct geni_spi *a,const struct geni_spi *b){
    return a&&a->se.ctx==b->se.ctx&&a->se.rd==b->se.rd&&a->se.wr==b->se.wr&&
        a->se.delay_us==b->se.delay_us&&a->use_dma==b->use_dma&&
        a->cur_bpw==b->cur_bpw&&a->cur_speed_hz==b->cur_speed_hz&&
        a->last_mode==b->last_mode&&a->last_cs==b->last_cs&&
        a->poll_limit==b->poll_limit&&a->poll_only==b->poll_only&&
        a->dma.ctx==b->dma.ctx&&a->dma.clean==b->dma.clean&&
        a->dma.clean_inval==b->dma.clean_inval&&a->dma.inval==b->dma.inval&&
        a->dma.tx_virt==b->dma.tx_virt&&a->dma.rx_virt==b->dma.rx_virt&&
        a->dma.tx_phys==b->dma.tx_phys&&a->dma.rx_phys==b->dma.rx_phys&&a->dma.size==b->dma.size;
}
static int idle(struct geni_spi *s){
    uint32_t state=s->se.rd(s->se.ctx,SE_GENI_STATUS);
    uint32_t dma=s->se.rd(s->se.ctx,SE_GENI_DMA_MODE_EN);
    if(state==UINT32_MAX||dma!=GENI_DMA_MODE_EN||state&ACTIVE_MASK||
       s->se.rd(s->se.ctx,SE_GSI_EVENT_EN)||
       (s->se.rd(s->se.ctx,SE_GENI_TX_FIFO_STATUS)&GENI_RX_WC_MASK)||
       (s->se.rd(s->se.ctx,SE_GENI_RX_FIFO_STATUS)&GENI_RX_WC_MASK))return GENI_ERR_IO;
    return 0;
}
static int completion(struct geni_spi *s){
    if((s->se.rd(s->se.ctx,SE_DMA_TX_IRQ_STAT)&(GENI_DMA_AHB_ERR|GENI_DMA_RESET_DONE))||
       (s->se.rd(s->se.ctx,SE_DMA_RX_IRQ_STAT)&(GENI_DMA_AHB_ERR|GENI_DMA_RESET_DONE))||
       (s->se.rd(s->se.ctx,SE_GENI_M_IRQ_STATUS)&GENI_M_ERRORS))return GENI_ERR_IO;
    return 0;
}
static int check(EldenTouchDmaTransfer *t,enum EldenTouchDmaCheck phase){
    struct geni_dma *d=&t->engine->dma;EldenTouchDmaContract *c=&t->contract;
    return c->check(c->context,phase,c->epoch,c->sid,d->tx_phys,d->rx_phys,d->size);
}
int elden_touch_dma_attach(EldenTouchDmaTransfer *t,struct geni_spi *s,const EldenTouchDmaContract *c){
    if(!t)return GENI_ERR_PARAM;
    /* Attach only once per externally owned/reset engine epoch. */
    if(t->initialized||t->active||t->quarantined)return GENI_ERR_IO;
    int rc=layout(s,c);if(rc)return rc;
    rc=idle(s);if(rc)return rc;
    EldenTouchDmaTransfer next={0};next.engine=s;next.qualified_engine=*s;next.contract=*c;
    rc=check(&next,EldenTouchDmaBefore);if(rc)return rc;
    if(!unchanged(s,&next.qualified_engine))return GENI_ERR_IO;
    next.initialized=true;*t=next;return 0;
}
int elden_touch_dma_transfer(EldenTouchDmaTransfer *t,const uint8_t *tx,uint8_t *rx,size_t count){
    if(!t||!t->initialized||!count||(!tx&&!rx))return GENI_ERR_PARAM;
    if(t->active||t->quarantined)return GENI_ERR_IO;
    struct geni_spi *s=t->engine;EldenTouchDmaContract *c=&t->contract;
    if(!unchanged(s,&t->qualified_engine)){
        t->last_error=GENI_ERR_IO;t->quarantined=true;return GENI_ERR_IO;
    }
    int rc=layout(s,c);if(rc)return rc;
    if(count>s->dma.size||count>GENI_TRANS_LEN_MASK||(rx&&count>c->staging_bytes))return GENI_ERR_PARAM;
    /* The external caller may use tx==rx (ordinary full duplex). Neither
     * caller buffer may alias the private DMA or publication buffers. */
    if((tx&&(!cpu_separate(tx,count,c->staging,c->staging_bytes)||
              !cpu_separate(tx,count,s->dma.tx_virt,s->dma.size)||!cpu_separate(tx,count,s->dma.rx_virt,s->dma.size)))||
       (rx&&(!cpu_separate(rx,count,c->staging,c->staging_bytes)||
              !cpu_separate(rx,count,s->dma.tx_virt,s->dma.size)||!cpu_separate(rx,count,s->dma.rx_virt,s->dma.size))))return GENI_ERR_PARAM;
    t->active=true;
    rc=idle(s);if(!rc)rc=check(t,EldenTouchDmaBefore);
    if(!rc&&!unchanged(s,&t->qualified_engine))rc=GENI_ERR_IO;
    if(rc){t->active=false;t->last_error=rc;t->quarantined=true;return rc;}
    rc=geni_spi_transfer(s,tx,rx?c->staging:NULL,count,0);
    if(!rc)rc=idle(s);
    if(!rc)rc=completion(s);
    if(!rc)rc=check(t,EldenTouchDmaAfter);
    /* Canonical init deliberately leaves cur_bpw=0; the first transfer
     * writes the 8-bit packing and then publishes cur_bpw=8. Permit only
     * that source-defined transition, after a successful transaction. */
    if(!rc&&t->qualified_engine.cur_bpw==0&&s->cur_bpw==8)t->qualified_engine.cur_bpw=8;
    if(!rc&&!unchanged(s,&t->qualified_engine))rc=GENI_ERR_IO;
    t->active=false;t->last_error=rc;
    /* Canonical core may issue CANCEL/ABORT/FSM_RESET during recovery, but
     * it does not prove the reset is complete. Do not reuse that session. */
    if(rc){t->quarantined=true;return rc;}
    if(rx)memcpy(rx,c->staging,count);
    ++t->completed;return 0;
}
