/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned position,type,min_mv,max_mv,source_ma,source_mw;
    unsigned requested_mv,requested_ma,requested_mw;
    bool exact_voltage,give_back,capability_mismatch;
} EldenPdContract;
/* SPR fixed/battery/variable and PPS PDO/RDO interpretation. This describes
 * the contract only, never actual input draw, source selection or net charge. */
int elden_pd_contract(const uint32_t *source_pdos,unsigned count,uint32_t rdo,EldenPdContract *out);
#ifdef __cplusplus
}
#endif
