/* SPDX-License-Identifier: MIT */
/* The submit-timing/audit-mode/stall-kick port over the same two software
 * register peers as the A830 submit suite: real VM, ring and execbuf code,
 * model SMMU and CP. Hardware remains unproven by this. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoSubmit.h"
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
/* The gpu-audit boot argument the port reads once; the test supplies the answer. */
static uint32_t pe_gpu_audit=0;
extern "C" boolean_t PE_parse_boot_argn(const char *name,void *out,int max)
{CHECK(name && out && max==(int)sizeof(uint32_t));if(std::strcmp(name,"gpu-audit"))return 0;*(uint32_t *)out=pe_gpu_audit;return 1;}
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
};
static uint32_t ringmem[8192],fencemem[1024];
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
static int exposed(Path &p,bool write_combined=false)
{
    smmuReset();cp=CpPeer{};clock_us=0;
    std::memset(ringmem,0xef,sizeof(ringmem));std::memset(fencemem,0,sizeof(fencemem));
    if(NativeAdrenoPrepareSmmuMemory(&p.tables,32,40,0x800000,65536,0,16,&p.arena,&p.state,&p.visibility))
        return kIOReturnBadArgument;
    context=(ArmSmmu4KContextConfig){.lower=&p.arena.config,.upper=nullptr,.lower_tables=&p.state.tables,
        .upper_tables=nullptr,.id0=mmio[0x20/4],.id2=mmio[0x28/4],.asid=1,.coherent_walk=1,
        .stall_on_fault=0,.continue_after_fault=1,.ttbr0_switched=0};
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
    if(write_combined && p.object.makeWriteCombined())return kIOReturnNoMemory;
    if(p.vm.map(&p.object,0,0x800000,3,0,2,&p.handle))return kIOReturnBadArgument;
    if(p.ring.attach(&ringio,&ringcache,ringmem,sizeof(ringmem),0x100000,fencemem,sizeof(fencemem),0x108000))
        return kIOReturnBadArgument;
    return p.submit.attach(&p.vm,&p.ring);
}
void NativeAdrenoNotePhase(unsigned,size_t) {}
/* The unit's diagnostics this suite reads (not in the header: the stall set predates the port's own block). */
extern "C" {extern volatile uint32_t native_adreno_stall[2+NativeAdrenoRingStallWords],native_adreno_stall_mem[4+3*17+4],
    native_adreno_unstick[15],native_adreno_stall_log_count;extern volatile uint64_t native_adreno_stall_log[32][8];}
/* The ring's timeline stamp slot for a fence (the model writes what a real CP would emit). */
static void stampSlot(uint32_t fence,uint64_t start,uint64_t end)
{
    uint32_t *slot=fencemem+(512+(fence&63)*16)/4;
    slot[0]=(uint32_t)start;slot[1]=(uint32_t)(start>>32);slot[2]=(uint32_t)end;slot[3]=(uint32_t)(end>>32);
}
/* The native_adreno_long_jobs row matching a fence and timing source, or -1. The table survives blocks, so the source
 * discriminates a fence submitted twice under different timing sources. */
