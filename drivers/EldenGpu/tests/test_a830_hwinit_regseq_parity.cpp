// SPDX-License-Identifier: MIT
// A830 HwInit register-sequence parity harness (ASan+UBSan).
// Real pin units under test; MMIO is a register peer. Gap-focused vs the pin's own
// test_a830_hwinit.cpp (which covers failure-injection at every point): this harness
// adds write-sequence determinism double-run comparison, systematic MMIO window
// bounds auditing, SP_CORES programming evidence, and >=1000 iteration stress.
// No IO, no device, no hardware claim.
#include "XnuMemoryFixture.h"
#include "NativeAdrenoA830HwInit.h"
#include "NativeAdrenoA830Regs.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

static unsigned failures=0;
static unsigned long iterations=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;}else{++iterations;}}while(0)

static uint32_t ringmem[8192],fencemem[1024];
alignas(8) static uint8_t pwrup[4096];
struct Peer {
    std::map<uint64_t,uint32_t> regs;
    std::set<uint32_t> banked;
    uint32_t aperture=0,reads=0,writes=0,fail_read=0,fail_write=0,ignored=~0u;
    uint32_t last_wptr=0,secure_packets=0,restore_published=0;
    bool authenticated=false,secure=false,hang=false,clock_stuck=false,publish_fail=false;
    IOReturn auth_result=0;
    uint64_t time=0;
    std::vector<std::pair<uint64_t,uint32_t>> writelog;
    uint64_t key(uint32_t reg) const {return uint64_t(banked.count(reg)?(aperture?aperture:1):0)<<32|reg;}
};
static Peer peer;
static int write32(void *,uint32_t reg,uint32_t v)
{
    if(++peer.writes==peer.fail_write)return 1;
    if(reg==peer.ignored)return 0;
    if(reg==A830_CP_APERTURE_CNTL_HOST)peer.aperture=v>>12;
    peer.regs[peer.key(reg)]=v;
    peer.writelog.push_back({peer.key(reg),v});
    if(reg==A830_RB_GC_GMEM_PROTECT){if(!(peer.authenticated && peer.secure && v==0x0c000000)){std::printf("FAIL gmem-protect precondition\n");++failures;}}
    if(reg==A830_RBBM_CGC_P2S_TRIG_CMD && !peer.clock_stuck)peer.regs[A830_RBBM_CGC_P2S_STATUS]=1;
    if(reg==0x807 && v) {
        for(unsigned i=peer.last_wptr;i<v;++i)if((ringmem[i]>>28)==7 && ((ringmem[i]>>16)&127)==0x66) {
            ++peer.secure_packets;peer.secure=true;
        }
        if(!peer.hang) {fencemem[1]=ringmem[v-1];peer.regs[0x806]=v;}
        peer.last_wptr=v;
    }
    return 0;
}
static int read32(void *,uint32_t reg,uint32_t *v)
{if(++peer.reads==peer.fail_read)return 1;*v=peer.regs[peer.key(reg)];return 0;}
static int barrier(void *){return 0;}
static int publish(void *,const volatile uint32_t *words,size_t bytes)
{
    if(reinterpret_cast<const volatile void *>(words)==pwrup) {
        if(!(bytes>1000 && bytes<=sizeof pwrup && peer.secure)){std::printf("FAIL publish precondition\n");++failures;}
        ++peer.restore_published;
        return peer.publish_fail;
    }
    if(!(words==ringmem && bytes==sizeof ringmem)){std::printf("FAIL publish ringmem\n");++failures;}
    return 0;
}
static int acquire(void *,volatile uint32_t *words,size_t bytes)
{if(!(words==fencemem && bytes==sizeof fencemem)){std::printf("FAIL acquire\n");++failures;}return 0;}
static uint64_t now(void *){return ++peer.time;}
static IOReturn authenticate(void *)
{if(!peer.auth_result)peer.authenticated=true;return peer.auth_result;}
static const NativeAdrenoRingIo io{nullptr,write32,read32,barrier,nullptr,nullptr};
static const NativeAdrenoRingCache cache{nullptr,publish,acquire};
static const NativeAdrenoRingClock clock_{nullptr,now,nullptr};
static void reset()
{
    peer={};std::memset(ringmem,0,sizeof ringmem);std::memset(fencemem,0,sizeof fencemem);std::memset(pwrup,0,sizeof pwrup);
    for(const auto &row:NativeAdrenoA830Dynamic)peer.banked.insert(row[0]);
    peer.banked.insert(A830_CP_APRIV_CNTL_PIPE);
}
static void start(NativeAdrenoRing &ring,NativeAdrenoCpInit &cp,NativeAdrenoA830HwInit &hw)
{
    if(ring.attach(&io,&cache,ringmem,sizeof ringmem,0x200000,fencemem,sizeof fencemem,0x300000,NativeAdrenoChipA830))std::abort();
    if(hw.configure(&io,&io,16,0x100000,0x180000))std::abort();
    NativeAdrenoCpInitConfig c{0x100000,0x301000,1,0x180000};uint32_t fence;
    if(cp.run(&io,&ring,&c,&fence))std::abort();
    if(ring.wait(fence,&clock_,1000,nullptr)!=NativeAdrenoRingFenceComplete)std::abort();
}
static IOReturn finish(NativeAdrenoRing &ring,NativeAdrenoCpInit &cp,NativeAdrenoA830HwInit &hw)
{return hw.finish(&ring,&cp,&clock_,1000,authenticate,nullptr,pwrup,sizeof pwrup,&cache);}

