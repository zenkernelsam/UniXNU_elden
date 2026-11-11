/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ELDEN_POWER_REG_OWNER 32783u
#define ELDEN_POWER_REG_VALUES 19u
enum {EldenPowerRegLength=1,EldenPowerRegTuple,EldenPowerRegFirmware,EldenPowerRegTimeout,EldenPowerRegOverflow};
typedef struct {
    bool up,active,pending,failed,extended;
    uint32_t epoch,cycles,requests,errors,error,reply_count;
    unsigned index;
    uint8_t working[ELDEN_POWER_REG_VALUES],values[ELDEN_POWER_REG_VALUES];
} EldenPowerPathRead;
void elden_power_reg_init(EldenPowerPathRead *s);
void elden_power_reg_extend(EldenPowerPathRead *s);
unsigned elden_power_reg_count(const EldenPowerPathRead *s);
void elden_power_reg_link(EldenPowerPathRead *s,bool up,uint32_t epoch);
int elden_power_reg_begin(EldenPowerPathRead *s);
int elden_power_reg_next(EldenPowerPathRead *s,uint8_t out[28]);
void elden_power_reg_unsent(EldenPowerPathRead *s);
void elden_power_reg_fail(EldenPowerPathRead *s,unsigned error);
/* Ignore unrelated/duplicate replies; 1 is one byte,2 is a complete snapshot.
 * Only bus0/SID7 and a fixed sequence can be requested. Extended mode adds
 * the qualified suspend and GPIO9/10 register observations; never a write. */
int elden_power_reg_receive(EldenPowerPathRead *s,const uint8_t *data,size_t bytes);
#ifdef __cplusplus
}
#endif