static int longRow(uint32_t fence,unsigned source)
{
    for(unsigned i=0;i<8;++i)
        if((uint32_t)native_adreno_long_jobs[i][0]==fence && ((native_adreno_long_jobs[i][0]>>52)&0xf)==source)return (int)i;
    return -1;
}
int main()
{
    /* gpu-audit=0 (or the argument absent) ends the write-combine audit even inside the first sixteen submissions; =1 keeps it. */
    {
        Path p;CHECK(exposed(p,true)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        static std::vector<uint64_t> audited;audited.clear();
        native_adreno_line_audit=[](uint64_t physical,size_t bytes,uint32_t *,uint32_t *stale) {
            audited.push_back(physical);audited.push_back(bytes);*stale+=1;return 0;};
        const uint32_t submissions=native_adreno_wc_audit[0];
        uint32_t fence=0;
        native_adreno_audit_mode=0;   /* the cached read already answered 0: every submission skips the audit */
        for(unsigned i=0;i<3;++i) {
            CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
            fencemem[1]=fence;CHECK(p.submit.wait(fence,&ringclock,1000)==kIOReturnSuccess);
        }
        CHECK(audited.empty() && native_adreno_wc_audit[0]==submissions);
        native_adreno_audit_mode=1;
        CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
        CHECK(audited.size()==2 && native_adreno_wc_audit[0]==submissions+1);
        fencemem[1]=fence;CHECK(p.submit.wait(fence,&ringclock,1000)==kIOReturnSuccess);
        native_adreno_line_audit=nullptr;
        CHECK(p.submit.detach()==kIOReturnSuccess);
    }
    /* ~0 reads the boot argument once and caches the answer. */
    {
        pe_gpu_audit=1;native_adreno_audit_mode=~0u;
        Path p;CHECK(exposed(p,true)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        native_adreno_line_audit=[](uint64_t,size_t bytes,uint32_t *,uint32_t *stale){(void)bytes;*stale+=1;return 0;};
        uint32_t fence=0;
        CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess);
        CHECK(native_adreno_audit_mode==1);   /* the argument was consulted once and kept */
        pe_gpu_audit=0;   /* a later change does not re-read */
        const uint32_t submissions=native_adreno_wc_audit[0];
        CHECK(p.submit.submit(&buffer,1,0,&fence)==kIOReturnSuccess && native_adreno_wc_audit[0]==submissions+1);
        fencemem[1]=fence;CHECK(p.submit.waitAll(&ringclock,1000)==kIOReturnSuccess);
        native_adreno_line_audit=nullptr;native_adreno_audit_mode=0;
        CHECK(p.submit.detach()==kIOReturnSuccess);
    }
    /* Retired jobs land in native_adreno_job_time: by the driver's clock when the CP wrote no stamps. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        const uint32_t timed=native_adreno_job_time[0];
        p.submit.nextOwner(1234);p.submit.nextPriority(3);p.submit.nextRole(7);
        clock_us=1000;
        uint32_t fence=0;
        CHECK(p.submit.submit(nullptr,&buffer,1,0,&fence,&ringclock)==kIOReturnSuccess && fence==1);
        fencemem[1]=1;clock_us=5000;
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnSuccess);
        CHECK(native_adreno_job_time[0]==timed+1 && !native_adreno_job_time[1]);
        const int row=longRow(1,2);
        CHECK(row>=0 && native_adreno_long_jobs[row][1]==4000 && native_adreno_long_jobs[row][2]==4096);
        CHECK(((native_adreno_long_jobs[row][0]>>52)&0xf)==2);   /* the driver's clock, an upper bound */
        CHECK(native_adreno_long_jobs[row][4]==0 && (uint32_t)native_adreno_long_jobs[row][3]==1234u);
        CHECK(((native_adreno_long_jobs[row][3]>>32)&0xff)==7 && native_adreno_long_jobs[row][5]==1000);
        /* The next job's start is the later of its submission and the newest retirement seen on its ring. */
        clock_us=6000;
        CHECK(p.submit.submit(nullptr,&buffer,1,0,&fence,&ringclock)==kIOReturnSuccess && fence==2);
        fencemem[1]=2;clock_us=20000;
        CHECK(p.submit.wait(2,&ringclock,1000)==kIOReturnSuccess);
        const int row2=longRow(2,2);
        CHECK(row2>=0 && native_adreno_long_jobs[row2][1]==14000 && native_adreno_job_time[2]==1);
        CHECK(native_adreno_job_time[7]==1);   /* one >8 ms row of native_adreno_slow_jobs */
        CHECK((uint32_t)native_adreno_slow_jobs[0][0]==2 && native_adreno_slow_jobs[0][1]==14000);
        CHECK(p.submit.detach()==kIOReturnSuccess);
    }
    /* The CP's 19.2 MHz stamps win when the ring wrote them (gpu-timeline). */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        p.ring.setTimeline(true);
        clock_us=30000;
        uint32_t fence=0;
        CHECK(p.submit.submit(nullptr,&buffer,1,0,&fence,&ringclock)==kIOReturnSuccess && fence==1);
        fencemem[1]=1;stampSlot(1,1920000,1920000+19200);   /* 1000 us of GPU time */
        clock_us=40000;
        const uint32_t stamped_count=native_adreno_job_time[1];
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnSuccess);
        CHECK(native_adreno_job_time[1]==stamped_count+1);
        const int row=longRow(1,1);
        CHECK(row>=0 && native_adreno_long_jobs[row][1]==1000);
        CHECK(p.submit.detach()==kIOReturnSuccess);
    }
    /* A fence that stalls sees the kick instrumentation: progress sampled, the stage gated on a stopped CP, then the
     * snapshot before the stage, and the kick-to-retirement time filled at the job's release. */
    {
        Path p;CHECK(exposed(p)==kIOReturnSuccess);
        NativeAdrenoSubmitBuffer buffer{p.handle,0x800000,4096};
        p.submit.nextOwner(4321);
        clock_us=1000;
        uint32_t fence=0;
        CHECK(p.submit.submit(nullptr,&buffer,1,0,&fence,&ringclock)==kIOReturnSuccess && fence==1);
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnTimeout);   /* the fence's first timed-out wait */
        clock_us+=60000;
        const uint32_t kicks=native_adreno_stall_kick[25],logs=native_adreno_stall_log_count,stage1=native_adreno_unstick[0];
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnTimeout);
        CHECK(native_adreno_stall_kick[0]==1 && native_adreno_stall_kick[1]>=50000);
        CHECK(native_adreno_stall_kick[22]==4321u && native_adreno_stall_kick[24]==4096);
        CHECK(native_adreno_stall_kick[25]==kicks+1 && native_adreno_stall_kick[21]==0);
        CHECK(native_adreno_unstick[0]==stage1+1);   /* the model's CP did not move: the stage acted */
        CHECK(native_adreno_stall[0]==1 && native_adreno_stall_log_count>logs);   /* snapshot ran before the stage */
        const uint64_t t_kick=clock_us;
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnTimeout && native_adreno_stall_kick[25]==kicks+1);   /* under 50 ms more: none */
        fencemem[1]=1;
        CHECK(p.submit.wait(1,&ringclock,1000)==kIOReturnSuccess);
        CHECK(native_adreno_stall_kick[21]>0 && native_adreno_stall_kick[21]<=clock_us-t_kick+1000);   /* kick us -> retirement seen */
        CHECK(p.submit.detach()==kIOReturnSuccess);
    }
    std::printf("PASS: audit mode gates the wc audit; job timing by clock and CP stamps; top-8 and >8 ms tables; stall kick capture\n");
    std::fflush(stdout);   /* a leak-sanitizer exit path can skip the stdio flush */
    return 0;
}
/* Host doubles for the service-bound symbols the units reference. On Linux the weak externs resolve
 * to 0 by themselves; on macOS a strong trace reference (and the weak_import data symbols) still
 * needs a definition in the image, so the harness supplies inert ones. */
