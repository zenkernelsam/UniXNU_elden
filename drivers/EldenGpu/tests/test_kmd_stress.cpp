/* SPDX-License-Identifier: MIT */
/* KMD ring/fence stress: real VM, ring and execbuf code over software register peers.
 * Multi-context interleaved submissions across ring wrap-around, saturation, stale and
 * duplicate fence waits, teardown with outstanding fences. Deterministic; host/model only. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoSubmit.h"
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" volatile uint32_t native_adreno_stall_hang,native_adreno_stall_find[14],native_adreno_stall_fault[32];
extern "C" volatile uint32_t native_adreno_sync_audit[6],native_adreno_audit_mode;
extern "C" volatile uint64_t native_adreno_submit_us[8];
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define SMMU_BYTES 0x40000
#define CB 0x22000
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *out)
{CHECK(out);*out={};return kIOReturnSuccess;}
IOReturn NativeAdrenoMaintainOwnedPages(void *mapping,size_t bytes,NativeAdrenoCacheOperation op)
{
    CHECK(mapping && bytes && !(((uintptr_t)mapping|bytes)&4095));
    CHECK(op==NativeAdrenoClean || op==NativeAdrenoCleanInvalidate || op==NativeAdrenoInvalidate);
    ++fixture.cache_calls;
    return fixture.cache_calls==fixture.cache_fail_call?kIOReturnIOError:kIOReturnSuccess;
}
/* SMMUv2 register peer. */
static uint32_t mmio[SMMU_BYTES/4];
static struct {unsigned calls,fail_call,after;} mf;
static ArmSmmu4KIo smmuio;
static ArmSmmu4KStreamState streams;
static ArmSmmu4KStream routes[2]={{2,0},{4,1}};
static ArmSmmu4KStreamResult route_result;
static ArmSmmu4KContextConfig context;
static ArmSmmu4KContextImage context_image;
static int mevent(void){return ++mf.calls==mf.fail_call;}
static int mread32(void *c,size_t at,uint32_t *v)
{CHECK(c==&mf && at%4==0 && at<=SMMU_BYTES-4);if(mevent())return 1;*v=mmio[at/4];return 0;}
static int mread64(void *c,size_t at,uint64_t *v)
{CHECK(c==&mf && at%8==0 && at<=SMMU_BYTES-8);if(mevent())return 1;std::memcpy(v,mmio+at/4,8);return 0;}
static int mwrite32(void *c,size_t at,uint32_t v)
{CHECK(c==&mf && at%4==0 && at<=SMMU_BYTES-4);int f=mevent();if(f && !mf.after)return 1;mmio[at/4]=v;return f;}
static int mwrite64(void *c,size_t at,uint64_t v)
{CHECK(c==&mf && at%8==0 && at<=SMMU_BYTES-8);int f=mevent();if(f && !mf.after)return 1;std::memcpy(mmio+at/4,&v,8);return f;}
static int mbarrier(void *c){CHECK(c==&mf);return mevent();}
/* CP register peer. */
struct CpPeer {
    std::vector<uint32_t> regs;
    std::vector<uint32_t> writes;
    unsigned publish=0,acquire=0,barriers=0,fail_publish=0,fail_acquire=0,fail_barrier=0;
    CpPeer():regs(0x10000,0){}
};
static CpPeer cp;
static uint64_t clock_us=0;
static int cwrite(void *c,uint32_t reg,uint32_t v)
{CHECK(c==&cp && reg<cp.regs.size());cp.regs[reg]=v;cp.writes.push_back(reg);return 0;}
static int cread(void *c,uint32_t reg,uint32_t *v)
{CHECK(c==&cp && v && reg<cp.regs.size());*v=cp.regs[reg];return 0;}
static int cbarrier(void *c){CHECK(c==&cp);++cp.barriers;return cp.barriers==cp.fail_barrier;}
static int cpublish(void *c,const volatile uint32_t *w,size_t bytes)
{CHECK(c==&cp && w && (bytes==32768 || bytes==4096));++cp.publish;return cp.publish==cp.fail_publish;}
static int cacquire(void *c,volatile uint32_t *w,size_t bytes)
{CHECK(c==&cp && w && bytes==4096);++cp.acquire;return cp.acquire==cp.fail_acquire;}
static uint64_t cnow(void *c){CHECK(c==&cp);return clock_us;}
static int cidle(void *c){CHECK(c==&cp);clock_us+=1000;return 0;}
/* Per-context path: each context owns its ring/fence memory so interleaving is real. */
struct Path {
    NativeAdrenoTablePages tables;
    NativeAdrenoMemory object;
    ArmSmmu4KLiveArena arena;
    ArmSmmu4KLiveState state{};
    ArmSmmu4KLiveVisibility visibility;
    NativeAdrenoVm vm;
    NativeAdrenoRing ring;
    NativeAdrenoSubmit submit;
    uint64_t handle=0;
    uint32_t ringmem[8192],fencemem[1024];
};
static std::vector<uint64_t> dram_reads;
static NativeAdrenoRingIo ringio{&cp,cwrite,cread,cbarrier,nullptr,nullptr};
static NativeAdrenoRingCache ringcache{&cp,cpublish,cacquire};
static NativeAdrenoRingClock ringclock{&cp,cnow,cidle};
static void smmuReset(void)
{
    std::memset(mmio,0,sizeof(mmio));mf={};streams=(ArmSmmu4KStreamState){};
    smmuio=(ArmSmmu4KIo){&mf,SMMU_BYTES,mread32,mread64,mwrite32,mwrite64,mbarrier};
    mmio[0x20/4]=(1u<<30)|(1u<<27)|(4u<<9)|8;
    mmio[0x24/4]=0x40000010;mmio[0x28/4]=0x1433;mmio[0]=0x436;
    for(unsigned i=0;i<8;++i)mmio[(0xc00+4*i)/4]=0x20000;
    mmio[0x800/4]=1u<<31|9;mmio[0xc00/4]=3;
}
static int exposed(Path &p)
{
    /* Each context re-owns the shared SMMU peer: ring/fence traffic never touches the SMMU at
     * runtime, so resetting it per attach models independent per-context SMMU state. */
    smmuReset();
    cp=CpPeer{};
    std::memset(p.ringmem,0xef,sizeof(p.ringmem));std::memset(p.fencemem,0,sizeof(p.fencemem));
    if(NativeAdrenoPrepareSmmuMemory(&p.tables,32,40,0x800000,65536,0,16,&p.arena,&p.state,&p.visibility))
        return kIOReturnBadArgument;
    context=(ArmSmmu4KContextConfig){.lower=&p.arena.config,.lower_tables=&p.state.tables,
        .id0=mmio[0x20/4],.id2=mmio[0x28/4],.asid=1,.coherent_walk=1,
        .stall_on_fault=0,.continue_after_fault=1};
    ArmSmmu4KContextConfig actual=context;
    CHECK(ArmSmmu4KEncodeContext(&actual,&context_image)==ArmSmmu4KSuccess);
    mmio[0x1008/4]=context_image.cbar;mmio[0x1808/4]=context_image.cba2r;
    mmio[CB/4]=context_image.sctlr;mmio[(CB+0x10)/4]=context_image.tcr2;mmio[(CB+0x30)/4]=context_image.tcr;
    std::memcpy(mmio+(CB+0x20)/4,&context_image.ttbr0,8);
    std::memcpy(mmio+(CB+0x28)/4,&context_image.ttbr1,8);
    mmio[(CB+0x38)/4]=context_image.mair0;
    if(ArmSmmu4KAttachStreams(&smmuio,&streams,routes,2,2,&context,&route_result))return kIOReturnBadArgument;
    NativeAdrenoVmRegion lower={&p.tables,&p.arena,&p.state};
    if(p.vm.attach(&smmuio,&context,&streams,&lower,nullptr,32,1000))return kIOReturnBadArgument;
    if(p.object.allocate(8192,40,false,8192))return kIOReturnNoMemory;
    if(p.vm.map(&p.object,0,0x800000,3,0,2,&p.handle))return kIOReturnBadArgument;
    if(p.ring.attach(&ringio,&ringcache,p.ringmem,sizeof(p.ringmem),0x100000,p.fencemem,sizeof(p.fencemem),0x108000))
        return kIOReturnBadArgument;
    return p.submit.attach(&p.vm,&p.ring);
}
/* CP read-pointer simulation: the real CP advances CP_RB_RPTR (0x0806 dword index) as it
 * consumes ring words; the harness drives it so ring space frees exactly when fences retire. */
