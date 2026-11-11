// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include "DmaTransfer.h"
class IOBufferMemoryDescriptor;
// Allocation/cache adapter only. No clocks, SMMU, SPI setup or startup hook.
// Serialized owner retains this object until destroy succeeds. No copying.
class EldenTouchDmaBuffer final {
public:
    // *out must start null. On cleanup failure it retains an owned object
    // even when create returns an error; the caller must not discard it.
    static int create(EldenTouchDmaBuffer **out);
    // Once exported, destruction requires the same epoch owner to prove all
    // DMA stopped and the mappings withdrawn. An error retains everything.
    static int destroy(EldenTouchDmaBuffer **,void *owner,
                       int (*release_proof)(void *,uint64_t));
    uint64_t txPhysical() const {return physical_;}
    uint64_t rxPhysical() const {return physical_+span_;}
    size_t span() const {return span_;}
    size_t cacheLine() const {return line_;}
    // tx/rx are reviewed BUS addresses supplied by the route owner; they are
    // never inferred from physical_. Supply the lease callback and SID in
    // contract first; export wraps it to also propagate cache faults.
    int exportBuffers(struct geni_spi *,EldenTouchDmaContract *,uint64_t epoch,
                      uint64_t tx_bus,uint64_t rx_bus);
    bool cacheFault() const {return cache_fault_;}
private:
    static void *operator new(size_t,void *storage) noexcept {return storage;}
    EldenTouchDmaBuffer()=default;
    ~EldenTouchDmaBuffer()=default;
    EldenTouchDmaBuffer(const EldenTouchDmaBuffer&)=delete;
    EldenTouchDmaBuffer& operator=(const EldenTouchDmaBuffer&)=delete;
    static void clean(void *,void *,size_t);
    static void cleanInvalidate(void *,void *,size_t);
    static void invalidate(void *,void *,size_t);
    static int check(void *,EldenTouchDmaCheck,uint64_t,unsigned,uint64_t,uint64_t,size_t);
    void cache(void *,size_t,unsigned);
    IOBufferMemoryDescriptor *memory_=nullptr;
    uint8_t *cpu_=nullptr,*staging_=nullptr;
    uint64_t physical_=0,epoch_=0;
    void *route_owner_=nullptr;
    int (*route_check_)(void *,EldenTouchDmaCheck,uint64_t,unsigned,uint64_t,uint64_t,size_t)=nullptr;
    size_t span_=16384,line_=0;
    bool prepared_=false,exported_=false,cache_fault_=false;
};
