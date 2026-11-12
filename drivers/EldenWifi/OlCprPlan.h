/* SPDX-License-Identifier: GPL-2.0-only */
/* TB322FC firmware-directed PDC planning, without hardware side effects. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {ELDEN_OLCPR_MAX_PARAMS=16,ELDEN_OLCPR_NAME_BYTES=32,ELDEN_OLCPR_RAILS=4};
enum {EldenOlCprOk=0,EldenOlCprInvalid=-22,EldenOlCprRange=-34,EldenOlCprNoData=-61};
enum EldenOlCprRail {EldenOlCprS5f,EldenOlCprS4d,EldenOlCprS3g,EldenOlCprS7i};
typedef struct {
    char pin[ELDEN_OLCPR_NAME_BYTES];
    uint32_t wake_valid,wake,sleep_valid,sleep;
} EldenOlCprParam;
typedef struct {uint32_t wake_valid,wake,sleep_valid,sleep;} EldenOlCprVote;
typedef struct {
    EldenOlCprVote rail[ELDEN_OLCPR_RAILS];
    uint32_t requested,mapped,unknown,commands;
} EldenOlCprPlan;

/* Linux arithmetic: round upward to4, subtract16, add IR drop30/10.
 * Values retain the firmware/QMP convention; this API does not convert units
 * or authorize a regulator setting. Failure preserves the output. */
int elden_olcpr_adjust(uint32_t voltage,uint32_t ir_drop,uint32_t *result);
/* Fixed board aliases, per-rail maximum, then one complete plan. No alloc,
 * QMP/RPMh calls, MMIO or strings from firmware in a command. Unknown pins
 * follow Linux's accounting; all-unknown requests return NoData. */
int elden_olcpr_prepare(const EldenOlCprParam *,size_t,EldenOlCprPlan *);
const char *elden_olcpr_rail_name(unsigned);
const char *elden_olcpr_pdc_name(unsigned);
#ifdef __cplusplus
}
#endif
