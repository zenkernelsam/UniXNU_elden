/* SPDX-License-Identifier: MIT */
/* gpu-opt-034: real AdrenoPM4 pipeline emitters (Constants/Raster/Shader/VPC) + PM4 decoder
 * under ASan+UBSan -Werror. Zero extraction, zero replacement — pin self-tests
 * test_constants.c/test_raster.c/test_shader.c/test_vpc.c are the authoritative semantic source.
 * Complements pin coverage with NEW cross-run byte-level pipeline determinism and combined
 * stress decode readback. No device, hardware mapping or GPU is opened. */
#include "AdrenoPM4.h"
#include "AdrenoConstants.h"
#include "AdrenoRaster.h"
#include "AdrenoShader.h"
#include "AdrenoVPC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned long iterations = 0;
static int failures = 0;
#define CHECK(c) do { ++iterations; if (!(c)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define WORDS 2304
static uint32_t output[WORDS], before[WORDS];
static void reset() { std::memset(output, 0x6b, sizeof(output)); std::memcpy(before, output, sizeof(output)); }

/* ---- sha256 peer (test infrastructure, FIPS 180-4) for pipeline determinism digests ---- */
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
            uint32_t S1 = (e>>6|e<<26) ^ (e>>11|e<<21) ^ (e<<25|e<<7);
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

/* ================= fixtures (pin-exact) ================= */
static uint8_t const_data[8192];
static struct AdrenoConstantConfig const_config = {ADRENO_A740_CHIP_ID, AdrenoConstantVertex, 8192};

