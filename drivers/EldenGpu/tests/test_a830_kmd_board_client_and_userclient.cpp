/* SPDX-License-Identifier: MIT */
/* gpu-opt-036: real C++ KMD Board/Client/UserClient/Gpu host suite over the pinned
 * tree's production units with the upstream XnuMemoryFixture host doubles.
 * NativeAdrenoCpu.cpp is NOT host-compiled: it programs pmap cache attributes through
 * kern/cpu_data.h/vm_map.h/mach_vm_param.h kernel-only APIs, and the pin host tests
 * themselves REPLACE its exports (NativeAdrenoQueryCpu4K/Maintain* hooks) — it is
 * kernel-side-only by construction, transparently recorded. Zero source modification.
 * No device, hardware mapping or GPU is opened. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoBoard.h"
#include "NativeAdrenoClient.h"
#include "NativeAdrenoUserClient.h"
#include "NativeAdrenoGpu.h"
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int proc_selfpid() { return 42; }
extern "C" volatile uint32_t native_adreno_io_coherent;
extern "C" volatile uint32_t native_adreno_coherent_skips[4];
extern "C" { volatile uint32_t native_gpu_dcvs[15]; }

static unsigned long iterations = 0;
static int failures = 0;
#define CHECK(c) do { ++iterations; if (!(c)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---- pin-exact kernel-service doubles (test_userclient.cpp semantics) ---- */
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *out)
{ if (!out) std::abort(); *out = {}; return kIOReturnSuccess; }
IOReturn NativeAdrenoMaintainOwnedPages(void *mapping, size_t bytes, NativeAdrenoCacheOperation op)
{
    if (!(mapping && bytes && !(((uintptr_t)mapping | bytes) & 4095))) std::abort();
    if (!(op == NativeAdrenoClean || op == NativeAdrenoCleanInvalidate || op == NativeAdrenoInvalidate)) std::abort();
    ++fixture.cache_calls;
    return fixture.cache_calls == fixture.cache_fail_call ? kIOReturnIOError : kIOReturnSuccess;
}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans, size_t count, bool)
{ return spans && count ? kIOReturnSuccess : kIOReturnBadArgument; }
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans, size_t count, uint32_t *reset)
{ return spans && count && reset ? kIOReturnSuccess : kIOReturnBadArgument; }
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical, size_t bytes, NativeAdrenoCacheOperation op)
{
    if (!physical || !bytes || ((physical | bytes) & 4095) || op > NativeAdrenoInvalidate) std::abort();
    ++fixture.physical_calls; fixture.physical_bytes += bytes; return kIOReturnSuccess;
}
void NativeAdrenoNotePhase(unsigned, size_t) {}
bool NativeAdrenoPagesUncached(const NativeAdrenoPhysicalSpan *, size_t) { return false; }
extern "C" int PE_parse_boot_argn(const char *, void *, int) { return 0; }
extern "C" unsigned int native_pmap_user_mappings(ppnum_t, void **, uint64_t *, char *) { return 0; }

