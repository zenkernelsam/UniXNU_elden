/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Abi.h"
static void clear(void *p,size_t n){uint8_t *b=p;for(size_t i=0;i<n;i++)b[i]=0;}
int elden_actuator_ring_init(EldenActuatorRing *r,void *memory,size_t bytes,uint64_t anchor,uint64_t owner,uint64_t epoch){
    if(!r||!memory||bytes!=ELDEN_ACTUATOR_BYTES||!anchor||!owner||!epoch||((uintptr_t)memory&7)||r->page)return -22;
    clear(memory,bytes);r->page=memory;r->slots=(void *)((uint8_t *)memory+ELDEN_ACTUATOR_HEADER_BYTES);
    r->anchor=anchor;r->owner=owner;r->epoch=epoch;r->taken=0;r->generation=1;
    for(unsigned i=0;i<ELDEN_ACTUATOR_RING_FRAMES;i++)r->slots[i].frame=UINT64_MAX;
    EldenActuatorPage *p=r->page;p->version=ELDEN_ACTUATOR_VERSION;p->rate=ELDEN_ACTUATOR_RATE;p->ring_frames=ELDEN_ACTUATOR_RING_FRAMES;
    p->io_frames=ELDEN_ACTUATOR_IO_FRAMES;p->safety_frames=ELDEN_ACTUATOR_SAFETY_FRAMES;p->take_frames=ELDEN_ACTUATOR_TAKE_FRAMES;
    p->anchor_host=anchor;p->ticks_per_frame=400;p->owner=owner;p->epoch=epoch;
    p->generation=1;
    __atomic_store_n(&p->magic,ELDEN_ACTUATOR_MAGIC,__ATOMIC_RELEASE);return 0;
}
int elden_actuator_ring_check(const EldenActuatorRing *r){
    if(!r||!r->page||!r->slots)return -22;const EldenActuatorPage *p=r->page;
    if(__atomic_load_n(&p->magic,__ATOMIC_ACQUIRE)!=ELDEN_ACTUATOR_MAGIC||p->version!=1||p->rate!=48000||p->ring_frames!=16384||
       p->io_frames!=256||p->safety_frames!=128||p->take_frames!=64||p->ticks_per_frame!=400||p->anchor_host!=r->anchor||p->owner!=r->owner||p->epoch!=r->epoch)return -84;
    return 0;
}
int elden_actuator_ring_bind(EldenActuatorRing *r,void *memory,size_t bytes,uint64_t owner,uint64_t epoch){
    if(!r||!memory||bytes!=ELDEN_ACTUATOR_BYTES||((uintptr_t)memory&7)||!owner||!epoch||r->page)return -22;
    EldenActuatorRing value={0};value.page=memory;value.slots=(void *)((uint8_t *)memory+4096);
    value.owner=owner;value.epoch=epoch;value.anchor=value.page->anchor_host;
    if(!value.anchor||elden_actuator_ring_check(&value))return -84;value.taken=__atomic_load_n(&value.page->taken,__ATOMIC_ACQUIRE);
    value.generation=__atomic_load_n(&value.page->generation,__ATOMIC_ACQUIRE);if(!value.generation)return -84;*r=value;return 0;
}
int elden_actuator_ring_write(EldenActuatorRing *r,uint64_t frame,const int16_t *input,size_t count){
    if(!input||!count||count>2048||frame>UINT64_MAX-count)return -22;int rc=elden_actuator_ring_check(r);if(rc)return rc;
    uint64_t taken=__atomic_load_n(&r->page->taken,__ATOMIC_ACQUIRE);
    uint64_t generation=__atomic_load_n(&r->page->generation,__ATOMIC_ACQUIRE);
    if(!generation)return -84;if(generation!=r->generation)return -116;
    if(frame<taken||frame-taken>ELDEN_ACTUATOR_RING_FRAMES-count)return -116;
    for(size_t i=0;i<count;i++){
        EldenActuatorSlot *s=&r->slots[(frame+i)&(ELDEN_ACTUATOR_RING_FRAMES-1)];uint32_t seq=__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE);
        if(seq&1)return -16;
        if(!__atomic_compare_exchange_n(&s->seq,&seq,seq+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
        int same=__atomic_load_n(&s->frame,__ATOMIC_RELAXED)==frame+i&&__atomic_load_n(&s->generation,__ATOMIC_RELAXED)==generation;
        int32_t value=same?__atomic_load_n(&s->sample_q15,__ATOMIC_RELAXED):0;
        if(value<-32767||value>32767){__atomic_store_n(&s->seq,seq+2,__ATOMIC_RELEASE);return -84;}
        value+=input[i];
        if(value>32767)value=32767;if(value<-32767)value=-32767;
        __atomic_store_n(&s->sample_q15,value,__ATOMIC_RELAXED);__atomic_store_n(&s->frame,frame+i,__ATOMIC_RELAXED);
        __atomic_store_n(&s->generation,generation,__ATOMIC_RELAXED);__atomic_store_n(&s->seq,seq+2,__ATOMIC_RELEASE);
    }
    uint64_t end=frame+count,written=__atomic_load_n(&r->page->written,__ATOMIC_RELAXED);
    if(end>written)__atomic_store_n(&r->page->written,end,__ATOMIC_RELEASE);
    __atomic_fetch_add(&r->page->writes,1u,__ATOMIC_RELAXED);return 0;
}
int elden_actuator_ring_read(EldenActuatorRing *r,uint64_t frame,int16_t *out){
    if(!out||frame<r->taken)return -22;int rc=elden_actuator_ring_check(r);if(rc)return rc;
    EldenActuatorSlot *s=&r->slots[frame&(ELDEN_ACTUATOR_RING_FRAMES-1)];
    for(unsigned i=0;i<3;i++){
        uint32_t first=__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE);if(first&1)continue;
        uint64_t stamp=__atomic_load_n(&s->frame,__ATOMIC_RELAXED),generation=__atomic_load_n(&s->generation,__ATOMIC_RELAXED);
        int32_t sample=__atomic_load_n(&s->sample_q15,__ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);uint32_t second=__atomic_load_n(&s->seq,__ATOMIC_RELAXED);
        if(first!=second)continue;
        if(stamp!=frame||generation!=r->generation)return -61;if(sample<-32767||sample>32767)return -84;*out=(int16_t)sample;return 0;
    }
    return -16;
}
void elden_actuator_ring_taken(EldenActuatorRing *r,uint64_t end){
    if(!r||!r->page||end<r->taken)return;r->taken=end;__atomic_store_n(&r->page->taken,end,__ATOMIC_RELEASE);
}
uint64_t elden_actuator_frame(const EldenActuatorRing *r,uint64_t host){return r&&host>r->anchor?(host-r->anchor)/400:0;}
int elden_actuator_ring_new_run(EldenActuatorRing *r,uint64_t first){
    if(!r||!r->page||!r->generation||r->generation==UINT64_MAX||first<r->taken)return -22;
    r->generation++;elden_actuator_ring_taken(r,first);
    __atomic_store_n(&r->page->generation,r->generation,__ATOMIC_RELEASE);return 0;
}
