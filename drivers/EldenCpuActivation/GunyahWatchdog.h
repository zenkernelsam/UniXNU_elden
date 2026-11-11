/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ABI from the pinned Lenovo gh_virt_wdt and upstream gunyah_wdt drivers. */
enum { ELDEN_VDOG_CONTROL=0x86000005U, ELDEN_VDOG_STATUS=0x86000006U,
       ELDEN_VDOG_PET=0x86000007U, ELDEN_VDOG_TIME=0x86000008U,
       ELDEN_VDOG_BARK_MS=27000U, ELDEN_VDOG_TIMEOUT_MS=30000U };
typedef struct { uint64_t a0,a1,a2,a3; } EldenVdogReply;
typedef EldenVdogReply (*EldenVdogCall)(void *,uint32_t,uint32_t,uint32_t);
EldenVdogReply elden_vdog_smc(void *,uint32_t,uint32_t,uint32_t);
typedef enum { ELDEN_VDOG_IDLE, ELDEN_VDOG_ARMED, ELDEN_VDOG_STOPPED,
               ELDEN_VDOG_REFUSED, ELDEN_VDOG_UNCERTAIN } EldenVdogState;
typedef struct {
    uint32_t op,arg1,arg2;
    volatile uint64_t a0,a1,a2,a3;
} EldenVdogTrace;
typedef struct {
    EldenVdogState state;
    uint32_t attempted, enable_attempted, calls, last_op;
    int32_t last_error;
    uint32_t control, elapsed_ms, expired;
    uint32_t trace_count,trace_overflow;
    EldenVdogTrace trace[8];
} EldenVdog;
enum { ELDEN_VDOG_OK=0, ELDEN_VDOG_BAD_STATE=-100,
       ELDEN_VDOG_BUSY=-101, ELDEN_VDOG_BAD_STATUS=-102,
       ELDEN_VDOG_NOT_READY=-103 };
/* Caller serializes access. Software timestamps and hardware/route qualification
 * belong to the board controller; this protocol core cannot certify them. */
int elden_vdog_status(EldenVdog *,EldenVdogCall,void *);
int elden_vdog_arm(EldenVdog *,EldenVdogCall,void *,int recovery_ready);
int elden_vdog_pet(EldenVdog *,EldenVdogCall,void *);
int elden_vdog_stop(EldenVdog *,EldenVdogCall,void *);
#ifdef __cplusplus
}
#endif