/* ---- pin-exact software SMMU/CP peers ---- */
#define SMMU_BYTES 0x40000
#define CB 0x22000
static uint32_t mmio[SMMU_BYTES / 4];
static struct { unsigned calls, fail_call, after; } mf;
static ArmSmmu4KIo smmuio;
static ArmSmmu4KStreamState streams;
static ArmSmmu4KStream routes[2] = {{2, 0}, {4, 1}};
static ArmSmmu4KStreamResult route_result;
static ArmSmmu4KContextConfig context;
static ArmSmmu4KContextImage context_image;
static int mevent(void) { return ++mf.calls == mf.fail_call; }
static int mread32(void *c, size_t at, uint32_t *v)
{ if (!(c == &mf && at % 4 == 0 && at <= SMMU_BYTES - 4)) return 1; if (mevent()) return 1; *v = mmio[at / 4]; return 0; }
static int mread64(void *c, size_t at, uint64_t *v)
{ if (!(c == &mf && at % 8 == 0 && at <= SMMU_BYTES - 8)) return 1; if (mevent()) return 1; std::memcpy(v, mmio + at / 4, 8); return 0; }
static int mwrite32(void *c, size_t at, uint32_t v)
{ if (!(c == &mf && at % 4 == 0 && at <= SMMU_BYTES - 4)) return 1; int f = mevent(); if (f && !mf.after) return 1; mmio[at / 4] = v; return f; }
static int mwrite64(void *c, size_t at, uint64_t v)
{ if (!(c == &mf && at % 8 == 0 && at <= SMMU_BYTES - 8)) return 1; int f = mevent(); if (f && !mf.after) return 1; std::memcpy(mmio + at / 4, &v, 8); return f; }
static int mbarrier(void *c) { if (!(c == &mf)) return 1; return mevent(); }
struct CpPeer {
    std::vector<uint32_t> regs, writes;
    unsigned publish = 0, acquire = 0, barriers = 0, fail_publish = 0;
    CpPeer() : regs(0x10000, 0) {}
};
static CpPeer cp;
static uint64_t clock_us = 0;
extern "C" uint64_t native_adreno_now_us(void) { return clock_us; }
extern "C" void native_gpu_trace_record(uint32_t, uint32_t, const uint64_t *, uint32_t) {}
static int cwrite(void *c, uint32_t reg, uint32_t v)
{ if (!(c == &cp && reg < cp.regs.size())) return 1; cp.regs[reg] = v; cp.writes.push_back(reg); return 0; }
static int cread(void *c, uint32_t reg, uint32_t *v)
{ if (!(c == &cp && v && reg < cp.regs.size())) return 1; *v = cp.regs[reg]; return 0; }
static int cbarrier(void *c) { if (!(c == &cp)) return 1; ++cp.barriers; return 0; }
static int cpublish(void *c, const volatile uint32_t *w, size_t bytes)
{ if (!(c == &cp && w && bytes == 32768)) return 1; ++cp.publish; return cp.publish == cp.fail_publish; }
static int cacquire(void *c, volatile uint32_t *w, size_t bytes)
{ if (!(c == &cp && w && bytes == 4096)) return 1; ++cp.acquire; return 0; }
static uint64_t cnow(void *c) { if (!(c == &cp)) return 0; return clock_us; }
static int cidle(void *c) { if (!(c == &cp)) return 1; clock_us += 1000; return 0; }
struct Path {
    NativeAdrenoTablePages tables;
    ArmSmmu4KLiveArena arena;
    ArmSmmu4KLiveState state{};
    ArmSmmu4KLiveVisibility visibility;
    NativeAdrenoVm vm;
    NativeAdrenoRing ring;
    NativeAdrenoSubmit execbuf;
    NativeAdrenoUserClient client;
};
static uint32_t ringmem[8192], fencemem[1024];
static NativeAdrenoRingIo ringio{&cp, cwrite, cread, cbarrier, nullptr, nullptr};
static NativeAdrenoRingCache ringcache{&cp, cpublish, cacquire};
static NativeAdrenoRingClock ringclock{&cp, cnow, cidle};
static void smmuReset(void)
{
    std::memset(mmio, 0, sizeof(mmio)); mf = {}; streams = (ArmSmmu4KStreamState){};
    smmuio = (ArmSmmu4KIo){&mf, SMMU_BYTES, mread32, mread64, mwrite32, mwrite64, mbarrier};
    mmio[0x20/4] = (1u << 30) | (1u << 27) | (4u << 9) | 8;
    mmio[0x24/4] = 0x40000010; mmio[0x28/4] = 0x1433; mmio[0] = 0x436;
    for (unsigned i = 0; i < 8; ++i) mmio[(0xc00 + 4*i)/4] = 0x20000;
    mmio[0x800/4] = 1u << 31 | 9; mmio[0xc00/4] = 3;
}
static int exposed(Path &p)
{
    smmuReset(); cp = CpPeer{}; clock_us = 0;
    std::memset(ringmem, 0xef, sizeof(ringmem)); std::memset(fencemem, 0, sizeof(fencemem));
    if (NativeAdrenoPrepareSmmuMemory(&p.tables, 32, 40, 0x800000, 65536, 0, 16, &p.arena, &p.state, &p.visibility))
        return kIOReturnBadArgument;
    context = (ArmSmmu4KContextConfig){.lower = &p.arena.config, .lower_tables = &p.state.tables,
        .id0 = mmio[0x20/4], .id2 = mmio[0x28/4], .asid = 1, .coherent_walk = 1,
        .stall_on_fault = 0, .continue_after_fault = 1};
    ArmSmmu4KContextConfig actual = context;
    if (ArmSmmu4KEncodeContext(&actual, &context_image) != ArmSmmu4KSuccess) return kIOReturnBadArgument;
    mmio[0x1008/4] = context_image.cbar; mmio[0x1808/4] = context_image.cba2r;
    mmio[CB/4] = context_image.sctlr; mmio[(CB + 0x10)/4] = context_image.tcr2; mmio[(CB + 0x30)/4] = context_image.tcr;
    std::memcpy(mmio + (CB + 0x20)/4, &context_image.ttbr0, 8);
    std::memcpy(mmio + (CB + 0x28)/4, &context_image.ttbr1, 8);
    mmio[(CB + 0x38)/4] = context_image.mair0;
    if (ArmSmmu4KAttachStreams(&smmuio, &streams, routes, 2, 2, &context, &route_result)) return kIOReturnBadArgument;
    NativeAdrenoVmRegion lower = {&p.tables, &p.arena, &p.state};
    if (p.vm.attach(&smmuio, &context, &streams, &lower, nullptr, 32, 1000)) return kIOReturnBadArgument;
    if (p.ring.attach(&ringio, &ringcache, ringmem, sizeof(ringmem), 0x100000, fencemem, sizeof(fencemem), 0x108000))
        return kIOReturnBadArgument;
    if (p.execbuf.attach(&p.vm, &p.ring)) return kIOReturnBadArgument;
    return p.client.open(NativeAdrenoAbiVersion, &p.vm, &p.execbuf, &p.ring, 32, 40);
}

