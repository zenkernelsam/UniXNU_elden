/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_ACTUATOR_ABI_H
#define ELDEN_ACTUATOR_ABI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ELDEN_ACTUATOR_MAGIC 0x45414354u
#define ELDEN_ACTUATOR_VERSION 1u
#define ELDEN_ACTUATOR_RATE 48000u
#define ELDEN_ACTUATOR_OUTPUT_RATE 24000u
#define ELDEN_ACTUATOR_RING_FRAMES 16384u
#define ELDEN_ACTUATOR_HEADER_BYTES 4096u
#define ELDEN_ACTUATOR_IO_FRAMES 256u
#define ELDEN_ACTUATOR_SAFETY_FRAMES 128u
#define ELDEN_ACTUATOR_TAKE_FRAMES 64u
enum {EldenActuatorInfo=6,EldenActuatorStart=7,EldenActuatorStop=8,EldenActuatorMemory=8};
enum {EldenActuatorReady=1u,EldenActuatorRequested=2u,EldenActuatorRunning=4u,EldenActuatorStopped=8u,EldenActuatorHeld=16u};
typedef struct {
    uint32_t magic,version,rate,ring_frames,io_frames,safety_frames,take_frames,reserved;
    uint64_t anchor_host,ticks_per_frame,owner,epoch,generation;
    uint32_t state;int32_t error;
    uint64_t taken,written,writes,dropped,invalid,frames_consumed,frames_sent;
} EldenActuatorPage;
/* seq protects sample AND absolute frame. Fresh/late writes never reuse a
 * different turn silently. The sole plugin process serializes its writers;
 * kernel read uses bounded sequence checks, never an unbounded writer wait. */
typedef struct {uint32_t seq;int32_t sample_q15;uint64_t frame,generation;} EldenActuatorSlot;
#define ELDEN_ACTUATOR_BYTES (ELDEN_ACTUATOR_HEADER_BYTES+ELDEN_ACTUATOR_RING_FRAMES*sizeof(EldenActuatorSlot))
typedef struct {
    EldenActuatorPage *page;EldenActuatorSlot *slots;
    uint64_t anchor,owner,epoch,taken,generation;
} EldenActuatorRing;
int elden_actuator_ring_init(EldenActuatorRing *,void *memory,size_t bytes,uint64_t anchor,uint64_t owner,uint64_t epoch);
int elden_actuator_ring_bind(EldenActuatorRing *,void *memory,size_t bytes,uint64_t owner,uint64_t epoch);
int elden_actuator_ring_check(const EldenActuatorRing *);
/* User writer only; caller serializes writers. Rejects late/far-future frames,
 * not a promise that a real driver can meet CoreAudio's host-clock deadline. */
int elden_actuator_ring_write(EldenActuatorRing *,uint64_t frame,const int16_t *,size_t count);
/* Kernel retains its own cursor, anchor and identity; shared fields never
 * choose the hardware time or authorize a fresh session/voltage. */
int elden_actuator_ring_read(EldenActuatorRing *,uint64_t frame,int16_t *out);
void elden_actuator_ring_taken(EldenActuatorRing *,uint64_t end);
/* Native writer calls only after real STOP before a new run. Old mapped rings
 * retain their owner/epoch and old frames cannot cross a run generation. */
int elden_actuator_ring_new_run(EldenActuatorRing *,uint64_t first_frame);
uint64_t elden_actuator_frame(const EldenActuatorRing *,uint64_t host);
#ifdef __cplusplus
}
static_assert(sizeof(EldenActuatorPage)<=ELDEN_ACTUATOR_HEADER_BYTES,"actuator page");
static_assert(sizeof(EldenActuatorSlot)==24,"actuator slot ABI");
#else
_Static_assert(sizeof(EldenActuatorPage)<=ELDEN_ACTUATOR_HEADER_BYTES,"actuator page");
_Static_assert(sizeof(EldenActuatorSlot)==24,"actuator slot ABI");
#endif
#endif
