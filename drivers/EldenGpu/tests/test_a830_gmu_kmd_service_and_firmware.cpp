/* SPDX-License-Identifier: MIT */
/* gpu-opt-035: real C++ KMD GMU context/memory + firmware host suite over the pinned
 * tree's production units with the upstream XnuMemoryFixture host doubles.
 * NativeAdrenoGmuService.cpp is NOT host-compiled: it is a gated IOService publisher
 * using real IOKit service APIs (OSDefineMetaClassAndStructors/OSData/IODeviceMemory/
 * OSSharedPtr/registerService) — kernel-side only by the same established precedent as
 * NativeAdrenoPublish.cpp (see tools/test_gpu_maintenance.py); the pin authors never
 * host-tested it either. Its sole logic (DT reg-window parsing) delegates to
 * NativeAdrenoBoard::parseReg, already gate-covered; the unit deliberately performs
 * no register access and contains no HFI bridging by design. This is recorded
 * transparently; all four host-compilable units are covered here with zero source
 * modification. No device, hardware mapping or GPU is opened. */
#include "XnuMemoryFixture.h"
#include "NativeAdrenoFirmware.h"
#include "NativeAdrenoGmuContext.h"
#include "NativeAdrenoSha256.h"
#include "ArmSmmu4KMmio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* The weak empty defaults from NativeAdrenoFirmwareDefaults.cpp. */
extern "C" const uint8_t native_adreno_firmware_image[1];
extern "C" const uint64_t native_adreno_firmware_image_size;
extern "C" const uint8_t native_adreno_sqe_firmware_image[1];
extern "C" const uint64_t native_adreno_sqe_firmware_image_size;

static unsigned long iterations = 0;
static int failures = 0;
#define CHECK(c) do { ++iterations; if (!(c)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---- pin-exact kernel-service doubles ---- */
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *state)
{ CHECK(state); *state = {}; return kIOReturnSuccess; }
IOReturn NativeAdrenoMaintainOwnedPages(void *mapping, size_t bytes, NativeAdrenoCacheOperation op)
{ CHECK(mapping && bytes && op <= NativeAdrenoInvalidate); return kIOReturnSuccess; }
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

/* ---- pin-exact software SMMU peer (test_gmuctx.cpp semantics) ---- */
#define SMMU_BYTES 0x40000
alignas(8) static uint32_t mmio[SMMU_BYTES / 4];
static struct { size_t frozen; unsigned tbi; } peer;
struct WriteEvent { size_t at; uint64_t value; unsigned wide; };
static std::vector<WriteEvent> write_log;
static void peer_reset()
{
    std::memset(mmio, 0, sizeof(mmio)); peer = {}; write_log.clear();
    mmio[0] = 0x002f0406; mmio[0x20/4] = 0x4c017e18; mmio[0x24/4] = 0x40000016; mmio[0x28/4] = 0x1433;
    for (unsigned i = 0; i < 24; ++i) mmio[(0xc00 + 4*i) / 4] = 0x20000;
}
static int read32(void *, size_t at, uint32_t *v)
{ if (!(at % 4 == 0 && at <= SMMU_BYTES - 4)) return 1; *v = mmio[at/4]; return 0; }
static int read64(void *, size_t at, uint64_t *v)
{ if (!(at % 8 == 0 && at <= SMMU_BYTES - 8)) return 1; std::memcpy(v, mmio + at/4, 8); return 0; }
static int write32(void *, size_t at, uint32_t v)
{
    if (!(at % 4 == 0 && at <= SMMU_BYTES - 4)) return 1;
    write_log.push_back({at, v, 0});
    if (peer.frozen && at == peer.frozen) return 0;
    if ((at & 0xfff) == 0x58 && at >= 0x20000) { mmio[at/4] &= ~v; return 0; }
    if (peer.tbi && (at & 0xfff) == 0x10 && at >= 0x20000) v |= 0x60;
    if (peer.tbi && (at & 0xfff) == 0 && at >= 0x20000) v &= ~0x1000u;
    mmio[at/4] = v; return 0;
}
static int write64(void *, size_t at, uint64_t v)
{
    if (!(at % 8 == 0 && at <= SMMU_BYTES - 8)) return 1;
    write_log.push_back({at, v, 1});
    if (peer.frozen && at == peer.frozen) return 0;
    std::memcpy(mmio + at/4, &v, 8); return 0;
}
static int barrier(void *) { return 0; }
static unsigned steps[16], step_count;
static void progress(uint32_t step) { if (step_count < 16) steps[step_count++] = step; }
#ifdef NATIVE_ADRENO_HOST_TEST
extern "C" ArmSmmu4KStatus ArmSmmu4KBindMmio(ArmSmmu4KMmio *window, ArmSmmu4KIo *out)
{
    if (!window || !out || window->bytes != SMMU_BYTES) return ArmSmmu4KUnsupported;
    *out = {nullptr, SMMU_BYTES, read32, read64, write32, write64, barrier};
    return ArmSmmu4KSuccess;
}
#endif