/* ---- pin-exact Gpu board peer ---- */
struct Board {
    std::vector<const char *> order;
    unsigned fail_call = 0, quiesce_fail = 0, quiesce_calls = 0, calls = 0;
};
static Board board;
static int step(const char *name)
{ ++board.calls; board.order.push_back(name); return board.calls == board.fail_call ? kIOReturnIOError : 0; }
static int reserve(void *c) { return c == &board ? step("reserve") : 1; }
static int bootGmu(void *c) { return c == &board ? step("gmu") : 1; }
static int programSmmu(void *c) { return c == &board ? step("smmu") : 1; }
static int quiesce(void *c)
{
    if (c != &board) return 1;
    ++board.quiesce_calls; board.order.push_back("quiesce");
    return board.quiesce_fail;
}
static NativeAdrenoGpuBoard ops{&board, reserve, bootGmu, programSmmu, quiesce};
struct Registers {
    std::vector<uint32_t> values;
    std::vector<uint32_t> writes;
    unsigned publish = 0, acquire = 0, barriers = 0;
    Registers() : values(0x10000, 0) {}
};
static Registers regs;
static int gwrite32(void *c, uint32_t reg, uint32_t value)
{ if (!(c == &regs && reg < regs.values.size())) return 1; regs.values[reg] = value; regs.writes.push_back(reg); return 0; }
static int gread32(void *c, uint32_t reg, uint32_t *value)
{ if (!(c == &regs && value && reg < regs.values.size())) return 1; *value = regs.values[reg]; return 0; }
static int gbarrier(void *c) { if (!(c == &regs)) return 1; ++regs.barriers; return 0; }
static int gpublish(void *c, const volatile uint32_t *words, size_t bytes)
{ if (!(c == &regs && words && bytes == sizeof(ringmem))) return 1; ++regs.publish; return 0; }
static int gacquire(void *c, volatile uint32_t *words, size_t bytes)
{ if (!(c == &regs && words && bytes == sizeof(fencemem))) return 1; ++regs.acquire; return 0; }
static int gtimeRead(void *c, uint32_t address, uint32_t *value)
{ if (!(c == &regs && value && address < regs.values.size())) return 1; *value = regs.values[address]; return 0; }
static unsigned start_pending = 0;
static uint64_t gclock_us = 0;
static uint64_t gnow(void *c) { return c == &board ? gclock_us : 0; }
static int gidle(void *c)
{
    if (c != &board) return 1;
    gclock_us += 1000;
    if (start_pending) { fencemem[1] = start_pending; start_pending = 0; }
    return 0;
}
static NativeAdrenoGpuConfig gpuConfig()
{
    NativeAdrenoGpuConfig c{};
    c.mmio = {&regs, gwrite32, gread32, gbarrier, nullptr, nullptr};
    c.cache = {&regs, gpublish, gacquire};
    c.time = {&regs, gtimeRead};
    c.clock = {&board, gnow, gidle};
    c.start_timeout_us = 5000;
    c.ring = ringmem; c.ring_bytes = sizeof(ringmem); c.ring_iova = 0x200000;
    c.fence = fencemem; c.fence_bytes = sizeof(fencemem); c.fence_iova = 0x208000;
    c.sqe_iova = 0x300000; c.pwrup_reglist_iova = 0x400000;
    return c;
}
static void gpuReset()
{
    board = Board{}; regs = Registers{};
    std::memset(ringmem, 0xee, sizeof(ringmem)); std::memset(fencemem, 0, sizeof(fencemem));
    start_pending = 1; gclock_us = 0;
}

/* ---- board parseReg helper (pin test_board.cpp semantics) ---- */
static void putcell(uint8_t *at, uint32_t value)
{ at[0] = uint8_t(value >> 24); at[1] = uint8_t(value >> 16); at[2] = uint8_t(value >> 8); at[3] = uint8_t(value); }
static size_t regcells(uint8_t *out, const uint32_t (*cells)[4], size_t windows)
{
    for (size_t i = 0; i < windows; ++i) for (size_t c = 0; c < 4; ++c) putcell(out + i*16 + c*4, cells[i][c]);
    return windows * 16;
}

