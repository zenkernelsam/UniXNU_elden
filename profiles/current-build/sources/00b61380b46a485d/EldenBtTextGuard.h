/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ELDEN_BT_STREAM_OFFSET UINT64_C(0xddd20)
struct elden_bt_native_rule { uint64_t offset; uint32_t bytes; uint8_t before[16],after[16]; };
static const struct elden_bt_native_rule elden_bt_native_rules[]={
{UINT64_C(908576),4,{160,5,0,52},{31,32,3,213}},
{UINT64_C(11264),8,{0,0,0,0,0,0,0,0},{1,0,128,82,159,70,11,20}},
{UINT64_C(2969828),4,{103,253,255,23},{199,182,244,23}},
{UINT64_C(3263632),4,{0,4,0,52},{31,32,3,213}},
{UINT64_C(3635500),8,{253,123,191,169,253,3,0,145},{0,0,128,82,192,3,95,214}},
{UINT64_C(11272),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{64,0,0,53,114,124,7,20,9,4,0,81,110,124,7,20}},
{UINT64_C(1973704),4,{9,4,0,81},{144,131,248,23}},
{UINT64_C(11288),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{160,2,0,53,96,73,0,240,0,0,37,145,1,15,129,82}},
{UINT64_C(11304),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{233,23,28,148,104,73,0,240,8,33,6,145,31,1,0,249}},
{UINT64_C(11320),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{104,73,0,240,8,1,6,145,31,1,0,249,104,73,0,240}},
{UINT64_C(11336),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{8,161,6,145,31,1,0,249,136,73,0,144,8,225,6,145}},
{UINT64_C(11352),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{31,1,0,249,104,73,0,240,8,225,36,145,31,1,0,185}},
{UINT64_C(11368),12,{0,0,0,0,0,0,0,0,0,0,0,0},{78,124,7,20,8,4,0,81,35,124,7,20}},
{UINT64_C(1973496),4,{8,4,0,81},{200,131,248,23}},
{UINT64_C(11380),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{72,0,0,53,242,124,7,20,9,5,0,81,208,124,7,20}},
{UINT64_C(1974204),4,{9,5,0,81},{46,131,248,23}},
{UINT64_C(11396),16,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{72,0,0,53,21,125,7,20,8,5,0,81,252,124,7,20}},
{UINT64_C(1974396),4,{8,5,0,81},{2,131,248,23}},
};
static inline bool elden_bt_stream_rule(uint64_t offset,uint32_t bytes,const uint8_t *before,const uint8_t *after){
    if(!before||!after||!bytes||bytes>16)return false;
    for(unsigned i=0;i<sizeof(elden_bt_native_rules)/sizeof(elden_bt_native_rules[0]);i++){
        const struct elden_bt_native_rule *r=&elden_bt_native_rules[i];
        if(offset==r->offset&&bytes==r->bytes&&!memcmp(before,r->before,bytes)&&!memcmp(after,r->after,bytes))return true;
    }
    return false;
}
static inline bool elden_bt_stream_identity(int32_t cpu,int32_t subtype,int64_t base,const uint8_t *cdhash){
    static const uint8_t expected[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
    return cpu==0x100000c&&subtype==0&&base==0&&cdhash&&!memcmp(cdhash,expected,sizeof(expected));
}