extern "C" volatile uint32_t native_gpu_dcvs[15]={};
extern "C" int native_gpu_trace_enabled(void){return 0;}
extern "C" void native_gpu_trace_record(uint32_t,uint32_t,const uint64_t *,uint32_t){}
extern "C" uint32_t native_gpu_trace_snapshot(void *,uint32_t){return 0;}
extern "C" int native_gpu_pixel_put(const struct NativeGpuPixelPiece *,uint32_t){return 0;}
extern "C" uint32_t native_gpu_pixel_snapshot(void *,uint32_t){return 0;}
extern "C" unsigned int native_pmap_user_mappings(ppnum_t,void **,uint64_t *,char *){return 0;}
/* Physical-page maintenance (imported BOs): record it; no host page is touched. */
/* Host double: the page attribute change of user BOs has no host equivalent (no host page is touched). */
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool){return spans && count?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans,size_t count,uint32_t *reset)
{return spans && count && reset?kIOReturnSuccess:kIOReturnBadArgument;}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical,size_t bytes,NativeAdrenoCacheOperation op)
{
    if(!physical || !bytes || ((physical|bytes)&4095) || op>NativeAdrenoInvalidate)std::abort();
    ++fixture.physical_calls;fixture.physical_bytes+=bytes;return kIOReturnSuccess;
}
bool NativeAdrenoPagesUncached(const NativeAdrenoPhysicalSpan *,size_t){return true;}