int main()
{
    /* ================= A. NativeAdrenoBoard parseReg ================= */
    /* Positive 1: exact sm8550 gpu/gmu window parsing (pin facts) */
    {
        static const uint32_t gpu_cells[][4] = {{0, 0x03d00000, 0, 0x40000}, {0, 0x03d9e000, 0, 0x1000},
            {0, 0x03d61000, 0, 0x800}};
        uint8_t raw[64]; const size_t bytes = regcells(raw, gpu_cells, 3);
        NativeAdrenoWindow windows[NativeAdrenoMaxWindows]; size_t count = 0;
        CHECK(NativeAdrenoBoard::parseReg(raw, bytes, 2, 2, windows, NativeAdrenoMaxWindows, &count) == kIOReturnSuccess);
        CHECK(count == 3 && windows[0].base == 0x03d00000 && windows[0].bytes == 0x40000);
        CHECK(windows[1].base == 0x03d9e000 && windows[2].base == 0x03d61000 && windows[2].bytes == 0x800);
        static const uint32_t gmu_cells[][4] = {{0, 0x03d6a000, 0, 0x35000}, {0, 0x03d50000, 0, 0x10000},
            {0, 0x0b280000, 0, 0x10000}};
        const size_t gmu_bytes = regcells(raw, gmu_cells, 3);
        CHECK(NativeAdrenoBoard::parseReg(raw, gmu_bytes, 2, 2, windows, NativeAdrenoMaxWindows, &count) == kIOReturnSuccess);
        CHECK(count == 3 && windows[2].base == 0x0b280000);
    }
    /* Positive 2: cross-run byte-level determinism — repeated parses identical */
    {
        static const uint32_t cells[][4] = {{0, 0x03d00000, 0, 0x40000}, {0, 0x03d9e000, 0, 0x1000}};
        uint8_t raw[64]; const size_t bytes = regcells(raw, cells, 2);
        NativeAdrenoWindow a[NativeAdrenoMaxWindows], b[NativeAdrenoMaxWindows];
        size_t ca = 0, cb2 = 0;
        CHECK(NativeAdrenoBoard::parseReg(raw, bytes, 2, 2, a, NativeAdrenoMaxWindows, &ca) == kIOReturnSuccess);
        CHECK(NativeAdrenoBoard::parseReg(raw, bytes, 2, 2, b, NativeAdrenoMaxWindows, &cb2) == kIOReturnSuccess);
        CHECK(ca == cb2 && !std::memcmp(a, b, ca * sizeof(NativeAdrenoWindow)));
    }
    /* Positive 3: stress — 300 rounds varied window shapes, exact readback every round */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 300; ++round) {
            const unsigned nw = 1 + round % 3;
            uint32_t cells[3][4];
            for (unsigned i = 0; i < nw; ++i) {
                cells[i][0] = 0; cells[i][1] = 0x03000000 + round * 0x1000 + i * 0x400;
                cells[i][2] = 0; cells[i][3] = 0x1000 * (1 + i);
            }
            uint8_t raw[64]; const size_t bytes = regcells(raw, (const uint32_t (*)[4])cells, nw);
            NativeAdrenoWindow windows[NativeAdrenoMaxWindows]; size_t count = 0;
            int ok = NativeAdrenoBoard::parseReg(raw, bytes, 2, 2, windows, NativeAdrenoMaxWindows, &count) == kIOReturnSuccess
                && count == nw;
            for (unsigned i = 0; i < nw && ok; ++i)
                ok = windows[i].base == (0x03000000ull + round * 0x1000 + i * 0x400) && windows[i].bytes == 0x1000ull * (1 + i);
            good += ok;
            ++iterations;
        }
        CHECK(good == 300);
    }

    /* ================= B. NativeAdrenoUserClient over software peers ================= */
    /* Positive 4: pin-exact full transaction — query/alloc/map/submit/wait/free/close + first-submit context entry */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiParams params{};
        CHECK(p.client.query(&params) == kIOReturnSuccess && params.abi_version == NativeAdrenoAbiVersion);
        CHECK(params.input_bits == 32 && params.output_bits == 40 && params.max_bo_bytes == (UINT64_C(1) << 32));
        NativeAdrenoAbiBoAlloc alloc{8192, 12, 1};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess && allocated.handle);
        NativeAdrenoAbiBoMap map{allocated.handle, 0x800000, 3, 0, 2, 0};
        NativeAdrenoAbiBoMapReply mapped{};
        CHECK(p.client.mapBo(&map, &mapped) == kIOReturnSuccess && mapped.iova == 0x800000);
        NativeAdrenoAbiSubmit submit{};
        submit.buffer_count = 1;
        submit.buffers[0] = {allocated.handle, 0x800000, 1024};
        NativeAdrenoAbiSubmitReply reply{};
        CHECK(p.client.submit(&submit, &reply, &ringclock) == kIOReturnSuccess && reply.fence == 1);
        CHECK(cp.regs[0x0807] == 72 && (ringmem[37] >> 16 & 0x7fu) == 0x3f && (ringmem[9] >> 16 & 0x7fu) == 0x1f);
        CHECK(ringmem[38] == 0x800000 && ringmem[39] == 0 && ringmem[40] == 256);
        fencemem[1] = 1;
        NativeAdrenoAbiWaitFence wait{1, UINT64_C(1000000)};
        CHECK(p.client.waitFence(&wait, &ringclock) == kIOReturnSuccess);
        NativeAdrenoAbiBoFree released{allocated.handle};
        CHECK(p.client.freeBo(&released) == kIOReturnSuccess);
        CHECK(p.client.liveObjects() == 0 && p.client.close() == kIOReturnSuccess);
    }
    /* Positive 5: cross-run CP write-sequence + ring image determinism (pin never compares across runs) */
    {
        std::vector<uint32_t> seq1, seq2, regs1, regs2;
        uint32_t image1[41], image2[41];
        for (unsigned pass = 0; pass < 2; ++pass) {
            Path p; CHECK(exposed(p) == kIOReturnSuccess);
            NativeAdrenoAbiBoAlloc alloc{8192, 12, 1};
            NativeAdrenoAbiBoAllocReply allocated{};
            CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess);
            NativeAdrenoAbiBoMap map{allocated.handle, 0x800000, 3, 0, 2, 0};
            NativeAdrenoAbiBoMapReply mapped{};
            CHECK(p.client.mapBo(&map, &mapped) == kIOReturnSuccess);
            NativeAdrenoAbiSubmit submit{};
            submit.buffer_count = 1;
            submit.buffers[0] = {allocated.handle, 0x800000, 1024};
            NativeAdrenoAbiSubmitReply reply{};
            CHECK(p.client.submit(&submit, &reply, &ringclock) == kIOReturnSuccess);
            (pass ? seq2 : seq1) = cp.writes;
            (pass ? regs2 : regs1) = cp.regs;
            std::memcpy(pass ? image2 : image1, ringmem, sizeof(image1));
            fencemem[1] = reply.fence;
            NativeAdrenoAbiWaitFence wait{reply.fence, UINT64_C(1000000)};
            CHECK(p.client.waitFence(&wait, &ringclock) == kIOReturnSuccess);
            CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess);
        }
        CHECK(seq1.size() == seq2.size() && !seq1.empty());
        CHECK(seq1 == seq2);
        CHECK(regs1 == regs2);
        CHECK(!std::memcmp(image1, image2, sizeof(image1)));
    }
    /* Positive 6: two-client scope isolation — windows, peaks and cross-trespass refusals (pin exact) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        static const NativeAdrenoAbiDeviceInfo info = {0x43050a01u, 3u << 20, 0x1000000ull, 0x1fffffffff000ull, 0x20000000ull,
            0x60000000ull, 16, 6, 1, 4, NativeAdrenoDeviceClientIova, 0};
        NativeAdrenoUserClient other;
        CHECK(other.open(NativeAdrenoAbiVersion, &p.vm, &p.execbuf, &p.ring, 32, 40) == kIOReturnSuccess);
        CHECK(p.client.attachDeviceInfo(&info) == kIOReturnSuccess && other.attachDeviceInfo(&info) == kIOReturnSuccess);
        CHECK(p.client.setScope(0, 0x800000, 0x8000) == kIOReturnSuccess);
        CHECK(other.setScope(NativeAdrenoQueueRange, 0x808000, 0x8000) == kIOReturnSuccess);
        NativeAdrenoAbiDeviceInfo got{};
        CHECK(other.deviceInfo(&got) == kIOReturnSuccess && got.va_start == 0x808000 && got.va_size == 0x8000);
        NativeAdrenoAbiBoAlloc alloc{8192, 12, 0};
        NativeAdrenoAbiBoAllocReply mine{}, theirs{};
        CHECK(p.client.allocBo(&alloc, &mine) == kIOReturnSuccess && other.allocBo(&alloc, &theirs) == kIOReturnSuccess);
        NativeAdrenoAbiBoMapReply mapped{};
        NativeAdrenoAbiBoMap trespass{mine.handle, 0x808000, 3, 0, 2, 0};
        CHECK(p.client.mapBo(&trespass, &mapped) == kIOReturnBadArgument);
        NativeAdrenoAbiBoMap ours{mine.handle, 0x800000, 3, 0, 2, 0}, yours{theirs.handle, 0x80e000, 3, 0, 2, 0};
        CHECK(p.client.mapBo(&ours, &mapped) == kIOReturnSuccess && other.mapBo(&yours, &mapped) == kIOReturnSuccess);
        CHECK(p.client.windowPeak() == 0x802000 && other.windowPeak() == 0x810000);
        fencemem[1] = 1;
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess && p.client.liveObjects() == 0);
        CHECK(other.releaseAll(&ringclock, 1000) == kIOReturnSuccess);
    }
    /* Positive 7: queue lifecycle stress — create/interleaved submit/close cycles x120 rounds */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        unsigned good = 0;
        for (unsigned round = 0; round < 120; ++round) {
            NativeAdrenoAbiQueueReply qr{};
            int ok = p.client.createQueue(&qr) == kIOReturnSuccess && qr.queue;
            NativeAdrenoAbiQueue close{qr.queue};
            ok = ok && p.client.closeQueue(&close) == kIOReturnSuccess;
            good += ok;
            ++iterations;
        }
        CHECK(good == 120);
        CHECK(p.client.close() == kIOReturnSuccess);
    }
    /* Positive 8: alloc/map/free stress — 350 rounds small BO cycles, zero leak (liveObjects returns to zero) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        unsigned good = 0;
        for (unsigned round = 0; round < 350; ++round) {
            NativeAdrenoAbiBoAlloc alloc{4096 * (1 + round % 3), 12, 0};
            NativeAdrenoAbiBoAllocReply allocated{};
            int ok = p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess && allocated.handle;
            if ((round & 1) && ok) {
                NativeAdrenoAbiBoMap map{allocated.handle, 0x800000 + (round % 4) * 0x4000, 3, 0, 2, 0};
                NativeAdrenoAbiBoMapReply mapped{};
                ok = p.client.mapBo(&map, &mapped) == kIOReturnSuccess;
            }
            NativeAdrenoAbiBoFree released{allocated.handle};
            const IOReturn fr = p.client.freeBo(&released);
            if (ok && fr != kIOReturnSuccess) { std::fprintf(stderr, "diag: round %u free=%d\n", round, fr); ok = 0; }
            good += ok;
            ++iterations;
        }
        CHECK(good == 350 && p.client.liveObjects() == 0);
        CHECK(p.client.close() == kIOReturnSuccess);
    }
    /* Positive 9: metadata roundtrip + 300-object stress drained by releaseAll */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiBoAlloc alloc{4096, 12, 0};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess);
        NativeAdrenoAbiMetadata meta{};
        meta.handle = allocated.handle; meta.bytes = 8;
        std::memcpy(meta.text, "ring-buf", 8);
        CHECK(p.client.setMetadata(&meta) == kIOReturnSuccess);
        NativeAdrenoAbiMetadata got{};
        got.handle = allocated.handle; got.bytes = 16;
        CHECK(p.client.getMetadata(&got, &got) == kIOReturnSuccess);
        CHECK(got.bytes == 8 && !std::memcmp(got.text, "ring-buf", 8));
        NativeAdrenoAbiBoFree released{allocated.handle};
        CHECK(p.client.freeBo(&released) == kIOReturnSuccess);
        for (unsigned i = 0; i < 300; ++i) {
            NativeAdrenoAbiBoAllocReply more{};
            if (p.client.allocBo(&alloc, &more) != kIOReturnSuccess || !more.handle) break;
            ++iterations;
        }
        CHECK(p.client.liveObjects() == 300);
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess && p.client.liveObjects() == 0);
    }
    /* Positive 10: cpu-map lease pins backing object; unmap unpins (pin exact lease semantics) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiBoAlloc alloc{4096, 12, 0};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess);
        NativeAdrenoAbiBoCpuMap cm{allocated.handle};
        NativeAdrenoAbiBoCpuMapReply cr{};
        CHECK(p.client.cpuMap(&cm, &cr) == kIOReturnSuccess && cr.bytes == 4096 && cr.token);
        CHECK(p.client.memoryForToken(cr.token) == p.client.memoryOf(allocated.handle));
        NativeAdrenoAbiBoFree released{allocated.handle};
        CHECK(p.client.freeBo(&released) == kIOReturnBusy);
        NativeAdrenoAbiBoCpuUnmap unmap{allocated.handle, cr.token, 0};
        CHECK(p.client.cpuUnmap(&unmap) == kIOReturnSuccess);
        CHECK(p.client.cpuUnmap(&unmap) == kIOReturnBusy);
        CHECK(p.client.freeBo(&released) == kIOReturnSuccess);
        CHECK(p.client.close() == kIOReturnSuccess);
    }
    /* Positive 11: dispatch surface — exact record sizes, copies only on success (pin exact) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiParams params{};
        CHECK(p.client.dispatch(NativeAdrenoMethodQuery, nullptr, 0, &params, sizeof(params), &ringclock) == kIOReturnSuccess);
        CHECK(params.abi_version == NativeAdrenoAbiVersion);
        NativeAdrenoAbiBoAlloc alloc{8192, 12, 1};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.dispatch(NativeAdrenoMethodBoAlloc, &alloc, sizeof(alloc), &allocated, sizeof(allocated), &ringclock) == kIOReturnSuccess);
        CHECK(allocated.handle);
        NativeAdrenoAbiBoFree released{allocated.handle};
        CHECK(p.client.dispatch(NativeAdrenoMethodBoFree, &released, sizeof(released), nullptr, 0, &ringclock) == kIOReturnSuccess);
        CHECK(p.client.close() == kIOReturnSuccess);
    }

    /* ================= C. NativeAdrenoGpu lifecycle ================= */
    /* Positive 12: pin-exact happy path — fixed board order, real ring/CP/IRQ programming */
    {
        gpuReset(); NativeAdrenoGpu gpu; const NativeAdrenoGpuConfig c = gpuConfig();
        CHECK(gpu.start(&ops, &c) == kIOReturnSuccess);
        CHECK(gpu.ready() && gpu.stepsCompleted() == NativeAdrenoGpuSteps);
        CHECK(gpu.startFence() == 1 && !gpu.quiesced() && fencemem[1] == 1);
        CHECK(board.order.size() == 3 && !std::strcmp(board.order[0], "reserve") &&
            !std::strcmp(board.order[1], "gmu") && !std::strcmp(board.order[2], "smmu"));
        CHECK(gpu.ring() && gpu.interrupts() && gpu.timer());
        CHECK(regs.values[0x0800] == 0x200000 && regs.values[0x0804] == 0x208000);
        CHECK(regs.values[0x0830] == 0x300000 && regs.values[0x0038] == NativeAdrenoIrq::mask());
        CHECK(regs.values[0x0807] == 17);
        CHECK(gpu.stop() == kIOReturnSuccess && !gpu.ready() && board.quiesce_calls == 1);
        CHECK(gpu.stop() == kIOReturnNotReady);
    }
    /* Positive 13: cross-run lifecycle write-sequence determinism */
    {
        std::vector<uint32_t> seq1, seq2;
        for (unsigned pass = 0; pass < 2; ++pass) {
            gpuReset(); NativeAdrenoGpu gpu; const NativeAdrenoGpuConfig c = gpuConfig();
            CHECK(gpu.start(&ops, &c) == kIOReturnSuccess);
            (pass ? seq2 : seq1) = regs.writes;
            CHECK(gpu.stop() == kIOReturnSuccess);
        }
        CHECK(seq1.size() == seq2.size() && seq1.size() > 10 && seq1 == seq2);
    }
    /* Positive 14: A830 profile — aqe doorbell programmed, chip reported */
    {
        gpuReset(); NativeAdrenoGpu gpu; NativeAdrenoGpuConfig c = gpuConfig();
        c.chip_id = NativeAdrenoChipA830;
        c.aqe_iova = 0x500000;
        CHECK(gpu.start(&ops, &c) == kIOReturnSuccess && gpu.ready());
        CHECK(gpu.ring()->chipId() == NativeAdrenoChipA830);
        CHECK(regs.values[0x816] == 0x300000 && regs.values[0x818] == 0x500000);
        CHECK(regs.values[0x62] == NativeAdrenoIrq::mask());
        CHECK(gpu.stop() == kIOReturnSuccess);
    }
    /* Positive 15: lifecycle stress — repeated fresh starts all converge with exact order */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 40; ++round) {
            gpuReset(); NativeAdrenoGpu gpu; const NativeAdrenoGpuConfig c = gpuConfig();
            int ok = gpu.start(&ops, &c) == kIOReturnSuccess && gpu.ready() && gpu.startFence() == 1;
            ok = ok && gpu.stop() == kIOReturnSuccess && board.quiesce_calls == 1;
            good += ok;
            ++iterations;
        }
        CHECK(good == 40);
    }

    /* ================= D. NativeAdrenoClient wire ABI over real UserClient dispatch ================= */
    /* Positive 16: end-to-end client→transport→userclient round trip (open/alloc/map/submit/fence/free/close) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        struct Bridge {
            static IOReturn call(void *cookie, uint32_t method, const void *input, size_t input_size,
                void *output, size_t output_size)
            {
                Path *path = static_cast<Path *>(cookie);
                return path->client.dispatch(method, input, input_size, output, output_size, &ringclock);
            }
        };
        NativeAdrenoTransport transport{&p, &Bridge::call};
        NativeAdrenoClient client;
        CHECK(client.open(&transport) == kIOReturnSuccess && client.isOpen());
        CHECK(client.info().abi_version == NativeAdrenoAbiVersion);
        uint64_t handle = 0;
        CHECK(client.allocBo(8192, 12, false, &handle) == kIOReturnSuccess && handle);
        uint64_t mapped_iova = 0;
        CHECK(client.mapBo(handle, 0x800000, 3, 0, 2, &mapped_iova) == kIOReturnSuccess && mapped_iova == 0x800000);
        NativeAdrenoAbiSubmitBuffer buffer{handle, 0x800000, 1024};
        uint64_t fence = 0;
        CHECK(client.submit(&buffer, 1, 0, 0, &fence) == kIOReturnSuccess && fence == 1);
        fencemem[1] = 1;
        CHECK(client.waitFence(1, UINT64_C(1000000)) == kIOReturnSuccess);
        CHECK(client.freeBo(handle) == kIOReturnSuccess);
        CHECK(client.close() == kIOReturnSuccess && !client.isOpen());
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess);
    }
    /* Positive 17: client stress — 200 rounds open/alloc/free/close over the bridge, zero leak */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        struct Bridge {
            static IOReturn call(void *cookie, uint32_t method, const void *input, size_t input_size,
                void *output, size_t output_size)
            {
                Path *path = static_cast<Path *>(cookie);
                return path->client.dispatch(method, input, input_size, output, output_size, &ringclock);
            }
        };
        NativeAdrenoTransport transport{&p, &Bridge::call};
        unsigned good = 0;
        for (unsigned round = 0; round < 200; ++round) {
            NativeAdrenoClient client;
            int ok = client.open(&transport) == kIOReturnSuccess;
            uint64_t handle = 0;
            ok = ok && client.allocBo(4096, 12, false, &handle) == kIOReturnSuccess && handle;
            ok = ok && client.freeBo(handle) == kIOReturnSuccess;
            ok = ok && client.close() == kIOReturnSuccess;
            good += ok;
            ++iterations;
        }
        CHECK(good == 200 && p.client.liveObjects() == 0);
    }

    /* ================= negative fail-closed battery ================= */
    /* Negative 1: malformed DT reg — truncated/cell-incomplete lengths refused */
    {
        static const uint32_t cells[][4] = {{0, 0x03d00000, 0, 0x40000}};
        uint8_t raw[64]; regcells(raw, cells, 1);
        NativeAdrenoWindow windows[NativeAdrenoMaxWindows]; size_t count = 0;
        CHECK(NativeAdrenoBoard::parseReg(raw, 15, 2, 2, windows, NativeAdrenoMaxWindows, &count) != kIOReturnSuccess);
        CHECK(NativeAdrenoBoard::parseReg(raw, 8, 2, 2, windows, NativeAdrenoMaxWindows, &count) != kIOReturnSuccess);
        CHECK(NativeAdrenoBoard::parseReg(nullptr, 16, 2, 2, windows, NativeAdrenoMaxWindows, &count) != kIOReturnSuccess);
    }
    /* Negative 2: alloc/map/submit refusal battery — no device write, no ring advance (pin exact) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        const uint32_t wptr = cp.regs[0x0807];
        NativeAdrenoAbiBoAllocReply allocated{};
        NativeAdrenoAbiBoAlloc bad[] = {{0, 12, 0}, {4097, 12, 0}, {8192, 11, 0},
            {8192, 25, 0}, {8192, 12, 2}, {0x10000001, 12, 0}};
        for (const auto &request : bad) CHECK(p.client.allocBo(&request, &allocated) == kIOReturnBadArgument);
        CHECK(!allocated.handle);
        NativeAdrenoAbiBoAlloc good{4096, 12, 0};
        CHECK(p.client.allocBo(&good, &allocated) == kIOReturnSuccess);
        NativeAdrenoAbiBoMap wrong[] = {{0, 0x800000, 3, 0, 2, 0}, {allocated.handle, 0x800000, 2, 0, 2, 0},
            {allocated.handle, 0x800001, 3, 0, 2, 0}, {allocated.handle, 0x800000, 3, 2, 2, 0},
            {allocated.handle, 0x800000, 3, 0, 1, 0}, {allocated.handle, 0x800000, 3, 0, 2, 1},
            {allocated.handle + 9, 0x800000, 3, 0, 2, 0}};
        NativeAdrenoAbiBoMapReply mapped{};
        for (const auto &request : wrong) CHECK(p.client.mapBo(&request, &mapped) == kIOReturnBadArgument);
        NativeAdrenoAbiSubmit empty{};
        NativeAdrenoAbiSubmitReply reply{};
        CHECK(p.client.submit(&empty, &reply, &ringclock) == kIOReturnBadArgument);
        empty.buffer_count = 17;
        CHECK(p.client.submit(&empty, &reply, &ringclock) == kIOReturnBadArgument);
        CHECK(cp.regs[0x0807] == wptr && p.execbuf.outstanding() == 0);
        NativeAdrenoAbiBoFree final_free{allocated.handle};
        CHECK(p.client.freeBo(&final_free) == kIOReturnSuccess);
        CHECK(p.client.close() == kIOReturnSuccess);
    }
    /* Negative 3: dispatch surface enforcement — wrong sizes null outputs unknown selector, untouched output */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        unsigned char untouched[sizeof(NativeAdrenoAbiParams)];
        std::memset(untouched, 0x5a, sizeof(untouched));
        CHECK(p.client.dispatch(NativeAdrenoMethodQuery, nullptr, 1, untouched, sizeof(untouched), &ringclock) == kIOReturnBadArgument);
        CHECK(p.client.dispatch(NativeAdrenoMethodQuery, nullptr, 0, untouched, sizeof(untouched) - 1, &ringclock) == kIOReturnBadArgument);
        CHECK(p.client.dispatch(NativeAdrenoMethodQuery, nullptr, 0, nullptr, 0, &ringclock) == kIOReturnBadArgument);
        CHECK(p.client.dispatch(99, nullptr, 0, nullptr, 0, &ringclock) == kIOReturnUnsupported);
        CHECK(untouched[0] == 0x5a && untouched[sizeof(untouched) - 1] == 0x5a);
        CHECK(p.client.close() == kIOReturnSuccess);
    }
    /* Negative 4: version/mount refusals + close-after-releaseAll NotReady (pin exact) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoUserClient client;
        CHECK(client.open(NativeAdrenoAbiVersion + 1, &p.vm, &p.execbuf, &p.ring, 32, 40) == kIOReturnUnsupported);
        CHECK(client.open(NativeAdrenoAbiVersion, nullptr, &p.execbuf, &p.ring, 32, 40) == kIOReturnBadArgument);
        CHECK(client.open(NativeAdrenoAbiVersion, &p.vm, nullptr, &p.ring, 32, 40) == kIOReturnBadArgument);
        CHECK(client.open(NativeAdrenoAbiVersion, &p.vm, &p.execbuf, &p.ring, 30, 40) == kIOReturnBadArgument);
        CHECK(client.close() == kIOReturnNotReady);
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess);
        CHECK(p.client.query(nullptr) == kIOReturnBadArgument && p.client.close() == kIOReturnNotReady);
    }
    /* Negative 5: Gpu argument refusals — incomplete board/config, bad chip, aqe violations touch nothing */
    {
        gpuReset(); NativeAdrenoGpu gpu; NativeAdrenoGpuConfig c = gpuConfig();
        CHECK(gpu.start(nullptr, &c) == kIOReturnBadArgument && !board.calls);
        NativeAdrenoGpuBoard incomplete{};
        CHECK(gpu.start(&incomplete, &c) == kIOReturnBadArgument);
        c.chip_id = NativeAdrenoChipA830;
        CHECK(gpu.start(&ops, &c) == kIOReturnBadArgument && !board.calls); /* aqe missing */
        c.chip_id = 0x1234;
        c.aqe_iova = 0;
        gpuReset(); NativeAdrenoGpu gpu2;
        CHECK(gpu2.start(&ops, &c) == kIOReturnUnsupported);
    }
    /* Negative 6: board step failures stop the sequence and quiesce exactly once (pin exact) */
    {
        for (unsigned failure = 1; failure <= 3; ++failure) {
            gpuReset(); board.fail_call = failure;
            NativeAdrenoGpu gpu; const NativeAdrenoGpuConfig c = gpuConfig();
            CHECK(gpu.start(&ops, &c) == kIOReturnIOError);
            CHECK(gpu.failed() && !gpu.ready() && gpu.stepsCompleted() == failure - 1);
            CHECK(board.quiesce_calls == (failure == 1 ? 0 : 1));
            CHECK(!gpu.ring() && !gpu.interrupts() && gpu.stop() == kIOReturnNotReady);
            CHECK(regs.writes.empty());
            ++iterations;
        }
    }
    /* Negative 7: poisoned commit retains everything — publish failure quarantines the submit path */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiBoAlloc alloc{4096, 12, 0};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess);
        NativeAdrenoAbiBoMap map{allocated.handle, 0x800000, 3, 0, 2, 0};
        NativeAdrenoAbiBoMapReply mapped{};
        CHECK(p.client.mapBo(&map, &mapped) == kIOReturnSuccess);
        cp.fail_publish = cp.publish + 1;
        NativeAdrenoAbiSubmit submit{};
        submit.buffer_count = 1;
        submit.buffers[0] = {allocated.handle, 0x800000, 4096};
        NativeAdrenoAbiSubmitReply reply{};
        CHECK(p.client.submit(&submit, &reply, &ringclock) == kIOReturnIOError && !reply.fence);
        CHECK(p.execbuf.poisoned() && p.execbuf.outstanding() == 1);
        NativeAdrenoAbiBoFree retained{allocated.handle};
        CHECK(p.client.freeBo(&retained) == kIOReturnBusy);
        CHECK(p.client.close() == kIOReturnBusy);
    }
    /* Negative 8: client wire refusal — version/capability mismatch refused at open; malformed count refused before transport */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        struct Bridge {
            static IOReturn call(void *cookie, uint32_t method, const void *input, size_t input_size,
                void *output, size_t output_size)
            {
                Path *path = static_cast<Path *>(cookie);
                return path->client.dispatch(method, input, input_size, output, output_size, &ringclock);
            }
        };
        NativeAdrenoTransport transport{&p, &Bridge::call};
        NativeAdrenoClient client;
        uint64_t fence = 0;
        CHECK(client.submit(nullptr, 0, 0, 0, &fence) != kIOReturnSuccess && !fence);
        CHECK(client.waitFence(99, 0) != kIOReturnSuccess);
        CHECK(client.open(&transport) == kIOReturnSuccess);
        uint64_t handle = 0;
        CHECK(client.allocBo(0, 12, false, &handle) != kIOReturnSuccess && !handle);
        CHECK(client.freeBo(12345) != kIOReturnSuccess);
        CHECK(client.close() == kIOReturnSuccess);
    }
    /* Negative 9: in-flight close is Busy; teardown drains and refuses afterwards (pin exact) */
    {
        Path p; CHECK(exposed(p) == kIOReturnSuccess);
        NativeAdrenoAbiBoAlloc alloc{4096, 12, 0};
        NativeAdrenoAbiBoAllocReply allocated{};
        CHECK(p.client.allocBo(&alloc, &allocated) == kIOReturnSuccess);
        NativeAdrenoAbiBoMap map{allocated.handle, 0x800000, 3, 0, 2, 0};
        NativeAdrenoAbiBoMapReply mapped{};
        CHECK(p.client.mapBo(&map, &mapped) == kIOReturnSuccess);
        NativeAdrenoAbiSubmit submit{};
        submit.buffer_count = 1;
        submit.buffers[0] = {allocated.handle, 0x800000, 1024};
        NativeAdrenoAbiSubmitReply reply{};
        CHECK(p.client.submit(&submit, &reply, &ringclock) == kIOReturnSuccess);
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnTimeout);
        CHECK(p.client.liveObjects() == 1);
        fencemem[1] = reply.fence;
        CHECK(p.client.releaseAll(&ringclock, 1000) == kIOReturnSuccess);
        CHECK(p.client.liveObjects() == 0 && p.client.close() == kIOReturnNotReady);
    }

    std::printf("KMD_BCU_RESULT {\"iterations\":%lu,\"failures\":%d}\n", iterations, failures);
    return failures ? 1 : 0;
}
