/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool elden_cpu_perf_query(uint32_t protocol,uint32_t message) {
    if(protocol==0x10)return message==0;
    return protocol==0x13&&(message==0||message==1||message==3||message==4||message==6||message==8);
}
