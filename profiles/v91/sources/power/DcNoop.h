/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "PowerPathRead.h"
#ifdef __cplusplus
extern "C" {
#endif
enum EldenDcNoopPhase {EldenDcNoopOff,EldenDcNoopProbe,EldenDcNoopWrite,
    EldenDcNoopVerify,EldenDcNoopDone,EldenDcNoopFailed};
enum {EldenDcNoopUnsafe=1,EldenDcNoopProtocol,EldenDcNoopTimeout,EldenDcNoopReadback,EldenDcNoopStopped};
typedef struct {
    EldenPowerPathRead snapshot;
    enum EldenDcNoopPhase phase;
    bool up,pending,write_attempted,verified;
    uint32_t epoch,error,write_acks;
} EldenDcNoop;
void elden_dc_noop_init(EldenDcNoop *s);
void elden_dc_noop_link(EldenDcNoop *s,bool up,uint32_t epoch);
/* One attempt for the boot. Only the fixed 500mA value may ever be written. */
int elden_dc_noop_begin(EldenDcNoop *s);
int elden_dc_noop_next(EldenDcNoop *s,bool qualified,uint8_t out[28]);
void elden_dc_noop_unsent(EldenDcNoop *s);
void elden_dc_noop_fail(EldenDcNoop *s,unsigned reason);
void elden_dc_noop_cancel(EldenDcNoop *s);
int elden_dc_noop_receive(EldenDcNoop *s,const uint8_t *p,size_t bytes);
#ifdef __cplusplus
}
#endif