int main()
{
    const ArmSmmu4KIo io{nullptr, SMMU_BYTES, read32, read64, write32, write64, barrier};
    const size_t cb = 0x20000 + 1 * 0x1000;

    /* ================= A. NativeAdrenoFirmware + FirmwareDefaults ================= */
    /* Positive 1: validate accepts a byte-exact image; view content/digest exact */
    static uint8_t fw_image[768];
    for (unsigned i = 0; i < sizeof(fw_image); ++i) fw_image[i] = (uint8_t)(i * 31 + 7);
    NativeAdrenoFirmwareRules rules{};
    rules.bytes = sizeof(fw_image);
    NativeAdrenoSha256::hash(fw_image, sizeof(fw_image), rules.sha256);
    {
        NativeAdrenoFirmwareView view{};
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image), &rules, &view) == kIOReturnSuccess);
        CHECK(view.bytes == fw_image && view.length == sizeof(fw_image));
        CHECK(!std::memcmp(view.sha256, rules.sha256, 32));
    }
    /* Positive 2: cross-run determinism — repeated validation yields identical views */
    {
        NativeAdrenoFirmwareView a{}, b{};
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image), &rules, &a) == kIOReturnSuccess);
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image), &rules, &b) == kIOReturnSuccess);
        CHECK(!std::memcmp(&a, &b, sizeof(a)));
    }
    /* Positive 3: weak firmware defaults exist and are honestly empty — zero length, so validation
     * refuses them fail-closed (nothing runs without real firmware; documented pin contract) */
    {
        CHECK(native_adreno_firmware_image_size == 0 && native_adreno_sqe_firmware_image_size == 0);
        NativeAdrenoFirmwareView view{};
        NativeAdrenoFirmwareRules empty_rules{};
        CHECK(NativeAdrenoFirmware::validate(native_adreno_firmware_image, 0, &empty_rules, &view) != kIOReturnSuccess);
        CHECK(!view.bytes && !view.length);
    }
    /* Positive 4: stress — 320 validation rounds over varied sizes/content, all exact */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 320; ++round) {
            uint8_t image[512];
            const size_t bytes = 64 + (round % 7) * 64;
            for (size_t i = 0; i < bytes; ++i) image[i] = (uint8_t)(i + round);
            NativeAdrenoFirmwareRules rr{};
            rr.bytes = bytes;
            NativeAdrenoSha256::hash(image, bytes, rr.sha256);
            NativeAdrenoFirmwareView view{};
            const int ok = NativeAdrenoFirmware::validate(image, bytes, &rr, &view) == kIOReturnSuccess
                && view.length == bytes && !std::memcmp(view.sha256, rr.sha256, 32);
            good += ok;
            ++iterations;
        }
        CHECK(good == 320);
    }

    /* ================= B. NativeAdrenoGmuMemory allocator ================= */
    /* Positive 5: exact fixed a6xx layout (pin facts), sizes and span shape */
    {
        fixture = MemoryFixture{};
        NativeAdrenoGmuAllocator allocator;
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnSuccess && allocator.ready());
        const NativeAdrenoGmuMemoryLayout &l = allocator.layout();
        CHECK(l.icache_iova == 0x4000 && l.dummy_iova == 0x60000000 && l.debug_iova == 0x60400000);
        CHECK(l.log_iova == 0x60004000 && l.hfi_iova == 0x60008000 && l.end_iova == 0x60407000);
        CHECK(allocator.icache().bytes() == 0xffc000 && allocator.icache().spanCount() == 1);
        CHECK(allocator.hfi().bytes() == 0x4000 && allocator.dummy().bytes() == 0x4000 && allocator.debug().bytes() == 0x7000);
        CHECK(allocator.release() == kIOReturnSuccess && !allocator.ready() && fixture.live.empty());
    }
    /* Positive 6: repeat allocation refused Busy while live; object reusable after full release */
    {
        fixture = MemoryFixture{};
        NativeAdrenoGmuAllocator allocator;
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnSuccess);
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnBusy);
        CHECK(allocator.release() == kIOReturnSuccess);
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnSuccess);
        CHECK(allocator.release() == kIOReturnSuccess && fixture.live.empty());
    }
    /* Positive 7: cross-run physical-layout determinism — two fresh fixtures produce identical span sequences */
    {
        uint64_t run1[16] = {}, run2[16] = {};
        for (unsigned pass = 0; pass < 2; ++pass) {
            fixture = MemoryFixture{};
            NativeAdrenoGmuAllocator allocator;
            CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnSuccess);
            const NativeAdrenoMemory *objects[5] = {&allocator.icache(), &allocator.dummy(), &allocator.debug(),
                &allocator.log(), &allocator.hfi()};
            uint64_t *dst = pass ? run2 : run1;
            unsigned n = 0;
            for (unsigned i = 0; i < 5 && n < 15; ++i)
                for (size_t s = 0; s < objects[i]->spanCount() && n < 15; ++s, ++n)
                    dst[n] = objects[i]->spans()[s].physical ^ (objects[i]->spans()[s].bytes << 32);
            CHECK(allocator.release() == kIOReturnSuccess);
        }
        CHECK(!std::memcmp(run1, run2, sizeof(run1)));
    }
    /* Positive 8: stress — 400 alloc/release rounds over varied small sizes, zero leak every round */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 400; ++round) {
            fixture = MemoryFixture{};
            NativeAdrenoGmuAllocator allocator;
            NativeAdrenoGmuSizes sizes = NativeAdrenoGmuDefaultSizes();
            sizes.icache = 0x4000 * (1 + round % 4);
            sizes.debug = 0x4000 * (1 + round % 2);
            int ok = allocator.allocate(sizes) == kIOReturnSuccess && allocator.ready();
            ok = ok && allocator.release() == kIOReturnSuccess && fixture.live.empty();
            good += ok;
            ++iterations;
        }
        CHECK(good == 400);
    }
    /* Positive 9: 36-bit output-size allocation accepted with exact layout */
    {
        fixture = MemoryFixture{};
        NativeAdrenoGmuAllocator allocator;
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes(), 36) == kIOReturnSuccess);
        CHECK(allocator.layout().end_iova == 0x60407000);
        CHECK(allocator.release() == kIOReturnSuccess && fixture.live.empty());
    }

    /* ================= C. NativeAdrenoGmuContext full bring-up ================= */
    /* Positive 10: pin-exact complete bring-up — seven ordered steps, exact routes/TTBR0/readback */
    {
        peer_reset(); step_count = 0;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, progress, &report) == kIOReturnSuccess);
        CHECK(report.step == NativeAdrenoGmuContext::Ready && report.status == kIOReturnSuccess && context.ready());
        CHECK(step_count == 7);
        unsigned ordered = 1;
        for (unsigned i = 0; i < 7; ++i) if (steps[i] != i + 1) ordered = 0;
        CHECK(ordered);
        CHECK(report.program_enabled && report.program_readback_valid && report.routes_verified == 1 && !report.mismatch);
        CHECK(report.id1 == 0x40000016 && report.spans == 5 && report.id2 == 0x1433 && report.output_bits == 40);
        CHECK((report.s2cr & 0xff) == 1 && ((report.s2cr >> 16) & 3) == 0);
        CHECK(report.smr == ((1u << 31) | 5) && (report.sctlr & 1));
        CHECK(report.ttbr0 == ((uint64_t(1) << 48) | context.allocator().tables().pagePhysical(0)));
    }
    /* Positive 11: cross-run MMIO write-sequence determinism (pin never compares across runs) */
    {
        std::vector<WriteEvent> seq1, seq2;
        for (unsigned pass = 0; pass < 2; ++pass) {
            peer_reset();
            NativeAdrenoGmuContext context;
            NativeAdrenoGmuContextReport report;
            CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnSuccess);
            (pass ? seq2 : seq1) = write_log;
        }
        CHECK(seq1.size() == seq2.size() && seq1.size() > 10);
        unsigned identical = 1;
        for (size_t i = 0; i < seq1.size() && identical; ++i) {
            /* TTBR values embed per-run physical addresses; compare offsets and widths, and values for 32-bit writes. */
            if (seq1[i].at != seq2[i].at || seq1[i].wide != seq2[i].wide) identical = 0;
            if (!seq1[i].wide && seq1[i].value != seq2[i].value) identical = 0;
        }
        CHECK(identical);
    }
    /* Positive 12: 36-bit OAS context — PASize encoding and sub-2^36 table pages (pin exact) */
    {
        peer_reset();
        mmio[0x28/4] = 0x1413;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnSuccess && report.output_bits == 36);
        CHECK((mmio[(cb + 0x10)/4] & 7) == 1);
    }
    /* Positive 13: board entry through the host MMIO binding + payloads in ShengGmuConfig order */
    {
        peer_reset();
        NativeAdrenoGmuContextReport report;
        IOReturn status = NativeAdrenoGmuContextBringup(reinterpret_cast<volatile uint8_t *>(mmio), SMMU_BYTES,
            1, 1, 2, 5, nullptr, &report);
        CHECK(status == kIOReturnSuccess && report.step == NativeAdrenoGmuContext::Ready);
        NativeAdrenoGmuPayload payloads[5];
        CHECK(NativeAdrenoGmuContextPayloads(payloads) == kIOReturnSuccess);
        CHECK(payloads[0].iova == 0x4000 && payloads[0].bytes == 0xffc000);
        CHECK(payloads[1].iova == 0x60000000 && payloads[2].iova == 0x60400000);
        CHECK(payloads[3].iova == 0x60004000 && payloads[4].iova == 0x60008000);
        unsigned nonzero = 1;
        for (unsigned i = 0; i < 5; ++i) if (!payloads[i].physical) nonzero = 0;
        CHECK(nonzero);
    }
    /* Positive 14: repeat-attempt refusal — one bringUp per object even after success */
    {
        peer_reset();
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnSuccess);
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnBusy);
    }
    /* Positive 15: sheng-observed ID2/TCR2 peer variance still converges ready (int-007..009 model) */
    {
        peer_reset();
        mmio[0x28/4] = 0x5511; peer.tbi = 1;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnSuccess && report.step == NativeAdrenoGmuContext::Ready);
        CHECK(report.output_bits == 36 && report.mismatch == ((1u << 3) | (1u << 8)));
        CHECK(report.smr == ((1u << 31) | 5) && (report.sctlr & 1));
    }
    /* Positive 16: multi-context stress — repeated independent fresh bring-ups all converge (fixture reset between) */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 4; ++round) {
            peer_reset();
            NativeAdrenoGmuContext context;
            NativeAdrenoGmuContextReport report;
            good += context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnSuccess
                && report.step == NativeAdrenoGmuContext::Ready && report.spans == 5;
            ++iterations;
        }
        CHECK(good == 4);
    }

    /* ================= negative fail-closed battery ================= */
    /* Negative 1: firmware null/length-mismatch contracts — view never published */
    {
        NativeAdrenoFirmwareView view{};
        CHECK(NativeAdrenoFirmware::validate(nullptr, sizeof(fw_image), &rules, &view) == kIOReturnBadArgument);
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image), nullptr, &view) == kIOReturnBadArgument);
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image), &rules, nullptr) == kIOReturnBadArgument);
        CHECK(NativeAdrenoFirmware::validate(fw_image, sizeof(fw_image) - 1, &rules, &view) == kIOReturnBadArgument);
        CHECK(!view.bytes && !view.length);
    }
    /* Negative 2: firmware hash corruption refused IOError, zeroed view, image never copied */
    {
        uint8_t corrupted[768];
        std::memcpy(corrupted, fw_image, sizeof(corrupted));
        corrupted[100] ^= 1;
        NativeAdrenoFirmwareView view{};
        CHECK(NativeAdrenoFirmware::validate(corrupted, sizeof(corrupted), &rules, &view) == kIOReturnIOError);
        CHECK(!view.bytes && !view.length);
    }
    /* Negative 3: allocator range violations leave nothing behind */
    {
        fixture = MemoryFixture{};
        NativeAdrenoGmuAllocator allocator;
        NativeAdrenoGmuSizes big = NativeAdrenoGmuDefaultSizes();
        big.icache = 0x60000000;
        CHECK(allocator.allocate(big) == kIOReturnBadArgument);
        big = NativeAdrenoGmuDefaultSizes();
        big.hfi = 0x400000;
        CHECK(allocator.allocate(big) == kIOReturnBadArgument);
        big = NativeAdrenoGmuDefaultSizes();
        big.debug = 0x20000000;
        CHECK(allocator.allocate(big) == kIOReturnBadArgument);
        CHECK(!allocator.ready() && fixture.live.empty());
    }
    /* Negative 4: allocator zero/unaligned sizes refused; allocation failure injection is leak-free */
    {
        fixture = MemoryFixture{};
        NativeAdrenoGmuAllocator allocator;
        NativeAdrenoGmuSizes bad = NativeAdrenoGmuDefaultSizes();
        bad.icache = 0;
        CHECK(allocator.allocate(bad) == kIOReturnBadArgument);
        bad = NativeAdrenoGmuDefaultSizes();
        bad.log = 0x800;
        CHECK(allocator.allocate(bad) == kIOReturnBadArgument);
        fixture.allocation_fail = true;
        CHECK(allocator.allocate(NativeAdrenoGmuDefaultSizes()) == kIOReturnNoMemory);
        CHECK(!allocator.ready() && fixture.live.empty());
        fixture.allocation_fail = false;
    }
    /* Negative 5: context argument refusals touch nothing */
    {
        peer_reset();
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(nullptr, 1, 1, 2, 5, nullptr, &report) == kIOReturnBadArgument);
        CHECK(context.bringUp(&io, 64, 1, 2, 5, nullptr, &report) == kIOReturnBadArgument);
        CHECK(context.bringUp(&io, 1, 0, 2, 5, nullptr, &report) == kIOReturnBadArgument);
        CHECK(context.bringUp(&io, 1, 1, 128, 5, nullptr, &report) == kIOReturnBadArgument);
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, nullptr) == kIOReturnBadArgument);
        CHECK(report.step == 0);
    }
    /* Negative 6: hypervisor-dropped TCR write caught by readback — bank never enabled, SMR invalid */
    {
        peer_reset(); peer.frozen = cb + 0x30;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) != kIOReturnSuccess);
        CHECK(report.step == NativeAdrenoGmuContext::Program && !report.program_enabled);
        CHECK(report.mismatch == (1u << 2));
        CHECK(!(mmio[cb/4] & 1) && !(mmio[(0x800 + 8)/4] & (1u << 31)));
    }
    /* Negative 7: non-FAULT S2CR readback refuses attach after programming; route never verified */
    {
        peer_reset();
        mmio[(0xc00 + 8)/4] = 0x10000;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) != kIOReturnSuccess);
        CHECK(report.step == NativeAdrenoGmuContext::Attach && report.program_enabled && !report.routes_verified);
        CHECK(!context.ready());
    }
    /* Negative 8: already-enabled bank refused before any write */
    {
        peer_reset(); mmio[cb/4] = 1;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) != kIOReturnSuccess);
        CHECK(report.step == NativeAdrenoGmuContext::Program && report.program_writes == 0);
    }
    /* Negative 9: unsupported OAS encoding refused before allocation/write; no-4K-granule ID2 refused phase 1 */
    {
        peer_reset(); mmio[0x28/4] = 0x14f3;
        NativeAdrenoGmuContext context;
        NativeAdrenoGmuContextReport report;
        CHECK(context.bringUp(&io, 1, 1, 2, 5, nullptr, &report) == kIOReturnUnsupported);
        CHECK(report.step == NativeAdrenoGmuContext::Allocate && !(mmio[cb/4] & 1));
        peer_reset(); mmio[0x28/4] = 0x0433;
        NativeAdrenoGmuContext context2;
        CHECK(context2.bringUp(&io, 1, 1, 2, 5, nullptr, &report) != kIOReturnSuccess);
        CHECK(report.step == NativeAdrenoGmuContext::Program && report.program_phase == 1 && report.program_writes == 0);
    }

    std::printf("GMU_KMD_RESULT {\"iterations\":%lu,\"failures\":%d}\n", iterations, failures);
    return failures ? 1 : 0;
}