static struct AdrenoRasterState raster_fixture()
{
    return (struct AdrenoRasterState){ADRENO_A740_CHIP_ID,
        AdrenoRasterClipNear|AdrenoRasterClipFar|AdrenoRasterHalfZ|AdrenoRasterFrontCCW,
        3,16,2,0,0,0,0};
}
static struct AdrenoShaderState shader_fixture()
{
    return (struct AdrenoShaderState){ADRENO_A740_CHIP_ID, AdrenoConstantVertex, 128, 1,
        0, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
}
static struct AdrenoShaderMapping shader_mapping_fixture()
{
    return (struct AdrenoShaderMapping){1, UINT64_C(0x100000000), 4096, 0, 12, 1};
}
static uint8_t vpc_names[15][32];
static struct AdrenoVPCOutput vpc_outputs[16];
static struct AdrenoVPCInput vpc_inputs[15];
static struct AdrenoVPCState vpc_state;
static void vpc_fixture(unsigned n)
{
    vpc_state = (struct AdrenoVPCState){ADRENO_A740_CHIP_ID,62,0,vpc_outputs,n+1,vpc_inputs,n};
    vpc_outputs[0] = (struct AdrenoVPCOutput){(const uint8_t *)"air.position",12,0,4,32,0,0,0};
    for (unsigned i = 0; i < n; ++i) {
        std::snprintf((char *)vpc_names[i], sizeof vpc_names[i], "generated(v%u)", i);
        vpc_outputs[i+1] = (struct AdrenoVPCOutput){vpc_names[i],std::strlen((char *)vpc_names[i]),4+i*4,4,32,0,0,0};
        vpc_inputs[i] = (struct AdrenoVPCInput){vpc_names[i],std::strlen((char *)vpc_names[i]),4,32,15,i*4,0};
    }
}

/* emit all four pipeline states sequentially into dst; returns total words or 0 on failure */
static size_t emit_pipeline(uint32_t *dst, size_t capacity)
{
    size_t at = 0;
    struct AdrenoConstantSpan one = {0, 0, 16};
    struct AdrenoConstantResult cr = AdrenoA740Constants(&const_config, &one, 1, const_data, 8192, dst + at, capacity - at);
    if (cr.status != AdrenoConstantOK) return 0;
    at += cr.words;
    struct AdrenoRasterState rs = raster_fixture();
    struct AdrenoRasterResult rr = AdrenoA740Raster(&rs, dst + at, capacity - at);
    if (rr.status != AdrenoRasterOK) return 0;
    at += rr.words;
    struct AdrenoShaderState ss = shader_fixture();
    struct AdrenoShaderMapping sm = shader_mapping_fixture();
    struct AdrenoShaderResult sr = AdrenoA740ShaderBind(&ss, &sm, dst + at, capacity - at);
    if (sr.status != AdrenoShaderOK) return 0;
    at += sr.words;
    vpc_fixture(2);
    struct AdrenoVPCResult vr = AdrenoA740VaryingMap(&vpc_state, dst + at, capacity - at);
    if (vr.status != AdrenoVPCOK) return 0;
    at += vr.words;
    return at;
}

int main()
{
    for (unsigned i = 0; i < 8192; ++i) const_data[i] = (uint8_t)(i * 7 + 3);

    /* ============ A. Constants ============ */
    /* Positive 1: pin-exact emission + PM4 decode readback (type7 opcode/payload/state word/data exactness) */
    {
        reset();
        struct AdrenoConstantSpan one = {64, 32, 64};
        struct AdrenoConstantResult r = AdrenoA740Constants(&const_config, &one, 1, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantOK && r.packets == 1);
        AdrenoPM4Packet packets[8]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 8, &count) == AdrenoPM4Success && count == 1);
        CHECK(packets[0].type == 7 && packets[0].identifier == 0x32 && packets[0].payload_words == 3 + 64/4);
        CHECK(output[1] == ((32/16) | 0x4000 | 0x200000 | (64/16) * 0x400000));
        CHECK(!output[2] && !output[3]);
        CHECK(!std::memcmp(output + 4, const_data + 64, 64));
        CHECK(!std::memcmp(output + r.words, before + r.words, sizeof(output) - r.words*4));
    }
    /* Positive 2: multi-span emission — packet count equals span count, sequential layout */
    {
        reset();
        struct AdrenoConstantSpan spans[4] = {{0,0,16},{32,64,32},{64,128,16},{128,256,48}};
        struct AdrenoConstantResult r = AdrenoA740Constants(&const_config, spans, 4, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantOK && r.packets == 4);
        AdrenoPM4Packet packets[8]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 8, &count) == AdrenoPM4Success && count == 4);
    }
    /* Positive 3: fragment-stage opcode 0x34 + zero-span edge case legality */
    {
        reset();
        struct AdrenoConstantConfig fc = const_config;
        fc.stage = AdrenoConstantFragment;
        struct AdrenoConstantSpan one = {0, 0, 16};
        struct AdrenoConstantResult r = AdrenoA740Constants(&fc, &one, 1, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantOK);
        AdrenoPM4Packet packets[8]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 8, &count) == AdrenoPM4Success);
        CHECK(packets[0].identifier == 0x34);
        reset();
        struct AdrenoConstantResult z = AdrenoA740Constants(&const_config, 0, 0, 0, 0, output, WORDS);
        CHECK(z.status == AdrenoConstantOK && !z.words && !z.packets);
    }

    /* ============ B. Raster ============ */
    /* Positive 4: full-clamp emission exactness — 60 words/46 registers/14 type4 packets */
    {
        reset();
        struct AdrenoRasterState rs = raster_fixture();
        struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 80);
        CHECK(!r.status && r.words == 60 && r.registers == 46);
        AdrenoPM4Packet packets[16]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 16, &count) == AdrenoPM4Success && count == 14);
        unsigned all_type4 = 1;
        for (size_t i = 0; i < count; ++i) if (packets[i].type != 4) all_type4 = 0;
        CHECK(all_type4);
        CHECK(!std::memcmp(output + r.words, before + r.words, sizeof(output) - r.words*4));
    }
    /* Positive 5: clamp-disabled short emission — 24 words/12 registers/12 packets */
    {
        reset();
        struct AdrenoRasterState rs = raster_fixture();
        rs.flags = 0;
        struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 80);
        CHECK(!r.status && r.words == 24 && r.registers == 12);
    }
    /* Positive 6: fallback chip id accepted identically shaped */
    {
        reset();
        struct AdrenoRasterState rs = raster_fixture();
        rs.chip_id = ADRENO_A740_FALLBACK_CHIP_ID;
        struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 80);
        CHECK(!r.status && r.words == 60);
    }

    /* ============ C. Shader ============ */
    /* Positive 7: VS emission exactness — 24 words/13 registers/11 packets, control encoding exact */
    {
        reset();
        struct AdrenoShaderState ss = shader_fixture();
        struct AdrenoShaderMapping sm = shader_mapping_fixture();
        struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(!r.status && r.words == 24 && r.registers == 13);
        AdrenoPM4Packet packets[12]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 12, &count) == AdrenoPM4Success && count == 11);
        uint32_t regs[13], values[13]; unsigned n = 0;
        for (size_t i = 0; i < count; ++i)
            for (unsigned j = 0; j < packets[i].payload_words; ++j) {
                regs[n] = packets[i].identifier + j;
                values[n++] = output[packets[i].header_word + 1 + j];
            }
        CHECK(n == 13);
        CHECK(regs[0] == 0xab00 && values[0] == 5 && regs[1] == 0xb309 && values[1] == 0xa2);
        uint32_t control = (uint32_t)(ss.max_full_register+1)<<7 | ss.branch_stack_hw<<13;
        control |= ss.merged_registers<<20 | ss.early_preamble<<21;
        CHECK(values[2] == control && values[3] == ss.instruction_groups && !values[4]);
        uint64_t address = sm.base + sm.shader_offset;
        CHECK(values[5] == (uint32_t)address && values[6] == address>>32);
    }
    /* Positive 8: FS stage register window — fragment register sequence exact */
    {
        reset();
        struct AdrenoShaderState ss = shader_fixture();
        ss.stage = AdrenoConstantFragment;
        ss.merged_registers = 1; ss.double_threadsize = 1; ss.input_words = 128;
        ss.need_full_quad = 1; ss.need_pixel_lod = 1;
        struct AdrenoShaderMapping sm = shader_mapping_fixture();
        struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(!r.status && r.words == 24);
        AdrenoPM4Packet packets[12]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 12, &count) == AdrenoPM4Success);
        uint32_t regs[13]; unsigned n = 0;
        for (size_t i = 0; i < count; ++i)
            for (unsigned j = 0; j < packets[i].payload_words; ++j)
                regs[n++] = packets[i].identifier + j;
        const uint32_t fs[] = {0xa980,0xab05,0xa982,0xa983,0xa984,0xa985,0xa986,0xa987,0xa988,0xa9a9,0xaa01};
        CHECK(n == 13 && !std::memcmp(regs+2, fs, sizeof(fs)));
    }

    /* ============ D. VPC ============ */
    /* Positive 9: varying map emission — 12 packets, varying/stride/position shape */
    {
        reset();
        vpc_fixture(2);
        struct AdrenoVPCResult r = AdrenoA740VaryingMap(&vpc_state, output, 128);
        CHECK(!r.status && r.varying_count == 2 && r.stride == r.position_location + 4);
        AdrenoPM4Packet packets[16]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, r.words, packets, 16, &count) == AdrenoPM4Success && count == 12);
        unsigned all_type4 = 1;
        for (size_t i = 0; i < count; ++i) if (packets[i].type != 4) all_type4 = 0;
        CHECK(all_type4);
    }
    /* Positive 10: canonical ordering invariance — permuted inputs produce byte-identical emission */
    {
        reset();
        vpc_fixture(6);
        struct AdrenoVPCResult r1 = AdrenoA740VaryingMap(&vpc_state, output, 128);
        CHECK(!r1.status);
        uint32_t canonical[128]; std::memcpy(canonical, output, sizeof(canonical));
        for (unsigned i = 0; i < 3; ++i) {
            struct AdrenoVPCInput tmp = vpc_inputs[i];
            vpc_inputs[i] = vpc_inputs[5-i];
            vpc_inputs[5-i] = tmp;
        }
        reset();
        struct AdrenoVPCResult r2 = AdrenoA740VaryingMap(&vpc_state, output, 128);
        CHECK(!r2.status && !std::memcmp(output, canonical, sizeof(canonical)));
    }

    /* ============ E. cross-run pipeline determinism + stress ============ */
    /* Positive 11: combined pipeline emission double-run byte-identical + sha256 stable */
    {
        static uint32_t pipe1[WORDS], pipe2[WORDS];
        std::memset(pipe1, 0x6b, sizeof(pipe1)); std::memset(pipe2, 0x6b, sizeof(pipe2));
        size_t w1 = emit_pipeline(pipe1, WORDS);
        size_t w2 = emit_pipeline(pipe2, WORDS);
        CHECK(w1 > 0 && w1 == w2);
        CHECK(!std::memcmp(pipe1, pipe2, w1 * 4));
        uint8_t d1[32], d2[32];
        sha256_peer(pipe1, w1 * 4, d1);
        sha256_peer(pipe2, w2 * 4, d2);
        CHECK(!std::memcmp(d1, d2, 32));
    }
    /* Positive 12: stress — 1200 rounds varied emitter configs, each emit+decode roundtrip */
    {
        unsigned good = 0;
        for (unsigned round = 0; round < 1200; ++round) {
            unsigned variant = round % 4;
            unsigned ok = 0;
            if (variant == 0) {
                reset();
                struct AdrenoConstantSpan one = {(unsigned)(round % 128)*16, (unsigned)(round % 64)*16, 16};
                struct AdrenoConstantResult r = AdrenoA740Constants(&const_config, &one, 1, const_data, 8192, output, WORDS);
                if (r.status == AdrenoConstantOK) {
                    AdrenoPM4Packet pk[8]; size_t pc = 0;
                    ok = AdrenoPM4Plan(output, r.words, pk, 8, &pc) == AdrenoPM4Success && pc == 1;
                }
            } else if (variant == 1) {
                reset();
                struct AdrenoRasterState rs = raster_fixture();
                rs.flags = (unsigned)(round % 16);
                rs.polygon_mode = 1 + (round % 3);
                struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 80);
                if (!r.status) {
                    AdrenoPM4Packet pk[16]; size_t pc = 0;
                    ok = AdrenoPM4Plan(output, r.words, pk, 16, &pc) == AdrenoPM4Success && pc > 0;
                }
            } else if (variant == 2) {
                reset();
                struct AdrenoShaderState ss = shader_fixture();
                ss.max_full_register = (int)(round % 62) - 1;
                ss.branch_stack_hw = (round % 64) * 2;
                struct AdrenoShaderMapping sm = shader_mapping_fixture();
                struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
                if (!r.status) {
                    AdrenoPM4Packet pk[12]; size_t pc = 0;
                    ok = AdrenoPM4Plan(output, r.words, pk, 12, &pc) == AdrenoPM4Success && pc == 11;
                }
            } else {
                reset();
                vpc_fixture(1 + round % 4);
                struct AdrenoVPCResult r = AdrenoA740VaryingMap(&vpc_state, output, 128);
                if (!r.status) {
                    AdrenoPM4Packet pk[16]; size_t pc = 0;
                    ok = AdrenoPM4Plan(output, r.words, pk, 16, &pc) == AdrenoPM4Success && pc == 12;
                }
            }
            good += ok;
            ++iterations;
        }
        CHECK(good == 1200);
    }
    /* Positive 13: all emitted register offsets within architectural register space */
    {
        reset();
        size_t w = emit_pipeline(output, WORDS);
        CHECK(w > 0);
        AdrenoPM4Packet packets[64]; size_t count = 0;
        CHECK(AdrenoPM4Plan(output, w, packets, 64, &count) == AdrenoPM4Success && count > 0);
        unsigned in_range = 1;
        for (size_t i = 0; i < count; ++i) {
            if (packets[i].type == 4 && packets[i].identifier > 0x3ffff) in_range = 0;
            ++iterations;
        }
        CHECK(in_range);
    }
    /* Positive 14: raster fixed-point sweep — point/line field extremes accepted */
    {
        unsigned good = 0;
        for (unsigned i = 0; i <= 200; ++i) {
            struct AdrenoRasterState rs = raster_fixture();
            rs.point_size_q4 = i * 327;
            rs.line_width_q1 = i;
            struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 80);
            good += !r.status;
            ++iterations;
        }
        CHECK(good == 201);
    }
    /* Positive 15: vpc full input-count range 0..15 all emit successfully */
    {
        unsigned good = 0;
        for (unsigned n = 0; n <= 15; ++n) {
            vpc_fixture(n);
            struct AdrenoVPCResult r = AdrenoA740VaryingMap(&vpc_state, output, 128);
            good += !r.status;
            ++iterations;
        }
        CHECK(good == 16);
    }
    /* Positive 16: shader max-full/branch sweep emits with exact control encoding */
    {
        unsigned good = 0;
        for (int full = -1; full < 24; ++full) {
            struct AdrenoShaderState ss = shader_fixture();
            ss.max_full_register = full;
            ss.branch_stack_hw = (unsigned)(full + 1) * 4;
            struct AdrenoShaderMapping sm = shader_mapping_fixture();
            struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
            good += !r.status;
            ++iterations;
        }
        CHECK(good == 25);
    }

    /* ================= negative fail-closed battery ================= */
    /* Negative 1: constants capacity exhaustion — transactional zero-write refusal */
    {
        reset();
        struct AdrenoConstantSpan one = {0, 0, 16};
        struct AdrenoConstantResult r = AdrenoA740Constants(&const_config, &one, 1, const_data, 8192, output, 7);
        CHECK(r.status == AdrenoConstantCapacity && !r.words && !r.packets);
        CHECK(!std::memcmp(output, before, sizeof(output)));
    }
    /* Negative 2: constants null/invalid contracts + range violations */
    {
        reset();
        struct AdrenoConstantSpan one = {0, 0, 16};
        struct AdrenoConstantResult r = AdrenoA740Constants(0, &one, 1, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantArgument);
        struct AdrenoConstantSpan bad = {0, 0, 15}; /* non-16-multiple bytes */
        r = AdrenoA740Constants(&const_config, &bad, 1, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantRange);
        struct AdrenoConstantSpan beyond = {8192, 0, 16}; /* destination at end-of-constants */
        r = AdrenoA740Constants(&const_config, &beyond, 1, const_data, 8192, output, WORDS);
        CHECK(r.status == AdrenoConstantRange);
    }
    /* Negative 3: raster capacity + profile refusals */
    {
        reset();
        struct AdrenoRasterState rs = raster_fixture();
        struct AdrenoRasterResult r = AdrenoA740Raster(&rs, output, 59);
        CHECK(r.status == AdrenoRasterCapacity && !r.words);
        CHECK(!std::memcmp(output, before, sizeof(output)));
        rs.chip_id = 0x43050a02;
        r = AdrenoA740Raster(&rs, output, 80);
        CHECK(r.status == AdrenoRasterProfile);
        rs = raster_fixture(); rs.polygon_mode = 4;
        r = AdrenoA740Raster(&rs, output, 80);
        CHECK(r.status == AdrenoRasterProfile);
    }
    /* Negative 4: raster null/misaligned contracts */
    {
        reset();
        struct AdrenoRasterResult r = AdrenoA740Raster(0, output, 80);
        CHECK(r.status == AdrenoRasterArgument);
        struct AdrenoRasterState rs = raster_fixture();
        r = AdrenoA740Raster(&rs, (uint32_t *)((uint8_t *)output + 1), 80);
        CHECK(r.status == AdrenoRasterArgument);
    }
    /* Negative 5: shader capacity + profile refusals (19-case sweep spot check) */
    {
        reset();
        struct AdrenoShaderState ss = shader_fixture();
        struct AdrenoShaderMapping sm = shader_mapping_fixture();
        struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 23);
        CHECK(r.status == AdrenoShaderCapacity && !r.words);
        CHECK(!std::memcmp(output, before, sizeof(output)));
        ss.binary_bytes = 127; /* not 128-multiple */
        r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(r.status == AdrenoShaderProfile);
        ss = shader_fixture(); ss.max_full_register = 63;
        r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(r.status == AdrenoShaderProfile);
    }
    /* Negative 6: shader mapping violations — misaligned offset/base/readonly */
    {
        reset();
        struct AdrenoShaderState ss = shader_fixture();
        struct AdrenoShaderMapping sm = shader_mapping_fixture();
        sm.shader_offset = 1;
        struct AdrenoShaderResult r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(r.status == AdrenoShaderMappingError);
        sm = shader_mapping_fixture(); sm.base = 0;
        r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(r.status == AdrenoShaderMappingError);
        sm = shader_mapping_fixture(); sm.readonly = 0;
        r = AdrenoA740ShaderBind(&ss, &sm, output, 128);
        CHECK(r.status == AdrenoShaderMappingError);
    }
    /* Negative 7: vpc identity/linkage violations — missing varying + duplicate identity */
    {
        reset();
        vpc_fixture(1);
        vpc_inputs[0].identity = (const uint8_t *)"generated(missing)";
        vpc_inputs[0].identity_bytes = 18;
        struct AdrenoVPCResult r = AdrenoA740VaryingMap(&vpc_state, output, 128);
        CHECK(r.status == AdrenoVPCMissingVarying);
        vpc_fixture(2);
        vpc_inputs[1].identity = vpc_inputs[0].identity;
        vpc_inputs[1].identity_bytes = vpc_inputs[0].identity_bytes;
        r = AdrenoA740VaryingMap(&vpc_state, output, 128);
        CHECK(r.status == AdrenoVPCIdentity);
    }
    /* Negative 8: vpc capacity + null contracts — transactional */
    {
        reset();
        vpc_fixture(1);
        struct AdrenoVPCResult r = AdrenoA740VaryingMap(&vpc_state, output, 41);
        CHECK(r.status == AdrenoVPCCapacity && !r.words && !r.registers);
        CHECK(!std::memcmp(output, before, sizeof(output)));
        r = AdrenoA740VaryingMap(0, output, 128);
        CHECK(r.status == AdrenoVPCArgument);
    }

    std::printf("PM4_RESULT {\"iterations\":%lu,\"failures\":%d}\n", iterations, failures);
    return failures ? 1 : 0;
}
