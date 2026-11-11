// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "NativeDmaBuffer.h"
#include "DmaCache.h"
#include "EldenTouchResources.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/task.h>
#include <string.h>

int EldenTouchDmaBuffer::create(EldenTouchDmaBuffer **out){
    if(!out||*out)return GENI_ERR_PARAM;
    void *raw=IOMalloc(sizeof(EldenTouchDmaBuffer));if(!raw)return -12;
    auto *b=new(raw) EldenTouchDmaBuffer;*out=b;
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    b->line_=4u<<((ctr>>16)&15);
    if(b->line_<32||b->line_>4096) {destroy(out,nullptr,nullptr);return GENI_ERR_NOTSUP;}
    // Cached, wired memory. Never map high RAM as inhibited device memory.
    b->memory_=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
        static_cast<IOOptionBits>(kIODirectionInOut)|static_cast<IOOptionBits>(kIOMemoryPhysicallyContiguous)|
        static_cast<IOOptionBits>(kIOMemoryMapperNone),2*b->span_,4096);
    if(!b->memory_){destroy(out,nullptr,nullptr);return -12;}
    // Apple kext mode uses the qualified base vtable even for explicit calls.
    // The build audit checks these offsets against the original 21H461 tables.
    if(b->memory_->IOGeneralMemoryDescriptor::prepare(kIODirectionInOut)!=kIOReturnSuccess){
        destroy(out,nullptr,nullptr);return GENI_ERR_IO;
    }
    b->prepared_=true;
    b->cpu_=static_cast<uint8_t *>(b->memory_->IOBufferMemoryDescriptor::getBytesNoCopy());
    IOByteCount contiguous=0;
    b->physical_=b->memory_->IOGeneralMemoryDescriptor::getPhysicalSegment(0,&contiguous,kIOMemoryMapperNone);
    if(!b->cpu_||!b->physical_||(b->physical_&4095)||((uintptr_t)b->cpu_&4095)||
       contiguous<2*b->span_||b->physical_>UINT64_MAX-2*b->span_){destroy(out,nullptr,nullptr);return GENI_ERR_IO;}
    b->staging_=static_cast<uint8_t *>(IOMalloc(b->span_));
    if(!b->staging_){destroy(out,nullptr,nullptr);return -12;}
    memset(b->cpu_,0,2*b->span_);memset(b->staging_,0,b->span_);
    // Only our two separate buffers; no DMA is possible before export.
    b->cache(b->cpu_,b->span_,1);b->cache(b->cpu_+b->span_,b->span_,1);
    if(b->cache_fault_){destroy(out,nullptr,nullptr);return GENI_ERR_IO;}
    return 0;
}
int EldenTouchDmaBuffer::destroy(EldenTouchDmaBuffer **handle,void *owner,
                                int (*proof)(void *,uint64_t)){
    if(!handle||!*handle)return GENI_ERR_PARAM;
    auto *b=*handle;
    if(b->exported_){if(!proof)return GENI_ERR_IO;int rc=proof(owner,b->epoch_);if(rc)return rc;}
    if(b->prepared_){
        if(b->memory_->IOGeneralMemoryDescriptor::complete(kIODirectionInOut)!=kIOReturnSuccess)return GENI_ERR_IO;
        b->prepared_=false;
    }
    if(b->memory_)b->memory_->release();
    if(b->staging_)IOFree(b->staging_,b->span_);
    b->~EldenTouchDmaBuffer();IOFree(b,sizeof(EldenTouchDmaBuffer));*handle=nullptr;return 0;
}
int EldenTouchDmaBuffer::exportBuffers(struct geni_spi *s,EldenTouchDmaContract *c,uint64_t epoch,
                                     uint64_t tx,uint64_t rx){
    if(!s||!c||!c->check||c->sid!=ELDEN_TOUCH_SE_DMA_SID||!epoch||exported_||cache_fault_||!prepared_||!staging_||
       !tx||!rx||tx>UINT64_MAX-span_||rx>UINT64_MAX-span_||
       !(tx+span_<=rx||rx+span_<=tx))return GENI_ERR_PARAM;
    s->dma.ctx=this;s->dma.clean=clean;s->dma.clean_inval=cleanInvalidate;s->dma.inval=invalidate;
    s->dma.tx_virt=cpu_;s->dma.rx_virt=cpu_+span_;s->dma.tx_phys=tx;s->dma.rx_phys=rx;s->dma.size=span_;
    c->epoch=epoch;c->staging=staging_;c->staging_bytes=span_;c->cache_line_bytes=line_;
    route_owner_=c->context;route_check_=c->check;c->context=this;c->check=check;
    epoch_=epoch;exported_=true;return 0;
}
int EldenTouchDmaBuffer::check(void *p,EldenTouchDmaCheck phase,uint64_t epoch,unsigned sid,
                              uint64_t tx,uint64_t rx,size_t bytes){
    auto *b=static_cast<EldenTouchDmaBuffer *>(p);
    if(b->cache_fault_||epoch!=b->epoch_||!b->exported_||!b->route_check_)return GENI_ERR_IO;
    return b->route_check_(b->route_owner_,phase,epoch,sid,tx,rx,bytes);
}
void EldenTouchDmaBuffer::cache(void *address,size_t bytes,unsigned op){
    EldenDmaCacheSpan range;
    // Canonical GENI callbacks always use the buffer base. Reject foreign or
    // cross-buffer cache ranges before executing any cache instruction.
    if((address!=cpu_&&address!=cpu_+span_)||op>2||
       elden_dma_cache_span((uintptr_t)address,span_,(uintptr_t)address,bytes,line_,&range)){
        cache_fault_=true;return;
    }
    for(uintptr_t p=range.first;p<range.end;p+=range.line){
        if(op==0)__asm__ volatile("dc cvac, %0"::"r"(p):"memory");
        else if(op==1)__asm__ volatile("dc civac, %0"::"r"(p):"memory");
        else __asm__ volatile("dc ivac, %0"::"r"(p):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
}
void EldenTouchDmaBuffer::clean(void *p,void *a,size_t n){static_cast<EldenTouchDmaBuffer *>(p)->cache(a,n,0);}
void EldenTouchDmaBuffer::cleanInvalidate(void *p,void *a,size_t n){static_cast<EldenTouchDmaBuffer *>(p)->cache(a,n,1);}
void EldenTouchDmaBuffer::invalidate(void *p,void *a,size_t n){static_cast<EldenTouchDmaBuffer *>(p)->cache(a,n,2);}
