/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#include <stdbool.h>
static inline uint64_t elden_cold_token(uint32_t epoch,uint32_t ms){return (uint64_t)epoch<<32|ms;}
/* The diagnostic runs have a <=900s lifetime, far shorter than one ms-counter wrap. */
static inline bool elden_cold_token_valid(uint64_t token,uint32_t epoch,uint32_t ms){
    return epoch&&(uint32_t)(token>>32)==epoch&&(uint32_t)(ms-(uint32_t)token)<=1000u;
}
