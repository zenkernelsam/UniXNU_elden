/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_QUEUE_H
#define ELDEN_HAPTICS_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenHapticsQueueSlots=32,EldenHapticsQueuePoints=64,EldenHapticsBudgetSlots=64};
enum {EldenHapticsQueued=1,EldenHapticsRunning=2,EldenHapticsComplete=3,
      EldenHapticsCancelled=4,EldenHapticsFailed=5};
enum {EldenHapticsRequestArgument=-22,EldenHapticsRequestBusy=-16,
      EldenHapticsRequestDenied=-13,EldenHapticsRequestStale=-116,EldenHapticsRequestClock=-110};
typedef struct {uint64_t owner,epoch,id,not_before_us;uint32_t duration_us;
    uint8_t mask,wave,gain[2],reserved;} EldenHapticsRequest;
typedef struct {uint64_t owner,epoch,id;uint32_t state;int error;} EldenHapticsResult;
typedef struct {uint16_t intensity_q15;uint8_t gain[2];} EldenHapticsClickPoint;
typedef struct {
    const EldenHapticsClickPoint *points;
    uint32_t max_duration_us,max_late_us,window_us,max_on_us[2],max_dual_on_us;
    uint64_t square_budget[2];
    uint16_t max_sum_gain;
    uint8_t count,max_gain[2],available_mask;
} EldenHapticsQueueLimits;
typedef struct {
    void *context;
    /* owner+epoch identify the actually admitted native session. This is
     * checked at submit AND execution; no caller-controlled calibrated flag.
     * All callbacks and queue methods run on the original native worker/gate. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch);
    int (*start)(void *,unsigned motor,uint8_t wave,uint8_t gain,uint32_t duration_us);
    int (*poll)(void *,unsigned motor); /*0 pending,1 actual stop/done,<0 error*/
    int (*stop)(void *,unsigned motor); /*real stopped/standby result*/
    void (*complete)(void *,const EldenHapticsResult *);
    /* Optional persistent native RAM/RTP budget, called after this queue's
     * own limits and before a physical start. Existing standalone users may
     * leave NULL; the native broker supplies the actual shared provider. */
    int (*reserve)(void *,const EldenHapticsRequest *,uint64_t now_us);
} EldenHapticsQueueIo;
typedef struct {EldenHapticsRequest request;uint8_t used,cancel,reserved[6];uint64_t sequence;} EldenHapticsQueueSlot;
typedef struct {uint64_t expires_us,square[2];uint32_t duration_us;uint8_t mask,used;} EldenHapticsBudget;
typedef struct {
    EldenHapticsQueueIo io;EldenHapticsQueueLimits limits;
    EldenHapticsClickPoint points[EldenHapticsQueuePoints];EldenHapticsQueueSlot slots[EldenHapticsQueueSlots];
    EldenHapticsBudget budget[EldenHapticsBudgetSlots];
    uint64_t clock_us,next_sequence,active_deadline;
    uint64_t last_owner,last_epoch,last_id;
    int active_index,error;
    uint8_t initialized,stopping,held,has_clock,clock_failed,active_mask;
} EldenHapticsQueue;
/* Caller zero-initializes. A valid measured click LUT is copied here so user
 * memory cannot change limits during execution. This is not a certification
 * of the LUT; native initialization must qualify source/device calibration. */
int elden_haptics_queue_init(EldenHapticsQueue *,const EldenHapticsQueueLimits *,const EldenHapticsQueueIo *);
/* Per-session monotonically increasing id; up to32 queued requests. A request
 * has one waveform/timing, but both motors use independently measured gains.
 * All queued durations are <=250ms. No infinite loop or arbitrary replay.
 * Return0 means queued only; completion callback reports actual outcome. */
int elden_haptics_queue_submit(EldenHapticsQueue *,uint64_t owner,uint64_t epoch,uint64_t id,
    uint64_t not_before_us,uint32_t duration_us,uint8_t motor_mask,uint8_t wave,uint16_t intensity_q15);
int elden_haptics_queue_step(EldenHapticsQueue *,uint64_t now_us);
int elden_haptics_queue_cancel(EldenHapticsQueue *,uint64_t owner,uint64_t epoch,uint64_t id);
int elden_haptics_queue_disconnect(EldenHapticsQueue *,uint64_t owner,uint64_t epoch);
int elden_haptics_queue_stop(EldenHapticsQueue *,uint64_t now_us);
int elden_haptics_queue_terminal(const EldenHapticsQueue *);
#ifdef __cplusplus
}
#endif
#endif
