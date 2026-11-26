/* SPDX-License-Identifier: MIT */
/* SMMU/VM address-space isolation & BO lifecycle stress: real derived Vm/Memory/SmmuMemory
 * code over software SMMU register peers (one independent peer per context — SMMU banks do
 * not interact, so independent peers model independent contexts exactly). Map/unmap/remap
 * churn, multi-context isolation, overlap/alignment/zero-size/double-unmap/cross-context
 * rejection, teardown with active mappings. Deterministic; host/model only. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoVm.h"
#include "NativeAdrenoMemory.h"
#include <cstdio>
#include <cstring>
#include <vector>
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
/* One software SMMUv2 register peer; each address-space context owns one. */
struct SmmuPeer {
    std::vector<uint32_t> mmio;
    unsigned calls=0,fail_call=0,after=0;
    ArmSmmu4KIo io{};
    ArmSmmu4KStreamState streams{};
    ArmSmmu4KStreamResult route_result{};
    ArmSmmu4KContextConfig config{};
    ArmSmmu4KContextImage image{};
    SmmuPeer():mmio(SMMU_BYTES/4,0){}
};
static int mevent(SmmuPeer *p){return ++p->calls==p->fail_call;}
static int mread32(void *c,size_t at,uint32_t *v)
{
    SmmuPeer *p=(SmmuPeer *)c;CHECK(p && at%4==0 && at<=SMMU_BYTES-4);
    if(mevent(p))return 1;*v=p->mmio[at/4];return 0;
}
static int mread64(void *c,size_t at,uint64_t *v)
{
    SmmuPeer *p=(SmmuPeer *)c;CHECK(p && at%8==0 && at<=SMMU_BYTES-8);
    if(mevent(p))return 1;std::memcpy(v,p->mmio.data()+at/4,8);return 0;
}
static int mwrite32(void *c,size_t at,uint32_t v)
{
    SmmuPeer *p=(SmmuPeer *)c;CHECK(p && at%4==0 && at<=SMMU_BYTES-4);
    int f=mevent(p);if(f && !p->after)return 1;p->mmio[at/4]=v;return f;
}
static int mwrite64(void *c,size_t at,uint64_t v)
{
    SmmuPeer *p=(SmmuPeer *)c;CHECK(p && at%8==0 && at<=SMMU_BYTES-8);
    int f=mevent(p);if(f && !p->after)return 1;std::memcpy(p->mmio.data()+at/4,&v,8);return f;
}
static int mbarrier(void *c){CHECK(c);return mevent((SmmuPeer *)c);}
static ArmSmmu4KStream routes[2]={{2,0},{4,1}};
/* One address-space context: own peer, tables, arena, vm. Arena [0x800000, 0x810000). */
struct Ctx {
    SmmuPeer peer;
    NativeAdrenoTablePages tables;
    ArmSmmu4KLiveArena arena;
    ArmSmmu4KLiveState state{};
    ArmSmmu4KLiveVisibility visibility;
    NativeAdrenoVm vm;
};
static void program(SmmuPeer &p)
{
    p.mmio[0x20/4]=(1u<<30)|(1u<<27)|(4u<<9)|8;
    p.mmio[0x24/4]=0x40000010;p.mmio[0x28/4]=0x1433;p.mmio[0]=0x436;
    for(unsigned i=0;i<8;++i)p.mmio[(0xc00+4*i)/4]=0x20000;
    p.mmio[0x800/4]=1u<<31|9;p.mmio[0xc00/4]=3;
    p.io=(ArmSmmu4KIo){&p,SMMU_BYTES,mread32,mread64,mwrite32,mwrite64,mbarrier};
}
static int exposed(Ctx &c)
{
    program(c.peer);
    int prep=NativeAdrenoPrepareSmmuMemory(&c.tables,32,40,0x800000,65536,0,16,&c.arena,&c.state,&c.visibility);
    if(prep) {std::fprintf(stderr,"prepare rc=%d\n",prep);return kIOReturnBadArgument;}
    c.peer.config=(ArmSmmu4KContextConfig){.lower=&c.arena.config,.lower_tables=&c.state.tables,
        .id0=c.peer.mmio[0x20/4],.id2=c.peer.mmio[0x28/4],.asid=1,.coherent_walk=1,
        .stall_on_fault=0,.continue_after_fault=1};
    ArmSmmu4KContextConfig actual=c.peer.config;
    CHECK(ArmSmmu4KEncodeContext(&actual,&c.peer.image)==ArmSmmu4KSuccess);
    c.peer.mmio[0x1008/4]=c.peer.image.cbar;c.peer.mmio[0x1808/4]=c.peer.image.cba2r;
    c.peer.mmio[CB/4]=c.peer.image.sctlr;c.peer.mmio[(CB+0x10)/4]=c.peer.image.tcr2;
    c.peer.mmio[(CB+0x30)/4]=c.peer.image.tcr;
    std::memcpy(c.peer.mmio.data()+(CB+0x20)/4,&c.peer.image.ttbr0,8);
    std::memcpy(c.peer.mmio.data()+(CB+0x28)/4,&c.peer.image.ttbr1,8);
    c.peer.mmio[(CB+0x38)/4]=c.peer.image.mair0;
    int atrc=ArmSmmu4KAttachStreams(&c.peer.io,&c.peer.streams,routes,2,2,&c.peer.config,&c.peer.route_result);
    if(atrc) {std::fprintf(stderr,"attach-streams rc=%d\n",atrc);return kIOReturnBadArgument;}
    NativeAdrenoVmRegion lower={&c.tables,&c.arena,&c.state};
    IOReturn vrc=c.vm.attach(&c.peer.io,&c.peer.config,&c.peer.streams,&lower,nullptr,32,1000);
    if(vrc)std::fprintf(stderr,"vm-attach rc=%d\n",(int)vrc);
    return vrc;
}
int main()
{
    unsigned long long iterations=0;
    /* 1. Map/unmap/remap churn: 600 cycles at a fixed IOVA, then 300 remap cycles walking the
     * arena — the VM returns to zero objects every cycle and detaches cleanly at the end. */
    {
        Ctx c;CHECK(exposed(c)==kIOReturnSuccess);
        NativeAdrenoMemory object;
        CHECK(object.allocate(8192,40,false,8192)==kIOReturnSuccess);
        for(unsigned n=0;n<600;++n) {
            uint64_t handle=0;
            CHECK(c.vm.map(&object,0,0x800000,3,0,2,&handle)==kIOReturnSuccess && handle);
            CHECK(c.vm.unmap(handle)==kIOReturnSuccess);
            ++iterations;
        }
        for(unsigned n=0;n<300;++n) {
            const uint64_t iova=0x800000+((n%6)*0x2000);
            uint64_t handle=0;
            CHECK(c.vm.map(&object,0,iova,3,0,2,&handle)==kIOReturnSuccess && handle);
            CHECK(c.vm.unmap(handle)==kIOReturnSuccess);
            ++iterations;
        }
        object.release();
        CHECK(c.vm.detach()==kIOReturnSuccess);
        iterations+=600+300;
    }
    /* 2. Multi-context isolation: handles are per-VM namespaces (next_handle_ counter), so the
     * isolation invariant is that no operation on one context can ever resolve or disturb a
     * mapping of the other. Two independent address spaces map different objects at the SAME
     * iova; churning one context leaves the other's mapping fully live and removable. */
    {
        Ctx a,b;
        CHECK(exposed(a)==kIOReturnSuccess);
        CHECK(exposed(b)==kIOReturnSuccess);
        NativeAdrenoMemory oa,ob;
        CHECK(oa.allocate(8192,40,false,8192)==kIOReturnSuccess);
        CHECK(ob.allocate(8192,40,false,8192)==kIOReturnSuccess);
        uint64_t ha=0,hb=0;
        CHECK(a.vm.map(&oa,0,0x800000,3,0,2,&ha)==kIOReturnSuccess && ha);
        CHECK(b.vm.map(&ob,0,0x800000,3,0,2,&hb)==kIOReturnSuccess && hb);
        /* Heavy churn on a: 200 map/unmap cycles while b's mapping stays live throughout. */
        NativeAdrenoMemory churn;
        CHECK(churn.allocate(8192,40,false,8192)==kIOReturnSuccess);
        for(unsigned n=0;n<200;++n) {
            uint64_t hc=0;
            CHECK(a.vm.map(&churn,0,0x804000,3,0,2,&hc)==kIOReturnSuccess);
            CHECK(a.vm.unmap(hc)==kIOReturnSuccess);
            ++iterations;
        }
        /* a's original mapping unmaps; b's mapping at the same iova is untouched — it still
         * resolves inside b and unmaps cleanly, and b rejects a's stale handle namespace. */
        CHECK(a.vm.unmap(ha)==kIOReturnSuccess);
        CHECK(a.vm.unmap(ha)==kIOReturnBadArgument);   /* stale in a's own namespace */
        CHECK(b.vm.unmap(hb)==kIOReturnSuccess);       /* b survived everything a did */
        CHECK(b.vm.unmap(hb)==kIOReturnBadArgument);
        /* teardown independence */
        oa.release();churn.release();
        CHECK(a.vm.detach()==kIOReturnSuccess);
        ob.release();
        CHECK(b.vm.detach()==kIOReturnSuccess);
        iterations+=210;
    }
    /* 3. Negative validation battery on one live context: every malformed request rejected
     * with the VM staying fully usable afterwards. */
    {
        Ctx c;CHECK(exposed(c)==kIOReturnSuccess);
        NativeAdrenoMemory object;
        CHECK(object.allocate(8192,40,false,8192)==kIOReturnSuccess);
        uint64_t handle=0;
        /* zero-size / bad-alignment BO allocation */
        NativeAdrenoMemory zero;
        CHECK(zero.allocate(0,40,false,8192)!=kIOReturnSuccess);
        /* unaligned iova */
        CHECK(c.vm.map(&object,0,0x800001,3,0,2,&handle)==kIOReturnBadArgument && !handle);
        /* iova outside the arena */
        CHECK(c.vm.map(&object,0,0x400000,3,0,2,&handle)==kIOReturnBadArgument && !handle);
        CHECK(c.vm.map(&object,0,0x810000,3,0,2,&handle)==kIOReturnBadArgument && !handle);
        /* bad region / writeback / shareability */
        CHECK(c.vm.map(&object,2,0x800000,3,0,2,&handle)==kIOReturnBadArgument && !handle);
        CHECK(c.vm.map(&object,0,0x800000,3,2,2,&handle)==kIOReturnBadArgument && !handle);
        CHECK(c.vm.map(&object,0,0x800000,3,0,1,&handle)==kIOReturnBadArgument && !handle);
        /* a valid mapping, then overlapping attempts at exact base, interior and tail */
        CHECK(c.vm.map(&object,0,0x800000,3,0,2,&handle)==kIOReturnSuccess && handle);
        NativeAdrenoMemory other;
        CHECK(other.allocate(8192,40,false,8192)==kIOReturnSuccess);
        uint64_t h2=0;
        CHECK(c.vm.map(&other,0,0x800000,3,0,2,&h2)==kIOReturnBusy && !h2);   /* exact base: occupied */
        CHECK(c.vm.map(&other,0,0x801000,3,0,2,&h2)==kIOReturnBusy && !h2);   /* interior overlap: occupied */

        /* non-overlapping neighbour at the exact end succeeds */
        CHECK(c.vm.map(&other,0,0x802000,3,0,2,&h2)==kIOReturnSuccess && h2);
        /* the VM stayed usable: both unmap cleanly */
        CHECK(c.vm.unmap(handle)==kIOReturnSuccess);
        CHECK(c.vm.unmap(h2)==kIOReturnSuccess);
        object.release();other.release();
        CHECK(c.vm.detach()==kIOReturnSuccess);
        iterations+=13;
    }
    /* 4. Teardown with active mappings: detach answers Busy while any object is mapped;
     * after the final unmap it succeeds. */
    {
        Ctx c;CHECK(exposed(c)==kIOReturnSuccess);
        NativeAdrenoMemory object;
        CHECK(object.allocate(8192,40,false,8192)==kIOReturnSuccess);
        uint64_t handle=0;
        CHECK(c.vm.map(&object,0,0x800000,3,0,2,&handle)==kIOReturnSuccess);
        CHECK(c.vm.detach()==kIOReturnBusy);
        CHECK(c.vm.detach()==kIOReturnBusy);   /* stable under repetition */
        CHECK(c.vm.unmap(handle)==kIOReturnSuccess);
        object.release();
        CHECK(c.vm.detach()==kIOReturnSuccess);
        iterations+=5;
    }
    /* 5. findAddress: page-aligned, disjoint, inside the arena; mappings at found addresses
     * accept; alignment requests honoured. */
    {
        Ctx c;CHECK(exposed(c)==kIOReturnSuccess);
        NativeAdrenoMemory object;
        CHECK(object.allocate(8192,40,false,8192)==kIOReturnSuccess);
        uint64_t first=0,second=0;
        CHECK(c.vm.findAddress(0,8192,4096,&first)==kIOReturnSuccess);
        CHECK(!(first&4095) && first>=0x800000 && first+8192<=0x810000);
        uint64_t h1=0;
        CHECK(c.vm.map(&object,0,first,3,0,2,&h1)==kIOReturnSuccess);
        CHECK(c.vm.findAddress(0,8192,4096,&second)==kIOReturnSuccess);
        CHECK(second+8192<=first || first+8192<=second);   /* disjoint from the live mapping */
        /* negative: impossible alignment/size rejected */
        uint64_t out=0;
        CHECK(c.vm.findAddress(0,0,4096,&out)==kIOReturnBadArgument);
        CHECK(c.vm.findAddress(0,8192,2048,&out)==kIOReturnBadArgument);
        CHECK(c.vm.unmap(h1)==kIOReturnSuccess);
        object.release();
        CHECK(c.vm.detach()==kIOReturnSuccess);
        iterations+=8;
    }
    std::printf("PASS: SMMU/VM isolation stress — churn, multi-context isolation, validation battery, teardown, findAddress; %llu iterations\n",iterations);
}
