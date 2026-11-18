/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_RTC_CORE_SHIM_H
#define ELDEN_RTC_CORE_SHIM_H
#include "Core.h"
typedef uint32_t u32;typedef uint8_t u8;
#define NUM_8_BIT_RTC_REGS 4
struct pm8xxx_rtc_regs {unsigned read;};
struct pm8xxx_rtc {EldenRtc *regmap;const struct pm8xxx_rtc_regs *regs;};
static int rtc_read_bytes(EldenRtc *r,unsigned address,void *out,size_t bytes){
    if(!r||!r->initialized||r->stopped||!out||!bytes||bytes>4||address<0x6146||address+bytes>0x614c)return EldenRtcInvalid;
    ++r->reads;return r->io.read(r->io.context,(uint16_t)address,out,bytes);
}
static int rtc_read_byte(EldenRtc *r,unsigned address,unsigned *out){
    uint8_t byte;int rc=rtc_read_bytes(r,address,&byte,1);if(!rc)*out=byte;return rc;
}
static uint32_t rtc_le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
#define regmap_bulk_read(p,r,v,n) rtc_read_bytes(p,r,v,n)
#define regmap_read(p,r,v) rtc_read_byte(p,r,v)
#define get_unaligned_le32(p) rtc_le32(p)
#endif
