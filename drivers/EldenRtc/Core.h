/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_RTC_CORE_H
#define ELDEN_RTC_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRtcInvalid=-22,EldenRtcStopped=-19,EldenRtcNoOffset=-61,
      EldenRtcCounterReset=-116,EldenRtcRange=-34};
typedef struct {
    void *context;
    int (*read)(void *,uint16_t address,uint8_t *,size_t);
} EldenRtcIo;
typedef struct {
    EldenRtcIo io;
    uint32_t offset,raw;
    uint8_t initialized,has_offset,has_raw,stopped,reset,reserved[3];
    uint32_t reads,learned;
} EldenRtc;
/* All methods serialized by the actual clock owner. Raw counter uses exactly
 * the Gen4 Linux PMK8350/8550 read/carry logic; no RTC register writes.
 * Time is raw+offset, never raw interpreted as a Unix timestamp. */
int elden_rtc_core_init(EldenRtc *,const EldenRtcIo *);
int elden_rtc_core_raw(EldenRtc *,uint32_t *);
int elden_rtc_core_read(EldenRtc *,uint64_t *);
/* System-set real wall time learns a volatile offset. This neither guesses
 * the stock SDAM cell nor changes firmware's raw counter/alarm. */
int elden_rtc_core_learn(EldenRtc *,uint64_t seconds);
void elden_rtc_core_stop(EldenRtc *);
#ifdef __cplusplus
}
#endif
#endif
