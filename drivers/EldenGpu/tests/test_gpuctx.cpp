/* SPDX-License-Identifier: MIT */
/* Ported from research/Adreno740-ipados27 @ 5eb625f tests/test_gpuctx.cpp for the A830
 * maintenance derivation (MaintenanceSource.py). A830 deltas: six context buffers
 * (SQE, ring, memptrs, pwrup, scratch, AQE — 184 KiB), bringUp's chip argument, no
 * ring switching, and buffers 0 and 5 (SQE/AQE) are the read-only ones (perms 5). */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoGpuContext.h"
#include "ArmSmmu4KMmio.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
extern "C" {volatile uint32_t native_adreno_wc_enabled,native_adreno_table_switches[8];}
extern "C" volatile uint64_t native_adreno_maint_kind[8][3],native_adreno_maint_walk[8];
extern "C" volatile uint32_t native_adreno_sync_skips[2],native_adreno_sync_audit[6],native_adreno_maint_all;
extern "C" int PE_parse_boot_argn(const char *,void *,int) {return 0;}
extern "C" unsigned int native_pmap_user_mappings(ppnum_t,void **,uint64_t *,char *) {return 0;}
void NativeAdrenoNotePhase(unsigned,size_t) {}
bool NativeAdrenoPagesUncached(const NativeAdrenoPhysicalSpan *,size_t) {return false;}
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *s) {*s={};return 0;}
/* Maintenance through a kernel mapping: every call, with its operation and size. */
struct Call {void *mapping;size_t bytes;unsigned op;};
static std::vector<Call> calls;
IOReturn NativeAdrenoMaintainOwnedPages(void *mapping,size_t bytes,NativeAdrenoCacheOperation op)
{CHECK(mapping && bytes && op<=NativeAdrenoInvalidate);calls.push_back({mapping,bytes,(unsigned)op});return 0;}
extern "C" ArmSmmu4KStatus ArmSmmu4KBindMmio(ArmSmmu4KMmio *,ArmSmmu4KIo *) {return ArmSmmu4KUnsupported;}
#define SMMU_BYTES 0x40000
alignas(8) static uint32_t mmio[SMMU_BYTES/4];
static int read32(void *,size_t at,uint32_t *v){CHECK(at%4==0 && at<=SMMU_BYTES-4);*v=mmio[at/4];return 0;}
static int read64(void *,size_t at,uint64_t *v){CHECK(at%8==0 && at<=SMMU_BYTES-8);std::memcpy(v,mmio+at/4,8);return 0;}
static int write32(void *,size_t at,uint32_t v)
{
    CHECK(at%4==0 && at<=SMMU_BYTES-4);
    if((at&0xfff)==0x58 && at>=0x20000) {mmio[at/4]&=~v;return 0;}   /* context FSR is write-one-to-clear */
    mmio[at/4]=v;return 0;
}
static int write64(void *,size_t at,uint64_t v){CHECK(at%8==0 && at<=SMMU_BYTES-8);std::memcpy(mmio+at/4,&v,8);return 0;}
static int barrier(void *){return 0;}
static void reset()
{
    std::memset(mmio,0,sizeof(mmio));
    mmio[0]=0x002f0406;mmio[0x20/4]=0x4c017e18;mmio[0x24/4]=0x40000016;mmio[0x28/4]=0x1433;
    for(unsigned i=0;i<24;++i)mmio[(0xc00+4*i)/4]=0x20000;   /* every S2CR FAULT, every SMR invalid */
}
/* How many of the calls from `first` on were made on one of the context's buffers, and with which operation. */
static unsigned onBuffers(const NativeAdrenoGpuContext &context,size_t first,unsigned op)
{
    unsigned found=0;
    for(size_t c=first;c<calls.size();++c)for(unsigned i=0;i<context.bufferCount();++i)
        if(calls[c].mapping==context.buffer(i).mapping() && calls[c].op==op)++found;
    return found;
}
int main()
{
    const ArmSmmu4KIo io{mmio,SMMU_BYTES,read32,read64,write32,write64,barrier};
    const unsigned slots[2]={0,1};const uint16_t sids[2]={0,1};
    native_adreno_maint_all=0;
    reset();
    static NativeAdrenoGpuContext context;
    NativeAdrenoGmuContextReport report;
    /* A830 chip id; six buffers. */
    CHECK(context.bringUp(&io,0,2,slots,sids,nullptr,&report,0x44050001)==kIOReturnSuccess && context.ready());
    const unsigned count=context.bufferCount();
    CHECK(count==6);
    size_t kib=0;
    for(unsigned i=0;i<count;++i) {CHECK(context.buffer(i).synchronized());kib+=context.buffer(i).bytes()>>10;}
    CHECK(kib==184);   /* SQE 112 + ring 32 + memptrs 4 + pwrup 4 + scratch 4 + AQE 28 KiB */
    /* Each buffer: the zero fill cleaned, then cleaned and dropped as it is declared; the map into the kernel table adds nothing. */
    CHECK(onBuffers(context,0,NativeAdrenoClean)==count && onBuffers(context,0,NativeAdrenoCleanInvalidate)==count &&
        onBuffers(context,0,NativeAdrenoInvalidate)==0);
    CHECK(native_adreno_sync_skips[0]==count);
    /* createVm gates on the kernel table's ttbr0_switched image (enableSwitching); the submit path never
     * selects a second ring on A830. */
    CHECK(context.enableSwitching()==kIOReturnSuccess);
    /* A process address space maps every buffer at its fixed address and no CPU cache line is touched for it. */
    NativeAdrenoProcessVm *space=nullptr;
    size_t mark=calls.size();
    CHECK(context.createVm(&space)==kIOReturnSuccess && space && space->vm() && space->vm()->objectCount()==count);
    for(unsigned op=NativeAdrenoClean;op<=NativeAdrenoInvalidate;++op)CHECK(onBuffers(context,mark,op)==0);
    CHECK(native_adreno_sync_skips[0]==2*count);
    /* A submission's pass over that table: nothing to visit, six mappings left alone. */
    const uint64_t left=native_adreno_maint_kind[NativeAdrenoKindDriver][2],visited=native_adreno_maint_walk[1];
    mark=calls.size();
    CHECK(space->vm()->publishMapped()==kIOReturnSuccess && calls.size()==mark);
    CHECK(native_adreno_maint_kind[NativeAdrenoKindDriver][2]==left+count && native_adreno_maint_walk[1]==visited);
    /* The probe's store goes through the kernel's mapping of the scratch page: that page is cleaned there. */
    uint64_t handle=0,iova=0;size_t bytes=0;
    mark=calls.size();
    CHECK(context.probeBuffer(space,&handle,&iova,&bytes)==kIOReturnSuccess && handle && iova==GpuScratchIova+0xF00 && bytes==4);
    CHECK(calls.size()==mark+1 && calls[mark].mapping==context.buffer(4).mapping() && calls[mark].bytes==GpuPageBytes &&
        calls[mark].op==NativeAdrenoClean);
    CHECK(static_cast<const uint32_t *>(context.buffer(4).mapping())[0xF00/4]!=0);
    /* Its teardown unmaps them without invalidating: four are writable (SQE/AQE are read-only). */
    mark=calls.size();
    CHECK(context.destroyVm(space)==kIOReturnSuccess && onBuffers(context,mark,NativeAdrenoInvalidate)==0);
    CHECK(native_adreno_sync_skips[1]==count-2 && context.liveVms()==0);
    /* The comparison: every map cleans and invalidates what the GPU may write (and cleans the read-only SQE/AQE), every unmap
     * invalidates the writable ones. */
    native_adreno_maint_all=1;
    mark=calls.size();
    CHECK(context.createVm(&space)==kIOReturnSuccess);
    CHECK(onBuffers(context,mark,NativeAdrenoCleanInvalidate)==count-2 && onBuffers(context,mark,NativeAdrenoClean)==2);
    mark=calls.size();
    CHECK(space->vm()->publishMapped()==kIOReturnSuccess && onBuffers(context,mark,NativeAdrenoCleanInvalidate)==count);
    mark=calls.size();
    CHECK(context.destroyVm(space)==kIOReturnSuccess && onBuffers(context,mark,NativeAdrenoInvalidate)==count-2);
    native_adreno_maint_all=0;
    std::puts("PASS: A830 GPU context bring-up declares its 6 buffers (184 KiB) synchronized; a process address space maps, submits "
        "over and unmaps them with no CPU cache maintenance; the probe cleans the page it stored into; gpu-maint-all restores the passes\n");
}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool){return spans && count?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans,size_t count,uint32_t *reset)
{return spans && count && reset?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical,size_t bytes,NativeAdrenoCacheOperation op)
{
    if(!physical || !bytes || ((physical|bytes)&4095) || op>NativeAdrenoInvalidate)std::abort();
    ++fixture.physical_calls;fixture.physical_bytes+=bytes;return kIOReturnSuccess;
}
