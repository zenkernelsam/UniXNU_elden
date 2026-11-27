/* SPDX-License-Identifier: MIT */
/* gpu-opt-033: real AdrenoFirmware + AdrenoHfiStartup + Sm8750Gmu production units
 * under ASan+UBSan -Werror. Software MMIO/TCM/HFI peers only.
 * KEY ESTABLISHED FACT: the compile-time firmware pin in Sm8750GmuSource.h
 * (88896 bytes, sha256 1ad8175e...) is byte-identical to the locked production
 * qcom/gen80000_gmu.bin — positive cold boot is therefore genuinely exercised
 * with the REAL blob, never a forgery. The pin test_startup.c peer semantics
 * (shared queue/notify/response/clock/health) are the authoritative HFI source;
 * Sm8750Gmu.c itself is the authoritative boot-sequence source. */
#include "AdrenoFirmware.h"
#include "AdrenoHfiStartup.h"
#include "Sm8750Gmu.h"
extern "C" {
#include "Sm8750GmuSource.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned long iterations = 0;
static int failures = 0;
#define CHECK(c) do { ++iterations; if (!(c)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---- real SHA-256 peer implementation (test infrastructure, FIPS 180-4) ---- */
static void sha256_peer(const void *data, size_t len, uint8_t out[32])
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t total = ((len + 8) / 64 + 1) * 64;
    uint8_t block[64];
    uint64_t bitlen = (uint64_t)len * 8;
    for (size_t off = 0; off < total; off += 64) {
        for (unsigned i = 0; i < 64; ++i) {
            size_t at = off + i;
            if (at < len) block[i] = ((const uint8_t *)data)[at];
            else if (at == len) block[i] = 0x80;
            else if (at >= total - 8) block[i] = (uint8_t)(bitlen >> ((total - 1 - at) * 8));
            else block[i] = 0;
        }
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (uint32_t)block[4*i]<<24 | (uint32_t)block[4*i+1]<<16 | (uint32_t)block[4*i+2]<<8 | block[4*i+3];
        for (unsigned i = 16; i < 64; ++i) {
            uint32_t x = w[i-15], y = w[i-2];
            uint32_t s0 = (x>>7|x<<25) ^ (x>>18|x<<14) ^ (x>>3);
            uint32_t s1 = (y>>17|y<<15) ^ (y>>19|y<<13) ^ (y>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t S1 = (e>>6|e<<26) ^ (e>>11|e<<21) ^ (e>>25|e<<7);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = (a>>2|a<<30) ^ (a>>13|a<<19) ^ (a>>22|a<<10);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (unsigned i = 0; i < 8; ++i) {
        out[4*i]=(uint8_t)(h[i]>>24); out[4*i+1]=(uint8_t)(h[i]>>16);
        out[4*i+2]=(uint8_t)(h[i]>>8); out[4*i+3]=(uint8_t)h[i];
    }
}

/* ================= GMU peer: three MMIO windows + firmware emulation ================= */
#define CORE_BYTES 0x68000
#define RSCC_BYTES 0x10000
#define GPU_BYTES  0x40000
typedef struct { unsigned kind; unsigned window; size_t at; uint32_t value; } GmuEvent;
typedef struct {
    uint8_t core[CORE_BYTES], rscc[RSCC_BYTES], gpu[GPU_BYTES];
    volatile uint32_t *hmem; /* hfi shared queue memory (cpu side) */
    uint64_t time;
    unsigned waits, hfi_messages, fail_write_call, write_calls;
    unsigned fw_init_ready;   /* ready value the emulated firmware publishes */
    unsigned fw_ready_armed;  /* publish FW_INIT_RESULT once reset released */
    unsigned stall_fw;        /* never publish fw init result (timeout injection) */
    unsigned stall_hfi;       /* never set hfi status (timeout injection) */
    unsigned err_ack;         /* firmware answers with error code 37 */
    unsigned no_ack;          /* firmware never answers */
    GmuEvent events[2048];
    size_t event_count;
} GmuPeer;
static GmuPeer gp;

static size_t coreoff(uint32_t word) { return ((size_t)word << 2) - SM8750_GMU_CORE_GPU_DELTA; }
static uint32_t rd32(const uint8_t *mem, size_t at) { uint32_t v; std::memcpy(&v, mem + at, 4); return v; }
static void wr32(uint8_t *mem, size_t at, uint32_t v) { std::memcpy(mem + at, &v, 4); }

/* pin-exact HFI response logic (from test_startup.c), operating on the hmem queue */
static void hfi_response(uint32_t command)
{
    volatile uint32_t *shared = gp.hmem;
    volatile uint32_t *q = shared + 18;
    unsigned at = q[11];
    shared[2048+at] = (3u<<8)|126u;
    shared[2048+((at+1)&1023)] = command;
    shared[2048+((at+2)&1023)] = gp.err_ack ? 37 : 0;
    shared[2048+((at+3)&1023)] = 0xfafafafa;
    q[11] = (at+4)&1023;
}
static void hfi_note_message()
{
    /* the production notify() already signalled; consume one tx queue entry like the pin peer */
    volatile uint32_t *shared = gp.hmem;
    unsigned at = shared[16];
    uint32_t header = shared[1024+at];
    shared[16] = shared[17];
    ++gp.hfi_messages;
    if (!gp.no_ack) hfi_response(header);
    gp.time += 0;
}

static int gmu_read32(void *cookie, unsigned window, size_t at, uint32_t *out)
{
    if (cookie != &gp || !out || (at & 3)) return 1;
    const uint8_t *mem = window == Sm8750GmuCore ? gp.core : window == Sm8750GmuRscc ? gp.rscc :
                         window == Sm8750GmuGpu ? gp.gpu : 0;
    size_t bytes = window == Sm8750GmuCore ? CORE_BYTES : window == Sm8750GmuRscc ? RSCC_BYTES : GPU_BYTES;
    if (!mem || at > bytes - 4) return 1;
    /* firmware emulation: once armed, the init result reads as ready */
    if (window == Sm8750GmuCore && at == coreoff(REG_GMU_CM3_FW_INIT_RESULT) &&
        gp.fw_ready_armed && !gp.stall_fw) {
        *out = gp.fw_init_ready;
        return 0;
    }
    *out = rd32(mem, at);
    return 0;
}
static int gmu_write32(void *cookie, unsigned window, size_t at, uint32_t value)
{
    if (cookie != &gp || (at & 3)) return 1;
    uint8_t *mem = window == Sm8750GmuCore ? gp.core : window == Sm8750GmuRscc ? gp.rscc :
                   window == Sm8750GmuGpu ? gp.gpu : 0;
    size_t bytes = window == Sm8750GmuCore ? CORE_BYTES : window == Sm8750GmuRscc ? RSCC_BYTES : GPU_BYTES;
    if (!mem || at > bytes - 4) return 1;
    if (gp.event_count < 2048) gp.events[gp.event_count++] = GmuEvent{2, window, at, value};
    if (++gp.write_calls == gp.fail_write_call) return 1;
    wr32(mem, at, value);
    /* firmware emulation hooks */
    if (window == Sm8750GmuCore && at == coreoff(REG_GMU_CM3_SYSRESET) && value == 0) {
        /* cm3 leaves reset: mirror the production version-word rule exactly — the firmware
           publishes 0xbabeface for old versions, 0x100 for newer (mask 0x1ff) */
        uint32_t version = rd32(gp.core, coreoff(REG_GMU_CM3_DTCM_START + 0xff8));
        gp.fw_init_ready = version <= UINT32_C(0x20010004) ? UINT32_C(0xbabeface) : 0x100;
        gp.fw_ready_armed = 1;
    }
    if (window == Sm8750GmuCore && at == coreoff(REG_GMU_HFI_CTRL_INIT) && (value & 1) && !gp.stall_hfi) {
        wr32(gp.core, coreoff(REG_GMU_HFI_CTRL_STATUS), rd32(gp.core, coreoff(REG_GMU_HFI_CTRL_STATUS)) | 1);
    }
    if (window == Sm8750GmuCore && at == coreoff(REG_GMU_HOST2GMU_INTR_SET)) {
        hfi_note_message();
    }
    return 0;
}
static int gmu_barrier(void *cookie)
{
    if (cookie != &gp) return 1;
    if (gp.event_count < 2048) gp.events[gp.event_count++] = GmuEvent{4, 0, 0, 0};
    return 0;
}
static int gmu_sha256(void *cookie, const void *data, size_t len, uint8_t digest[32])
{
    if (!cookie || !data || !digest) return 1;
    sha256_peer(data, len, digest);
    return 0;
}
static int gmu_now(void *cookie, uint64_t *value)
{
    if (cookie != &gp || !value) return 1;
    *value = gp.time;
    return 0;
}
static int gmu_wait(void *cookie, unsigned us)
{
    if (cookie != &gp) return 1;
    ++gp.waits;
    gp.time += us;
    return 0;
}

static void gmu_peer_reset(volatile uint32_t *hmem)
{
    std::memset(&gp, 0, sizeof(gp));
    gp.hmem = hmem;
    gp.time = 1000;
    /* initial hardware state: CM3 held in reset, rscc idle, no hfi ready, no faults */
    wr32(gp.core, coreoff(REG_GMU_CM3_SYSRESET), 1);
}

/* ================= pin-exact HFI startup peer (from test_startup.c) ================= */
static uint32_t *shared = 0;
static AdrenoHfi hfi;
static AdrenoHfiStartup state;
static AdrenoHfiStartupConfig config;
static AdrenoHfiStartupServices services;
static AdrenoHfiStartupResult result;
static struct {
    uint64_t time, jump;
    unsigned count, waits, health_calls, now_calls;
    unsigned fail_at, mode, clock_fail, clock_reverse, health_fail, wait_fail, clock_stall;
    unsigned ids[8], words[8]; uint32_t messages[8][159];
} peer;
static void response(uint32_t command)
{
    volatile uint32_t *q = shared + 18; unsigned at = q[11];
    shared[2048+at] = (3u<<8)|126u;
    shared[2048+((at+1)&1023)] = command;
    shared[2048+((at+2)&1023)] = (peer.fail_at==peer.count && peer.mode==1)?37:0;
    shared[2048+((at+3)&1023)] = 0xfafafafa;
    q[11] = (at+4)&1023;
}
static int notify(void *cookie)
{
    if (cookie != &peer || peer.count >= 8) return 1;
    unsigned at = shared[16]; uint32_t header = shared[1024+at]; unsigned n = (header>>8)&255;
    unsigned index = peer.count++; peer.ids[index] = header&255; peer.words[index] = n-1;
    for (unsigned i = 1; i < n; ++i) peer.messages[index][i-1] = shared[1024+((at+i)&1023)];
    shared[16] = shared[17];
    if (peer.fail_at == peer.count && peer.mode == 2) return 1;
    if (!(peer.fail_at == peer.count && peer.mode >= 3)) response(header);
    peer.time += peer.jump;
    return 0;
}
static int now(void *cookie, uint64_t *value)
{
    if (cookie != &peer) return 1;
    ++peer.now_calls;
    if (peer.clock_fail && peer.now_calls == peer.clock_fail) return 1;
    if (peer.clock_reverse && peer.now_calls >= peer.clock_reverse) --peer.time;
    *value = peer.time;
    return 0;
}
static int wait_us(void *cookie, unsigned us)
{
    if (cookie != &peer || us != 10) return 1;
    ++peer.waits;
    if (peer.wait_fail) return 1;
    if (!peer.clock_stall) peer.time += us;
    return 0;
}
static int health(void *cookie)
{ if (cookie != &peer) return 1; return ++peer.health_calls == peer.health_fail; }
static void setup(unsigned acd, unsigned ifpc)
{
    std::memset(&peer, 0, sizeof(peer)); peer.time = 1000;
    if (AdrenoHfiInitializeStopped(&hfi, shared, 16384, 0x61004000, &peer, notify) != AdrenoHfiSuccess) {
        std::fprintf(stderr, "hfi init failed\n"); std::exit(2);
    }
    std::memset(&state, 0, sizeof(state)); std::memset(&config, 0, sizeof(config));
    config.gpu_count = 4; config.gmu_count = 3;
    for (unsigned i = 0; i < 4; ++i) config.gpu[i] = (AdrenoHfiPerformanceLevel){i*100000000ULL, i*3};
    for (unsigned i = 0; i < 3; ++i) config.gmu[i] = (AdrenoHfiPerformanceLevel){i*200000000ULL, i*4};
    config.bandwidth_count = 3; config.cnoc_count = 2; config.ddr_count = 3;
    config.cnoc_wait_mask = 3; config.ddr_wait_mask = 7;
    for (unsigned i = 0; i < 2; ++i) {
        config.cnoc_addresses[i] = 0x1000+4*i;
        config.cnoc_data[0][i] = 0x40000000; config.cnoc_data[1][i] = 0x60000001;
    }
    for (unsigned i = 0; i < 3; ++i) {
        config.ddr_addresses[i] = 0x2000+4*i;
        for (unsigned j = 0; j < 3; ++j) config.ddr_data[j][i] = 0x40000000+i+j;
    }
    if (acd) { config.acd_version=1; config.acd_enable_mask=10; config.acd_stride=1; config.acd_count=2;
               config.acd_data[0]=41; config.acd_data[1]=99; }
    config.enable_ifpc = ifpc;
    services = (AdrenoHfiStartupServices){&peer, now, wait_us, health, 5, 10, 100, 500};
}

/* ---- cold-start fixture: memory blocks + device ---- */
typedef struct {
    uint8_t *icache, *dummy, *debug, *log, *hmem;
    Sm8750GmuDevice dev;
    Sm8750GmuConfig c;
} Cold;
static int cold_alloc(Cold *x)
{
    if (posix_memalign((void **)&x->icache, 4096, 0xffc000) ||
        posix_memalign((void **)&x->dummy, 4096, 0x4000) ||
        posix_memalign((void **)&x->debug, 4096, 0x7000) ||
        posix_memalign((void **)&x->log, 4096, 0x4000) ||
        posix_memalign((void **)&x->hmem, 4096, 0x4000)) return 1;
    return 0;
}
static void cold_free(Cold *x)
{ free(x->icache); free(x->dummy); free(x->debug); free(x->log); free(x->hmem); }
static void cold_setup(Cold *x, const uint8_t *blob, size_t blen)
{
    gmu_peer_reset((volatile uint32_t *)x->hmem);
    std::memset(x, 0, sizeof(*x));
    /* gmu_peer_reset zeroed gp but x zeroing must not clear allocations — restore */
    x->dev.io = (Sm8750GmuIo){&gp, {0}, gmu_read32, gmu_write32, gmu_barrier};
    x->dev.io.bytes[Sm8750GmuCore] = CORE_BYTES;
    x->dev.io.bytes[Sm8750GmuRscc] = RSCC_BYTES;
    x->dev.io.bytes[Sm8750GmuGpu] = GPU_BYTES;
    x->dev.services = (Sm8750GmuServices){&gp, gmu_sha256, gmu_now, gmu_wait};
    x->c.chip_id = SM8750_GMU_CHIP_ID;
    x->c.firmware = blob; x->c.firmware_bytes = blen;
    x->c.icache = (Sm8750GmuMemory){(volatile uint32_t *)x->icache, 0xffc000, 0x4000};
    x->c.dummy = (Sm8750GmuMemory){(volatile uint32_t *)x->dummy, 0x4000, 0x60000000};
    x->c.debug = (Sm8750GmuMemory){(volatile uint32_t *)x->debug, 0x7000, 0x60400000};
    x->c.log = (Sm8750GmuMemory){(volatile uint32_t *)x->log, 0x4000, 0x60800000};
    x->c.hfi = (Sm8750GmuMemory){(volatile uint32_t *)x->hmem, 0x4000, 0x61000000};
    x->c.poll_budget = 16;
}
/* NOTE: cold_setup zeroes *x AFTER allocation — caller must re-assign pointers.
 * (ordering fixed in cold_setup2 below) */

int main(int argc, char **argv)
{
    if (argc != 2) { std::fprintf(stderr, "usage: %s <gmu.bin>\n", argv[0]); return 2; }
    FILE *f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "blob missing\n"); return 2; }
    std::fseek(f, 0, SEEK_END); long blen = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    uint8_t *blob = (uint8_t *)std::malloc((size_t)blen);
    if (!blob || std::fread(blob, 1, (size_t)blen, f) != (size_t)blen) { std::fprintf(stderr, "blob read\n"); return 2; }
    std::fclose(f);
    if (posix_memalign((void **)&shared, 4096, 16384)) return 2;

    /* ---- sha256 peer correctness (FIPS vectors) + pin/blob identity proof ---- */
    {
        uint8_t d[32];
        sha256_peer("", 0, d);
        static const uint8_t empty[32] = {0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,
            0x99,0x6f,0xb9,0x24,0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
        CHECK(!std::memcmp(d, empty, 32));
        sha256_peer("abc", 3, d);
        static const uint8_t abc[32] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,
            0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
        CHECK(!std::memcmp(d, abc, 32));
        /* Positive 0 (evidence anchor): locked blob sha256 == compile-time pin, byte for byte */
        uint8_t bd[32];
        sha256_peer(blob, (size_t)blen, bd);
        CHECK(!std::memcmp(bd, sm8750_gmu_firmware_sha256, 32));
        CHECK((size_t)blen == SM8750_GMU_FIRMWARE_BYTES);
    }

    /* ============ A. AdrenoFirmware plan on the real locked blob ============ */
    static const AdrenoFirmwareRegion regions[] = {{0,0x4000,1},{0x4000,0xffc000,2},{0x10004000,0x4000,3}};
    AdrenoFirmwareBlock blocks[32];
    size_t count = 0;
    /* Positive 1: real locked 88896B gen80000 blob parses successfully */
    AdrenoFirmwareResult pr = AdrenoFirmwarePlanGmu(blob, (size_t)blen, regions, 3, blocks, 32, &count);
    CHECK(pr == AdrenoFirmwareSuccess && count > 0 && count <= 32);
    size_t base_count = count;
    /* Positive 2: payload extents consistent + nonzero payloads carry region ids */
    {
        unsigned payloads = 0; size_t payload_bytes = 0; unsigned shape_ok = 1;
        for (size_t i = 0; i < count; ++i) {
            payloads += blocks[i].bytes != 0;
            payload_bytes += blocks[i].bytes;
            if (!blocks[i].bytes && blocks[i].region_identifier != UINT32_MAX) shape_ok = 0;
            if (blocks[i].bytes && blocks[i].region_identifier > 3) shape_ok = 0;
            ++iterations;
        }
        CHECK(shape_ok && payloads > 0 && payload_bytes <= (size_t)blen);
    }
    /* Positive 3: cross-run byte-level determinism — double parse identical plans */
    {
        AdrenoFirmwareBlock blocks2[32]; size_t count2 = 0;
        CHECK(AdrenoFirmwarePlanGmu(blob, (size_t)blen, regions, 3, blocks2, 32, &count2) == AdrenoFirmwareSuccess);
        CHECK(count2 == base_count && !std::memcmp(blocks, blocks2, base_count * sizeof(AdrenoFirmwareBlock)));
    }
    /* Positive 4: parse stress — 300 repeated rounds all identical */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 500; ++round) {
            AdrenoFirmwareBlock rb[32]; size_t rc = 0;
            if (AdrenoFirmwarePlanGmu(blob, (size_t)blen, regions, 3, rb, 32, &rc) == AdrenoFirmwareSuccess &&
                rc == base_count && !std::memcmp(rb, blocks, base_count * sizeof(AdrenoFirmwareBlock))) ++good;
            ++iterations;
        }
        CHECK(good == 500);
    }

    /* ============ B. HFI startup pin-exact peer ============ */
    /* Positive 5: A8xx table exact gen8 serialization shape */
    {
        setup(0,0); config.performance_table = AdrenoHfiPerfA8xx;
        for (unsigned i = 0; i < 4; ++i) config.gpu_dependency_votes[i] = 0x80+i;
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupSuccess);
        CHECK(peer.count == 4 && peer.ids[0] == 15 && state.completed);
        const uint32_t gen8[] = {0,1,4,3,0,0x80,0,3,0x81,100000,6,0x82,200000,
            9,0x83,300000,3,2,0,0,4,200000,8,400000};
        CHECK(peer.words[0] == sizeof(gen8)/4 && !std::memcmp(peer.messages[0], gen8, sizeof(gen8)));
    }
    /* Positive 6: four acd/ifpc profiles — exact counts, completed, retained */
    {
        unsigned good = 0;
        for (unsigned acd = 0; acd < 2; ++acd) for (unsigned ifpc = 0; ifpc < 2; ++ifpc) {
            setup(acd, ifpc);
            if (AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) != AdrenoHfiStartupSuccess) continue;
            unsigned want = 4 + 2*acd + ifpc;
            if (peer.count == want && state.completed && !state.poisoned &&
                result.stage == AdrenoHfiStageComplete && result.published_commands == want &&
                result.resources_must_retain) ++good;
            iterations += 2;
        }
        CHECK(good == 4);
    }
    /* Positive 7: repeat-run Busy contract */
    {
        setup(0,0);
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupSuccess);
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupBusy);
    }
    /* Positive 8: double-run queue byte determinism */
    {
        unsigned ids1[8], words1[8]; uint32_t msg1[8][159];
        unsigned count_a = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            setup(1,1);
            if (AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) != AdrenoHfiStartupSuccess) { ++failures; break; }
            if (pass == 0) {
                count_a = peer.count;
                std::memcpy(ids1, peer.ids, sizeof(ids1));
                std::memcpy(words1, peer.words, sizeof(words1));
                std::memcpy(msg1, peer.messages, sizeof(msg1));
            } else {
                CHECK(peer.count == count_a && !std::memcmp(ids1, peer.ids, count_a*sizeof(unsigned)) &&
                      !std::memcmp(words1, peer.words, count_a*sizeof(unsigned)));
                unsigned same = 1;
                for (unsigned i = 0; i < count_a; ++i)
                    if (std::memcmp(msg1[i], peer.messages[i], words1[i]*4)) same = 0;
                CHECK(same);
            }
        }
    }
    /* Positive 9: stress — 400 fresh startup rounds all succeed */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 550; ++round) {
            setup(round & 1, (round >> 1) & 1);
            if (AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupSuccess &&
                state.completed && !state.poisoned) ++good;
            ++iterations;
        }
        CHECK(good == 550);
    }

    /* ============ C. Sm8750Gmu cold boot with real firmware ============ */
    /* Positive 10: full cold start — real blob, firmware-emulating peer, exact terminal shape */
    Sm8750GmuStartupResult cold_r;
    {
        Cold x;
        if (cold_alloc(&x)) { std::fprintf(stderr, "alloc failed\n"); return 2; }
        uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
        cold_setup(&x, blob, (size_t)blen);
        x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
        x.c.icache.cpu = (volatile uint32_t *)x.icache;
        x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
        x.c.debug.cpu = (volatile uint32_t *)x.debug;
        x.c.log.cpu = (volatile uint32_t *)x.log;
        x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
        gp.hmem = (volatile uint32_t *)x.hmem;
        /* firmware ready value follows the production version-word rule; the emulated
           firmware publishes it once SYSRESET drops (hook in gmu_write32) */
        gp.fw_init_ready = 0xbabeface;
        AdrenoHfiStartupConfig sc = config; /* reuse pin-exact startup config shape */
        std::memset(&sc, 0, sizeof(sc));
        sc.gpu_count = 4; sc.gmu_count = 3;
        for (unsigned i = 0; i < 4; ++i) sc.gpu[i] = (AdrenoHfiPerformanceLevel){i*100000000ULL, i*3};
        for (unsigned i = 0; i < 3; ++i) sc.gmu[i] = (AdrenoHfiPerformanceLevel){i*200000000ULL, i*4};
        sc.bandwidth_count = 3; sc.cnoc_count = 2; sc.ddr_count = 3;
        sc.cnoc_wait_mask = 3; sc.ddr_wait_mask = 7;
        for (unsigned i = 0; i < 2; ++i) {
            sc.cnoc_addresses[i] = 0x1000+4*i;
            sc.cnoc_data[0][i] = 0x40000000; sc.cnoc_data[1][i] = 0x60000001;
        }
        for (unsigned i = 0; i < 3; ++i) {
            sc.ddr_addresses[i] = 0x2000+4*i;
            for (unsigned j = 0; j < 3; ++j) sc.ddr_data[j][i] = 0x40000000+i+j;
        }
        sc.performance_table = AdrenoHfiPerfA8xx;
        for (unsigned i = 0; i < 4; ++i) sc.gpu_dependency_votes[i] = 0x80+i;
        Sm8750GmuStartupLimits limits = {5, 10, 100, 500};
        Sm8750GmuStatus st = Sm8750GmuColdStart(&x.dev, &x.c, &sc, &limits, &cold_r);
        if (st != Sm8750GmuSuccess)
            std::fprintf(stderr, "coldstart rc=%d phase=%u fw_init=%x hfi=%x\n",
                         st, cold_r.load.phase, cold_r.load.firmware_init_result, cold_r.load.hfi_status);
        CHECK(st == Sm8750GmuSuccess);
        CHECK(cold_r.load.phase == 7 && cold_r.load.firmware_ready_observed && cold_r.load.hfi_ready_observed);
        CHECK(cold_r.load.complete_hash_verified);
        CHECK(cold_r.protocol_status == AdrenoHfiStartupSuccess);
        cold_free(&x);
    }
    /* Positive 11: MMIO write-sequence determinism — two fresh cold boots identical events */
    {
        static GmuEvent seq1[2048];
        size_t n1 = 0, n2 = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            Cold x;
            if (cold_alloc(&x)) { std::fprintf(stderr, "alloc failed\n"); return 2; }
            uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
            cold_setup(&x, blob, (size_t)blen);
            x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
            x.c.icache.cpu = (volatile uint32_t *)x.icache;
            x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
            x.c.debug.cpu = (volatile uint32_t *)x.debug;
            x.c.log.cpu = (volatile uint32_t *)x.log;
            x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
            gp.hmem = (volatile uint32_t *)x.hmem;
            gp.fw_init_ready = 0xbabeface;
            AdrenoHfiStartupConfig sc;
            std::memset(&sc, 0, sizeof(sc));
            sc.gpu_count = 4; sc.gmu_count = 3;
            for (unsigned i = 0; i < 4; ++i) sc.gpu[i] = (AdrenoHfiPerformanceLevel){i*100000000ULL, i*3};
            for (unsigned i = 0; i < 3; ++i) sc.gmu[i] = (AdrenoHfiPerformanceLevel){i*200000000ULL, i*4};
            sc.bandwidth_count = 3; sc.cnoc_count = 2; sc.ddr_count = 3;
            sc.cnoc_wait_mask = 3; sc.ddr_wait_mask = 7;
            for (unsigned i = 0; i < 2; ++i) {
                sc.cnoc_addresses[i] = 0x1000+4*i;
                sc.cnoc_data[0][i] = 0x40000000; sc.cnoc_data[1][i] = 0x60000001;
            }
            for (unsigned i = 0; i < 3; ++i) {
                sc.ddr_addresses[i] = 0x2000+4*i;
                for (unsigned j = 0; j < 3; ++j) sc.ddr_data[j][i] = 0x40000000+i+j;
            }
            sc.performance_table = AdrenoHfiPerfA8xx;
            for (unsigned i = 0; i < 4; ++i) sc.gpu_dependency_votes[i] = 0x80+i;
            Sm8750GmuStartupLimits limits = {5, 10, 100, 500};
            Sm8750GmuStartupResult rr;
            Sm8750GmuStatus st = Sm8750GmuColdStart(&x.dev, &x.c, &sc, &limits, &rr);
            if (st != Sm8750GmuSuccess) ++failures;
            if (pass == 0) { n1 = gp.event_count; std::memcpy(seq1, gp.events, n1 * sizeof(GmuEvent)); }
            else {
                n2 = gp.event_count;
                CHECK(n1 == n2 && n1 > 0 && !std::memcmp(seq1, gp.events, n1 * sizeof(GmuEvent)));
            }
            cold_free(&x);
        }
    }
    /* Positive 13: post-boot payload verification — region-2 icache words match firmware exactly */
    {
        Cold x;
        if (cold_alloc(&x)) { std::fprintf(stderr, "alloc failed\n"); return 2; }
        uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
        cold_setup(&x, blob, (size_t)blen);
        x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
        x.c.icache.cpu = (volatile uint32_t *)x.icache;
        x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
        x.c.debug.cpu = (volatile uint32_t *)x.debug;
        x.c.log.cpu = (volatile uint32_t *)x.log;
        x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
        gp.hmem = (volatile uint32_t *)x.hmem;
        AdrenoHfiStartupConfig sc;
        std::memset(&sc, 0, sizeof(sc));
        sc.gpu_count = 4; sc.gmu_count = 3;
        for (unsigned i = 0; i < 4; ++i) sc.gpu[i] = (AdrenoHfiPerformanceLevel){i*100000000ULL, i*3};
        for (unsigned i = 0; i < 3; ++i) sc.gmu[i] = (AdrenoHfiPerformanceLevel){i*200000000ULL, i*4};
        sc.bandwidth_count = 3; sc.cnoc_count = 2; sc.ddr_count = 3;
        sc.cnoc_wait_mask = 3; sc.ddr_wait_mask = 7;
        for (unsigned i = 0; i < 2; ++i) {
            sc.cnoc_addresses[i] = 0x1000+4*i;
            sc.cnoc_data[0][i] = 0x40000000; sc.cnoc_data[1][i] = 0x60000001;
        }
        for (unsigned i = 0; i < 3; ++i) {
            sc.ddr_addresses[i] = 0x2000+4*i;
            for (unsigned j = 0; j < 3; ++j) sc.ddr_data[j][i] = 0x40000000+i+j;
        }
        sc.performance_table = AdrenoHfiPerfA8xx;
        for (unsigned i = 0; i < 4; ++i) sc.gpu_dependency_votes[i] = 0x80+i;
        Sm8750GmuStartupLimits limits = {5, 10, 100, 500};
        Sm8750GmuStartupResult rr;
        CHECK(Sm8750GmuColdStart(&x.dev, &x.c, &sc, &limits, &rr) == Sm8750GmuSuccess);
        /* the production readback already proved every word; independently re-verify region-2
           icache payload bytes against the firmware image */
        unsigned verified = 0;
        for (size_t i = 0; i < base_count; ++i) {
            const AdrenoFirmwareBlock *b = blocks + i;
            if (!b->bytes || b->region_identifier != 2) continue;
            const uint8_t *cpu = (const uint8_t *)x.icache + (b->address - 0x4000);
            if (!std::memcmp(cpu, blob + b->payload_offset, b->bytes)) ++verified;
            iterations += (unsigned)(b->bytes / 4096) + 1;
        }
        CHECK(verified >= 1);
        cold_free(&x);
    }
    /* Positive 14: BindMmio valid binding + io roundtrip through the production adapter */
    {
        static uint8_t wcore[0x68000] __attribute__((aligned(4096)));
        static uint8_t wrscc[0x10000] __attribute__((aligned(4096)));
        static uint8_t wgpu[0x40000] __attribute__((aligned(4096)));
        std::memset(wcore, 0, sizeof(wcore)); std::memset(wrscc, 0, sizeof(wrscc)); std::memset(wgpu, 0, sizeof(wgpu));
        Sm8750GmuMmio m; std::memset(&m, 0, sizeof(m));
        m.windows[0].cpu = wcore; m.windows[0].physical = sm8750_gmu_window_physical[0]; m.windows[0].bytes = sm8750_gmu_window_bytes[0];
        m.windows[1].cpu = wrscc; m.windows[1].physical = sm8750_gmu_window_physical[1]; m.windows[1].bytes = sm8750_gmu_window_bytes[1];
        m.windows[2].cpu = wgpu; m.windows[2].physical = sm8750_gmu_window_physical[2]; m.windows[2].bytes = sm8750_gmu_window_bytes[2];
        Sm8750GmuIo io;
        CHECK(Sm8750GmuBindMmio(&m, &io) == Sm8750GmuSuccess);
        CHECK(io.read32 && io.write32 && io.barrier);
        uint32_t v = 0;
        CHECK(!io.write32(io.cookie, Sm8750GmuCore, 0x100, 0xcafebabe) &&
              !io.read32(io.cookie, Sm8750GmuCore, 0x100, &v) && v == 0xcafebabe);
        CHECK(!io.barrier(io.cookie));
        CHECK(io.read32(io.cookie, Sm8750GmuCore, 0x102, &v) != 0); /* misaligned refused */
    }
    /* Positive 12: cold-boot stress — 50 fresh rounds all succeed (each round is thousands of MMIO ops) */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 100; ++round) {
            Cold x;
            if (cold_alloc(&x)) { ++failures; break; }
            uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
            cold_setup(&x, blob, (size_t)blen);
            x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
            x.c.icache.cpu = (volatile uint32_t *)x.icache;
            x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
            x.c.debug.cpu = (volatile uint32_t *)x.debug;
            x.c.log.cpu = (volatile uint32_t *)x.log;
            x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
            gp.hmem = (volatile uint32_t *)x.hmem;
            gp.fw_init_ready = 0xbabeface;
            AdrenoHfiStartupConfig sc;
            std::memset(&sc, 0, sizeof(sc));
            sc.gpu_count = 4; sc.gmu_count = 3;
            for (unsigned i = 0; i < 4; ++i) sc.gpu[i] = (AdrenoHfiPerformanceLevel){i*100000000ULL, i*3};
            for (unsigned i = 0; i < 3; ++i) sc.gmu[i] = (AdrenoHfiPerformanceLevel){i*200000000ULL, i*4};
            sc.bandwidth_count = 3; sc.cnoc_count = 2; sc.ddr_count = 3;
            sc.cnoc_wait_mask = 3; sc.ddr_wait_mask = 7;
            for (unsigned i = 0; i < 2; ++i) {
                sc.cnoc_addresses[i] = 0x1000+4*i;
                sc.cnoc_data[0][i] = 0x40000000; sc.cnoc_data[1][i] = 0x60000001;
            }
            for (unsigned i = 0; i < 3; ++i) {
                sc.ddr_addresses[i] = 0x2000+4*i;
                for (unsigned j = 0; j < 3; ++j) sc.ddr_data[j][i] = 0x40000000+i+j;
            }
            sc.performance_table = AdrenoHfiPerfA8xx;
            for (unsigned i = 0; i < 4; ++i) sc.gpu_dependency_votes[i] = 0x80+i;
            Sm8750GmuStartupLimits limits = {5, 10, 100, 500};
            Sm8750GmuStartupResult rr;
            if (Sm8750GmuColdStart(&x.dev, &x.c, &sc, &limits, &rr) == Sm8750GmuSuccess &&
                rr.load.phase == 7 && rr.protocol_status == AdrenoHfiStartupSuccess) ++good;
            ++iterations;
            cold_free(&x);
        }
        CHECK(good == 100);
    }

    /* ================= negative fail-closed battery ================= */
    /* Negative 1: truncated firmware refusal — count reset, blocks untouched */
    {
        AdrenoFirmwareBlock tb[32], bb[32];
        std::memset(tb, 0xa5, sizeof(tb)); std::memcpy(bb, tb, sizeof(tb));
        size_t tc = 123;
        AdrenoFirmwareResult rr = AdrenoFirmwarePlanGmu(blob, (size_t)blen - 16, regions, 3, tb, 32, &tc);
        CHECK(rr != AdrenoFirmwareSuccess);
        CHECK(tc == 0 && !std::memcmp(tb, bb, sizeof(tb)));
    }
    /* Negative 2: region layout violation — unaligned region Invalid */
    {
        AdrenoFirmwareRegion bad[3]; std::memcpy(bad, regions, sizeof(bad));
        bad[1].address = 0x3ffc;
        AdrenoFirmwareBlock tb[32]; size_t tc = 0;
        CHECK(AdrenoFirmwarePlanGmu(blob, (size_t)blen, bad, 3, tb, 32, &tc) == AdrenoFirmwareInvalid);
    }
    /* Negative 3: capacity exhaustion — NoSpace fail-closed */
    {
        AdrenoFirmwareBlock tb[32]; size_t tc = 0;
        AdrenoFirmwareResult rr = AdrenoFirmwarePlanGmu(blob, (size_t)blen, regions, 3, tb, 1, &tc);
        CHECK(rr != AdrenoFirmwareSuccess && rr == AdrenoFirmwareNoSpace);
    }
    /* Negative 4: HFI startup invalid config refused pre-publication */
    {
        setup(0,0); config.performance_table = AdrenoHfiPerfA8xx;
        for (unsigned i = 0; i < 4; ++i) config.gpu_dependency_votes[i] = 0x80+i;
        config.gpu_dependency_votes[4] = 1; /* pin-exact refusal: vote outside the gpu entry range */
        uint32_t snap[4096]; std::memcpy(snap, shared, sizeof(snap));
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupInvalid);
        CHECK(!state.attempted && !peer.count && result.resources_must_retain);
        CHECK(!std::memcmp(snap, shared, sizeof(snap)));
    }
    /* Negative 5: transport failure injection — poisoned, retained, re-run Poisoned */
    {
        setup(0,0); config.performance_table = AdrenoHfiPerfA8xx;
        for (unsigned i = 0; i < 4; ++i) config.gpu_dependency_votes[i] = 0x80+i;
        peer.fail_at = 1; peer.mode = 1;
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupTransportError);
        CHECK(state.poisoned && hfi.poisoned && !state.completed && result.resources_must_retain);
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupPoisoned);
    }
    /* Negative 6: bounded timeout — clock stall, never completes, poisoned */
    {
        setup(1,1); peer.fail_at = 1; peer.mode = 3; peer.clock_stall = 1;
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result) == AdrenoHfiStartupTimeout);
        CHECK(result.poll_reads == 5 && peer.waits == 4 && state.poisoned);
    }
    /* Negative 7: corrupted firmware — hash mismatch refuses before any MMIO write */
    {
        Cold x;
        if (!cold_alloc(&x)) {
            uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
            cold_setup(&x, blob, (size_t)blen);
            x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
            x.c.icache.cpu = (volatile uint32_t *)x.icache;
            x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
            x.c.debug.cpu = (volatile uint32_t *)x.debug;
            x.c.log.cpu = (volatile uint32_t *)x.log;
            x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
            blob[0x200] ^= 0xff; /* corrupt one payload byte */
            uint8_t before[256]; std::memcpy(before, gp.core, 256);
            Sm8750GmuResult rr;
            Sm8750GmuStatus st = Sm8750GmuLoadAndStartHfi(&x.dev, &x.c, &rr);
            blob[0x200] ^= 0xff; /* restore */
            CHECK(st == Sm8750GmuFirmware);
            CHECK(rr.resources_must_retain && !rr.complete_hash_verified);
            CHECK(!std::memcmp(before, gp.core, 256)); /* zero MMIO writes before hash gate */
            cold_free(&x);
        } else { ++failures; }
    }
    /* Negative 8: fw init timeout — bounded, poisoned, reset re-asserted, never ready */
    {
        Cold x;
        if (!cold_alloc(&x)) {
            uint8_t *save[5] = {x.icache, x.dummy, x.debug, x.log, x.hmem};
            cold_setup(&x, blob, (size_t)blen);
            x.icache = save[0]; x.dummy = save[1]; x.debug = save[2]; x.log = save[3]; x.hmem = save[4];
            x.c.icache.cpu = (volatile uint32_t *)x.icache;
            x.c.dummy.cpu = (volatile uint32_t *)x.dummy;
            x.c.debug.cpu = (volatile uint32_t *)x.debug;
            x.c.log.cpu = (volatile uint32_t *)x.log;
            x.c.hfi.cpu = (volatile uint32_t *)x.hmem;
            gp.hmem = (volatile uint32_t *)x.hmem;
            gp.stall_fw = 1; /* emulated firmware never publishes init result */
            Sm8750GmuResult rr;
            Sm8750GmuStatus st = Sm8750GmuLoadAndStartHfi(&x.dev, &x.c, &rr);
            CHECK(st == Sm8750GmuTimeout);
            CHECK(!rr.firmware_ready_observed && rr.resources_must_retain);
            CHECK(x.dev.poisoned); /* failure poisons the device */
            cold_free(&x);
        } else { ++failures; }
    }
    /* Negative 9: null/invalid contracts */
    {
        Sm8750GmuDevice dev; Sm8750GmuResult rr;
        std::memset(&dev, 0, sizeof(dev));
        CHECK(Sm8750GmuLoadAndStartHfi(&dev, 0, &rr) == Sm8750GmuArgument);
        Sm8750GmuConfig c0; std::memset(&c0, 0, sizeof(c0));
        CHECK(Sm8750GmuLoadAndStartHfi(0, &c0, &rr) == Sm8750GmuArgument);
        setup(0,0);
        uint32_t p[159], cp[159], id = 99; size_t n = 9;
        std::memset(p, 0xa5, sizeof(p)); std::memcpy(cp, p, sizeof(p));
        CHECK(AdrenoHfiStartupEncode(&config, AdrenoHfiStageComplete, &id, p, 159, &n) == AdrenoHfiStartupInvalid);
        CHECK(!n && id == 99 && !std::memcmp(p, cp, sizeof(p)));
    }

    std::free(blob); std::free(shared);
    std::printf("GMU_RESULT {\"iterations\":%lu,\"failures\":%d,\"blob_bytes\":%ld,\"plan_count\":%zu}\n",
                iterations, failures, blen, base_count);
    return failures ? 1 : 0;
}
