/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Baseline may survey repeatedly. During a PD initialization, do not add
 * diagnostic traffic until success plus10s, then collect only one snapshot. */
typedef struct {uint64_t ready_at_ms;} EldenPowerPathSchedule;
static inline bool elden_power_reg_scheduled(EldenPowerPathSchedule *s,bool pd_enabled,
        bool pd_complete,unsigned completed_reads,uint64_t now_ms){
    if(!pd_enabled)return true;
    if(!pd_complete){s->ready_at_ms=0;return false;}
    if(completed_reads)return false;
    if(!s->ready_at_ms){s->ready_at_ms=now_ms+10000;return false;}
    return now_ms>=s->ready_at_ms;
}
