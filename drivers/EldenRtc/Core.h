/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_RTC_CORE_H
#define ELDEN_RTC_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRtcInvalid=-22,EldenRtcStopped=-19,EldenRtcNoOffset=-61,
      EldenRtcCounterReset=-116,EldenRtcRange=-34,EldenRtcNoProvider=-6,
      EldenRtcPersistError=-5,EldenRtcAlarmError=-5};
/* Reboot honesty about whether RAM time is backed by the named NVMEM cell:
 * None: no provider bound or never accessed, nothing persisted.
 * Uncertain: a bound cell whose stored content is unverified, stale relative
 * to RAM, or whose last access failed (a torn write cannot be excluded).
 * Verified: RAM offset was restored and validated against the live counter,
 * or was successfully written to the cell. */
enum {EldenRtcPersistNone,EldenRtcPersistUncertain,EldenRtcPersistVerified};
typedef struct {
    void *context;
    int (*read)(void *,uint16_t address,uint8_t *,size_t);
} EldenRtcIo;
typedef struct {
    void *context;
    int (*read)(void *,uint8_t out[4]);
    int (*write)(void *,const uint8_t data[4]);
} EldenRtcPersistence;
typedef struct {
    void *context;
    int (*read)(void *,uint16_t address,uint8_t *,size_t);
    int (*write)(void *,uint16_t address,const uint8_t *,size_t);
} EldenRtcAlarmIo;
typedef struct {
    EldenRtcIo io;
    EldenRtcPersistence persistence;
    EldenRtcAlarmIo alarm_io;
    uint32_t offset,raw;
    uint8_t initialized,has_offset,has_raw,stopped,reset,persistence_bound,
        alarm_bound,alarm_enabled,alarm_pending,alarm_uncertain,persist_state;
    uint32_t reads,learned;
    uint64_t alarm_seconds;
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
/* Persistence is an explicitly bound named four-byte NVMEM provider. The core
 * never chooses a cell or SDAM offset; without a bound provider the offset
 * stays unbound and volatile. Failed NVM access leaves the prior accepted
 * RAM state intact and marks persistence EldenRtcPersistUncertain. Restore
 * validates the stored value against the live counter before accepting it. */
int elden_rtc_core_bind_persistence(EldenRtc *,const EldenRtcPersistence *);
int elden_rtc_core_restore_offset(EldenRtc *);
int elden_rtc_core_learn_persistent(EldenRtc *,uint64_t seconds);
/* Returns EldenRtcPersistNone/Uncertain/Verified so reboot can decide whether
 * wall time was actually persisted rather than assuming it. */
int elden_rtc_core_persist_state(EldenRtc *);
/* PMK8350/PMK8550 alarm bank from the pinned Linux donor pmk8350_regs: data
 * 0x6240, control 0x6246 enable bit7, status/clear 0x6248 clear bit0. The
 * 0x61xx bank is the RTC counter and must never receive alarm traffic.
 * Programming requires a bound provider owning alarm register access;
 * Elden board IRQ (PMK8550 SID0 peripheral 0x62 interrupt 1 edge-rising)
 * and system wake delivery remain unvalidated without a real IRQ owner. */
enum {EldenRtcAlarmData=0x6240,EldenRtcAlarmControl=0x6246,
      EldenRtcAlarmStatus=0x6248,EldenRtcAlarmEnableBit=0x80,
      EldenRtcAlarmClearBit=0x01};
int elden_rtc_core_bind_alarm(EldenRtc *,const EldenRtcAlarmIo *);
/* Donor ordering: RMW-disable, 4-byte data write, RMW-enable. Enabled alarms
 * in the past are rejected. On a mid-sequence failure the cached
 * alarm_enabled/alarm_seconds only reflect register content confirmed by a
 * completed access; the disable step confirms alarm_enabled==0. A failed
 * register write may have partially applied and sets alarm_uncertain until a
 * fully completed set/read/ack resynchronizes the cache. */
int elden_rtc_core_alarm_set(EldenRtc *,uint64_t seconds,int enabled);
int elden_rtc_core_alarm_read(EldenRtc *,uint64_t *seconds,int *enabled);
/* Donor handler ordering: RMW-disable then RMW-clear status bit0. */
int elden_rtc_core_alarm_ack(EldenRtc *);
void elden_rtc_core_stop(EldenRtc *);
#ifdef __cplusplus
}
#endif
#endif