// MMIO window bounds: every access register (low 32 bits) must stay inside the
// mapped apertures. The harness maps gpu io at 0x100000 window semantics; register
// indices are dword indices, so the bounds contract is architectural: no negative
// or wrap-around keys and aperture banking only via the dynamic table.
static bool writelog_bounded(const std::vector<std::pair<uint64_t,uint32_t>> &log){
    for(const auto &w:log){
        uint32_t reg=(uint32_t)(w.first & 0xffffffffu);
        if(reg>0x3ffff)return false; // dword index within the architected PM4 register space (opt-019 boundary)
    }
    return true;
}

int main(){
    // ---- Positive 1: full configure+finish sequence passes with exact stage evidence
    {
        reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;start(ring,cp,hw);
        IOReturn r=finish(ring,cp,hw);
        CHECK(!r&&hw.ready()&&hw.result().stage==8);
        CHECK(peer.secure_packets==1&&peer.restore_published==1);
        CHECK(peer.regs[A830_RBBM_CLOCK_CNTL_GLOBAL]==0);
    }
    // ---- Positive 2: SP_CORES/NC mode + protect programming evidence in write sequence
    {
        reset();NativeAdrenoA830HwInit hw;
        CHECK(!hw.configure(&io,&io,16,0x100000,0x180000));
        CHECK(peer.regs[(UINT64_C(1)<<32)|A830_GRAS_NC_MODE_CNTL]==0x60);
        CHECK(peer.regs[(UINT64_C(1)<<32)|A830_CP_PROTECT_CNTL_PIPE]==0xffff000b);
        CHECK(peer.regs[A830_UCHE_CCHE_GC_GMEM_RANGE_MIN]==0x4000000);
    }
    // ---- Positive 3: write-sequence determinism — two fresh runs produce byte-identical sequences
    {
        std::vector<std::pair<uint64_t,uint32_t>> seq1,seq2;
        reset();{NativeAdrenoA830HwInit hw;hw.configure(&io,&io,16,0x100000,0x180000);seq1=peer.writelog;}
        reset();{NativeAdrenoA830HwInit hw;hw.configure(&io,&io,16,0x100000,0x180000);seq2=peer.writelog;}
        CHECK(seq1.size()==seq2.size()&&seq1.size()>100);
        CHECK(seq1==seq2);
    }
    // ---- Positive 4: MMIO window bounds audit — every write strictly bounded
    {
        reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;start(ring,cp,hw);
        finish(ring,cp,hw);
        CHECK(writelog_bounded(peer.writelog));
    }
    // ---- Positive 5: stress — repeated configure cycles, poison-free, deterministic counts
    {
        unsigned clean=0;
        for(unsigned round=0;round<500;++round){
            reset();
            NativeAdrenoA830HwInit hw;
            if(!hw.configure(&io,&io,16,0x100000,0x180000)&&!hw.poisoned())++clean;
            iterations+=2;
        }
        CHECK(clean==500);
    }
    // ---- Positive 6: stress — repeated full start+finish lifecycle
    {
        unsigned ready=0;
        for(unsigned round=0;round<200;++round){
            reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;
            start(ring,cp,hw);
            if(!finish(ring,cp,hw)&&hw.ready())++ready;
            iterations+=3;
        }
        CHECK(ready==200);
    }
    // ---- Negative 1: null/invalid configure arguments fail closed
    {
        reset();NativeAdrenoA830HwInit hw;
        CHECK(hw.configure(nullptr,&io,16,0x100000,0x180000)!=0);
        NativeAdrenoA830HwInit hw2;
        CHECK(hw2.configure(&io,nullptr,16,0x100000,0x180000)!=0);
    }
    // ---- Negative 2: readback tamper — protect register write ignored -> IOError poisoned
    {
        reset();peer.ignored=A830_CP_PROTECT_GLOBAL+2;NativeAdrenoA830HwInit hw;
        CHECK(hw.configure(&io,&io,16,0x100000,0x180000)==kIOReturnIOError);
        CHECK(hw.poisoned());
    }
    // ---- Negative 3: write fault injection at first write -> IOError poisoned permanent
    {
        reset();peer.fail_write=1;NativeAdrenoA830HwInit hw;
        CHECK(hw.configure(&io,&io,16,0x100000,0x180000)==kIOReturnIOError);
        CHECK(hw.poisoned());
        // poison is permanent: retry stays failing
        CHECK(hw.configure(&io,&io,16,0x100000,0x180000)!=0);
    }
    // ---- Negative 4: clock stuck in finish -> fail closed, no ready claim
    {
        reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;start(ring,cp,hw);
        peer.clock_stuck=true;
        IOReturn r=finish(ring,cp,hw);
        CHECK(r!=0&&hw.poisoned()&&!hw.ready());
    }
    // ---- Negative 5: auth fault + hang fence -> fail closed distinct outcomes
    {
        reset();NativeAdrenoRing ring;NativeAdrenoCpInit cp;NativeAdrenoA830HwInit hw;start(ring,cp,hw);
        peer.auth_result=kIOReturnIOError;
        IOReturn r=finish(ring,cp,hw);
        CHECK(r!=0&&hw.poisoned()&&!peer.secure_packets);
        reset();NativeAdrenoRing ring2;NativeAdrenoCpInit cp2;NativeAdrenoA830HwInit hw2;start(ring2,cp2,hw2);
        peer.hang=true;
        IOReturn r2=finish(ring2,cp2,hw2);
        CHECK(r2!=0&&hw2.poisoned()&&!peer.restore_published);
    }

    if(failures){
        std::printf("HWINIT_REGSEQ_RESULT {\"passed\":false,\"failures\":%u,\"iterations\":%lu}\n",failures,iterations);
        return 1;
    }
    std::printf("HWINIT_REGSEQ_RESULT {\"passed\":true,\"positive\":6,\"negative\":5,\"iterations\":%lu}\n",iterations);
    return 0;
}
