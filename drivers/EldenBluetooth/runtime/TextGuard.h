/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ELDEN_BT_STREAM_OFFSET UINT64_C(0xddd20)
static inline bool elden_bt_stream_rule(uint64_t offset,uint32_t bytes,const uint8_t *before,const uint8_t *after){
    static const uint8_t original[4]={0xa0,0x05,0x00,0x34};
    static const uint8_t replacement[4]={0x1f,0x20,0x03,0xd5};
    return offset==ELDEN_BT_STREAM_OFFSET&&bytes==4&&before&&after&&
        !memcmp(before,original,4)&&!memcmp(after,replacement,4);
}
static inline bool elden_bt_stream_identity(int32_t cpu,int32_t subtype,int64_t base,const uint8_t *cdhash){
    static const uint8_t expected[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
    return cpu==0x100000c&&subtype==0&&base==0&&cdhash&&!memcmp(cdhash,expected,sizeof(expected));
}
