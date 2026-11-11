/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenLogFilterPhase {EldenLogFilterOff,EldenLogFilterSet,EldenLogFilterActive,EldenLogFilterRestore,EldenLogFilterDone,EldenLogFilterFailed};
typedef struct {
    enum EldenLogFilterPhase phase;
    uint32_t epoch,error,requests,acks;
    bool up,pending,touched,restored;
} EldenLogFilter;
void elden_log_filter_init(EldenLogFilter *s);
void elden_log_filter_link(EldenLogFilter *s,bool up,uint32_t epoch);
void elden_log_filter_fail(EldenLogFilter *s,unsigned error);
int elden_log_filter_next(EldenLogFilter *s,bool start,bool stop,uint8_t out[32]);
void elden_log_filter_unsent(EldenLogFilter *s);
int elden_log_filter_receive(EldenLogFilter *s,uint32_t epoch,const uint8_t *p,size_t n);
#ifdef __cplusplus
}
#endif
