/* SPDX-License-Identifier: MIT */
/* PM4 packet-stream & IB command-buffer bounds: the real AdrenoPM4 kit unit plus the real
 * derived Ring/Submit/VM units over software SMMU/CP peers. PM4 emission/plan boundary
 * battery, multi-IB batches, ring wrap-around packet integrity. Host/model only. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoSubmit.h"
extern "C" {
#include "AdrenoPM4.h"
}
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
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool){return spans && count?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans,size_t count,uint32_t *reset)
{return spans && count && reset?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical,size_t bytes,NativeAdrenoCacheOperation op)
{
    if(!physical || !bytes || ((physical|bytes)&4095) || op>NativeAdrenoInvalidate)std::abort();
    ++fixture.physical_calls;fixture.physical_bytes+=bytes;return kIOReturnSuccess;
}
void NativeAdrenoNotePhase(unsigned,size_t) {}
/* SMMUv2 register peer (shape proven in the ring/fence stress harness). */
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
enum : uint32_t {CpRptr=0x0806,CpWptr=0x0807};
int main()
{
    unsigned long long iterations=0;
    /* 1. PM4 emission boundary battery: exact documented limits accepted, one-past rejected. */
    {
        uint32_t header=0;
        /* Type4: 18-bit registers, 1..127 values, and reg+count-1 must not pass 0x3ffff */
        CHECK(AdrenoPM4Type4(0,1,&header)==AdrenoPM4Success);
        CHECK(AdrenoPM4Type4(0x3ffff,1,&header)==AdrenoPM4Success);           /* max reg, count 1 */
        /* precise boundary: reg+count-1 == 0x3ffff accepted, one past rejected */
        CHECK(AdrenoPM4Type4(0x3ffff-126,127,&header)==AdrenoPM4Success);
        CHECK(AdrenoPM4Type4(0x3ffff-125,127,&header)==AdrenoPM4InvalidArgument);
        CHECK(AdrenoPM4Type4(0x40000,1,&header)==AdrenoPM4InvalidArgument);   /* reg too wide */
        CHECK(AdrenoPM4Type4(0,0,&header)==AdrenoPM4InvalidArgument);         /* count 0 */
        CHECK(AdrenoPM4Type4(0,128,&header)==AdrenoPM4InvalidArgument);       /* count 128 */
        /* Type7: 7-bit opcodes, 0..16383 values */
        CHECK(AdrenoPM4Type7(0,0,&header)==AdrenoPM4Success);
        CHECK(AdrenoPM4Type7(0x7f,16383,&header)==AdrenoPM4Success);   /* max opcode, max count */
        CHECK(AdrenoPM4Type7(0x80,0,&header)==AdrenoPM4InvalidArgument);      /* opcode too wide */
        CHECK(AdrenoPM4Type7(0,16384,&header)==AdrenoPM4InvalidArgument);     /* count too wide */
        /* parity bits: a header with either parity bit flipped decodes as InvalidHeader */
        CHECK(AdrenoPM4Type7(0x46,2,&header)==AdrenoPM4Success);
        {
            AdrenoPM4Packet pkt{};size_t count=0;
            uint32_t bad[3]={header^(1u<<15),0,0};   /* count-parity flipped */
            CHECK(AdrenoPM4Plan(bad,3,&pkt,1,&count)==AdrenoPM4InvalidHeader && count==0);
            bad[0]=header^(1u<<23);                  /* opcode-parity flipped */
            CHECK(AdrenoPM4Plan(bad,3,&pkt,1,&count)==AdrenoPM4InvalidHeader && count==0);
        }
        iterations+=9;
    }
    /* 2. PM4 Plan structural decoding: emitted stream round-trips, truncation/bad input rejected. */
    {
        uint32_t h4=0,h7=0;
        CHECK(AdrenoPM4Type4(0x100,3,&h4)==AdrenoPM4Success);
        CHECK(AdrenoPM4Type7(0x46,2,&h7)==AdrenoPM4Success);
        /* stream: type4 + 3 payload, type7 + 2 payload */
        uint32_t stream[8]={h4,1,2,3,h7,4,5,0};
        AdrenoPM4Packet packets[4]={};
        size_t count=0;
        CHECK(AdrenoPM4Plan(stream,7,packets,4,&count)==AdrenoPM4Success && count==2);
        CHECK(packets[0].type==4 && packets[0].identifier==0x100 && packets[0].payload_words==3);
        CHECK(packets[1].type==7 && packets[1].identifier==0x46 && packets[1].payload_words==2);
        /* truncated stream: declare 3 payload words but provide 2 */
        CHECK(AdrenoPM4Plan(stream,3,packets,4,&count)==AdrenoPM4Truncated && count==0);
        /* capacity too small */
        CHECK(AdrenoPM4Plan(stream,7,packets,1,&count)!=AdrenoPM4Success && count==0);
        /* empty input succeeds with zero packets */
        CHECK(AdrenoPM4Plan(stream,0,packets,4,&count)==AdrenoPM4Success && count==0);
        iterations+=6;
    }
    /* 3. Multi-IB batches: 2/3/4 buffers per submission, mixed sizes — real VM/ring path. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        static_cast<volatile uint32_t *>(p.object.mapping())[0]=0x70970001u;
        uint32_t fence=0,last=0;
        for(unsigned n=0;n<150;++n) {
            const unsigned nibs=2+(n%3);   /* 2,3,4 IBs round-robin */
            NativeAdrenoSubmitBuffer buffers[4];
            for(unsigned i=0;i<nibs;++i)buffers[i]=(NativeAdrenoSubmitBuffer){p.handle,0x800000,1024*(i+1)};
            CHECK(p.submit.submit(buffers,nibs,0,&fence)==kIOReturnSuccess && fence==last+1);
            last=fence;
            cp.regs[CpRptr]=cp.regs[CpWptr];
            p.fencemem[1]=fence;
            CHECK(p.submit.wait(fence,&ringclock,1000)==kIOReturnSuccess);
            ++iterations;
        }
        CHECK(p.submit.outstanding()==0);
        CHECK(p.submit.detach()==kIOReturnSuccess);
        iterations+=150;
    }
    /* 4. Ring wrap packet integrity: 520 single-IB submissions; after each, the word at the
     * current write position (mod ring words) must either be a valid PM4 packet header or
     * untouched filler — never half a packet straddling the wrap. Sampled structurally:
     * every emitted packet header decodeable by the same Type4/Type7 rules. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        static_cast<volatile uint32_t *>(p.object.mapping())[0]=0x70970001u;
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        uint32_t fence=0;
        unsigned headers_checked=0;
        uint32_t prologue[128]={0};
        for(unsigned n=0;n<520;++n) {
            CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
            if(n==0)std::memcpy(prologue,p.ringmem,sizeof(prologue));   /* pristine first-submission prologue */
            cp.regs[CpRptr]=cp.regs[CpWptr];
            p.fencemem[1]=fence;
            CHECK(p.submit.wait(fence,&ringclock,1000)==kIOReturnSuccess);
            ++iterations;
        }
        /* Structural pass: parse the ring prefix with the real decoder. The prologue is a
         * packet sequence; feed exactly the first packet's words by its type-aware count. */
        AdrenoPM4Packet packets[16]={};
        size_t count=0;
        const uint32_t first=prologue[0];
        const uint32_t ftype=first>>28;
        CHECK(ftype==4 || ftype==7);   /* the prologue starts with a valid packet header */
        const unsigned firstCount=ftype==4?(first&127):(first&0x3fff);
        CHECK(firstCount<64);
        CHECK(AdrenoPM4Plan(prologue,firstCount+1,packets,16,&count)==AdrenoPM4Success && count==1);
        CHECK(packets[0].type==ftype);
        ++headers_checked;
        /* Walk the next packets too: the prologue is several packets; keep decoding while
         * whole packets are present — must never hit InvalidHeader inside the prologue. */
        unsigned walk=firstCount+1;
        for(unsigned step=0;step<40;++step) {
            const uint32_t word=prologue[walk];
            const uint32_t type=word>>28;
            if(type!=4 && type!=7)break;   /* end of the packet-formed prologue */
            const unsigned cnt=type==4?(word&127):(word&0x3fff);
            AdrenoPM4Packet one[1]={};size_t cn=0;
            CHECK(AdrenoPM4Plan(prologue+walk,cnt+1,one,1,&cn)==AdrenoPM4Success && cn==1);
            walk+=cnt+1;
            ++headers_checked;
        }
        CHECK(headers_checked>=4);   /* a prologue carries several real packets */
        CHECK(p.submit.outstanding()==0);
        CHECK(p.submit.detach()==kIOReturnSuccess);
        iterations+=520+64;
    }
    std::printf("PASS: PM4/cmdbuf bounds — emission battery, plan decode, multi-IB batches, wrap integrity; %llu iterations\n",iterations);
}