enum : uint32_t {CpRptr=0x0806,CpWptr=0x0807};
static void cpRetireExact(Path &p,uint32_t fence,uint32_t wptrAtFence)
{
    cp.regs[CpRptr]=wptrAtFence;
    p.fencemem[1]=fence;
}
/* Host doubles for the board's physical-page maintenance (no host page is touched). */
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool){return spans && count?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans,size_t count,uint32_t *reset)
{return spans && count && reset?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical,size_t bytes,NativeAdrenoCacheOperation op)
{
    if(!physical || !bytes || ((physical|bytes)&4095) || op>NativeAdrenoInvalidate)std::abort();
    ++fixture.physical_calls;fixture.physical_bytes+=bytes;return kIOReturnSuccess;
}
void NativeAdrenoNotePhase(unsigned,size_t) {}
int main()
{
    smmuReset();clock_us=0;
    unsigned long long iterations=0;
    /* 1. Wrap-around storm: one context, 2400 submit+retire iterations — the 8192-word ring
     * write pointer wraps many times; fences stay monotonic and references fully drain. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        static_cast<volatile uint32_t *>(p.object.mapping())[0]=0x70970001u;
        for(uint32_t n=1;n<=2400;++n) {
            uint32_t fence=0;
            CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess && fence==n);
            cpRetireExact(p,n,cp.regs[CpWptr]);
            CHECK(p.submit.wait(n,&ringclock,1000)==kIOReturnSuccess);
            ++iterations;
        }
        CHECK(p.submit.outstanding()==0 && p.submit.stats().completed==2400);
        CHECK(p.submit.stats().references_held==p.submit.stats().references_retired);
        CHECK(p.object.deviceReferences()==1);   /* mapping only */
        CHECK(p.submit.detach()==kIOReturnSuccess);
        iterations+=2400-1;   /* waits count as iterations too (2400 submits + 2400 waits - 1 already counted) */
    }
    /* 2. Multi-context interleave: on A830 there is exactly one ring (no ring switching);
     * four independent address spaces share it round-robin — the ring emits table-switch
     * prologues as the current context changes. Fences are ring-global monotonic; retirement
     * is in-order across contexts. 1200 submissions with staggered drain. */
    {
        Path paths[4];
        for(auto &p:paths)CHECK(exposed(p)==kIOReturnSuccess);
        /* All four contexts submit through the first path's ring; each keeps its own VM/memory. */
        NativeAdrenoSubmit submitters[4];
        for(unsigned i=0;i<4;++i)CHECK(submitters[i].attach(&paths[i].vm,&paths[0].ring)==kIOReturnSuccess);
        std::vector<uint32_t> wp;wp.push_back(0);
        std::vector<unsigned> owner;owner.push_back(0);
        uint32_t global=0;
        for(unsigned n=0;n<1200;++n) {
            const unsigned i=n%4;
            NativeAdrenoSubmitBuffer buffer{paths[i].handle,0x800000,4096};
            uint32_t fence=0;
            CHECK(submitters[i].submit(&buffer,1,0,&fence)==kIOReturnSuccess);
            CHECK(fence==global+1);   /* single ring: strictly monotonic across contexts */
            global=fence;
            wp.push_back(cp.regs[CpWptr]);
            owner.push_back(i);
            /* Staggered in-order retirement: every other round the CP retires everything up to
             * fence-1. wait() releases exactly one job, its owner's — ring space still frees. */
            if(fence>1 && n%2==0) {
                cpRetireExact(paths[0],fence-1,wp[fence-1]);
                CHECK(submitters[owner[fence-1]].wait(fence-1,&ringclock,1000)==kIOReturnSuccess);
            }
            ++iterations;
        }
        cpRetireExact(paths[0],global,wp[global]);
        /* Per-fence drain in order: every wait releases its owner job; all four contexts empty. */
        for(uint32_t f=1;f<=global;++f)
            CHECK(submitters[owner[f]].wait(f,&ringclock,1000)==kIOReturnSuccess);
        for(unsigned i=0;i<4;++i) {
            CHECK(submitters[i].outstanding()==0);
            CHECK(submitters[i].stats().references_held==submitters[i].stats().references_retired);
            CHECK(submitters[i].detach()==kIOReturnSuccess);
        }
        iterations+=1200+1200;   /* submissions + drain waits */
    }
    /* 3. Fence edge cases: duplicate waits, stale (long-retired) fences, transitive completion,
     * never-submitted and zero fences rejected. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        uint32_t fence=0;
        for(unsigned i=0;i<3;++i)CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
        cpRetireExact(p,3,cp.regs[CpWptr]);
        /* Transitive: fence 1 and 2 complete when the CP word passes 3. */
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnSuccess);
        CHECK(p.submit.wait(2,&ringclock,1000)==kIOReturnSuccess);
        /* Duplicate waits on the same fence both succeed. */
        CHECK(p.submit.wait(3,&ringclock,1000)==kIOReturnSuccess);
        CHECK(p.submit.wait(3,&ringclock,1000)==kIOReturnSuccess);
        /* Stale: long-retired fence still answers success (fast path, no timeout consumed). */
        const uint64_t before=clock_us;
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnSuccess && clock_us==before);
        /* Negative: never-submitted and zero fences are rejected. */
        CHECK(p.submit.wait(4,&ringclock,1000)==kIOReturnBadArgument);
        CHECK(p.submit.wait(0,&ringclock,1000)==kIOReturnBadArgument);
        CHECK(p.submit.detach()==kIOReturnSuccess);
        iterations+=7;
    }
    /* 4. Ring saturation: submissions fill the ring until kIOReturnBusy; no state corruption;
     * draining every fence reopens the ring and submission resumes at the next monotonic fence. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        uint32_t fence=0,last=0;
        unsigned accepted=0;
        for(unsigned n=0;n<600;++n) {
            IOReturn rc=p.submit.submit(&buffer,1,0,&fence);
            if(rc==kIOReturnBusy)break;
            CHECK(rc==kIOReturnSuccess && fence==last+1);
            last=fence;++accepted;++iterations;
        }
        CHECK(accepted<600);   /* the ring really saturated */
        CHECK(p.submit.outstanding()==accepted);
        /* A busy ring rejects further work without consuming a fence. */
        uint32_t probe=0;
        CHECK(p.submit.submit(&buffer,1,0,&probe)==kIOReturnBusy && probe==0);
        cpRetireExact(p,last,cp.regs[CpWptr]);
        for(uint32_t f=1;f<=last;++f) {
            cp.regs[CpRptr]=cp.regs[CpWptr];
            CHECK(p.submit.wait(f,&ringclock,1000)==kIOReturnSuccess);
        }
        CHECK(p.submit.outstanding()==0);
        CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess && fence==last+1);
        cpRetireExact(p,fence,cp.regs[CpWptr]);
        CHECK(p.submit.wait(fence,&ringclock,1000)==kIOReturnSuccess);
        CHECK(p.submit.detach()==kIOReturnSuccess);
        std::printf("ring saturated at %u outstanding\n",accepted);
        iterations+=3;
    }
    /* 5. Teardown with outstanding fences: detach answers kIOReturnBusy while jobs are in
     * flight; after the CP drains them the teardown succeeds with balanced references. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        uint32_t fence=0,last=0;
        for(unsigned i=0;i<5;++i) {
            CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
            last=fence;
        }
        CHECK(p.submit.detach()==kIOReturnBusy);
        CHECK(p.submit.detach()==kIOReturnBusy);   /* stable under repetition */
        cpRetireExact(p,last,cp.regs[CpWptr]);
        for(uint32_t f=1;f<=last;++f)
            CHECK(p.submit.wait(f,&ringclock,1000)==kIOReturnSuccess);
        CHECK(p.submit.detach()==kIOReturnSuccess);
        CHECK(p.submit.stats().references_held==p.submit.stats().references_retired);
        iterations+=9;
    }
    std::printf("PASS: KMD ring/fence stress — wrap storm, 4-context interleave, fence edge cases, saturation, teardown; %llu iterations; references balanced\n",iterations);
}
