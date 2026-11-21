/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_RTC_OWNER_H
#define ELDEN_RTC_OWNER_H
#include <stddef.h>
#include <stdint.h>
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRtcOwnerDisabled=-125,EldenRtcOwnerStale=-116,EldenRtcOwnerBusy=-16};
/* The guarded RTC persistence/alarm owner facade. This is a source-side
 * contract mainline can later bind to a real named NVMEM offset cell and the
 * real PMK8550 0x62/1 alarm owner. It chooses nothing: no cell, SDAM offset,
 * bank or IRQ comes from a filename or a donor default — every provider must
 * declare its name and be qualified before the existing core bind APIs run.
 * Default-off: a missing named provider or missing owner callbacks refuse. */
typedef struct {
    void *context;
    /* Named-provider identity; must equal the name the caller binds against,
     * non-empty. */
    const char *(*name)(void *context);
    /* Serialized provider owner/epoch; re-checked before every provider I/O so
     * a stale or foreign owner is fenced, not trusted. */
    int (*owner_epoch)(void *context,uint64_t *owner,uint64_t *epoch);
    /* Serialized four-byte NVMEM cell access. */
    int (*read)(void *context,uint8_t out[4]);
    int (*write)(void *context,const uint8_t data[4]);
} EldenRtcNvmem;
typedef struct {
    void *context;
    const char *(*name)(void *context);
    int (*owner_epoch)(void *context,uint64_t *owner,uint64_t *epoch);
    /* Serialized alarm-bank register access; the existing donor ordering
     * (0x6240/0x6246/0x6248, RMW-disable->data->RMW-enable) is preserved. */
    int (*read)(void *context,uint16_t address,uint8_t *out,size_t bytes);
    int (*write)(void *context,uint16_t address,const uint8_t *data,size_t bytes);
} EldenRtcAlarmOwner;
typedef struct {
    EldenRtc *core;
    EldenRtcNvmem nvmem;EldenRtcAlarmOwner alarm;
    EldenRtcPersistence persist_io;EldenRtcAlarmIo alarm_io;
    void *irq;
    uint64_t seq,nvmem_gen,alarm_gen,nvmem_owner,nvmem_epoch,alarm_owner,alarm_epoch;
    uint32_t nvmem_bound,alarm_bound,nvmem_registered,alarm_registered,stopped,busy;
    uint32_t restores,learns,alarm_sets,alarm_reads,acks,stops,rejected,dropped;
} EldenRtcOwner;
typedef struct {
    uint32_t nvmem_bound,alarm_bound,stopped;
    uint64_t nvmem_gen,alarm_gen,nvmem_owner,alarm_owner;
    uint32_t restores,learns,alarm_sets,alarm_reads,acks,rejected,dropped;
    int persist_state;
} EldenRtcOwnerStatus;
/* Bind this facade to one EldenRtc core (caller initialized it with the
 * read-only counter io). The facade never owns the counter io. */
int elden_rtc_owner_init(EldenRtcOwner *,EldenRtc *core);
/* Bind the named NVMEM offset cell. Requires every provider callback, an
 * explicit non-empty name equal to `named`, and a nonzero owner; only then is
 * the existing elden_rtc_core_bind_persistence invoked. Returns a generation
 * via *generation on success. */
int elden_rtc_owner_bind_nvmem(EldenRtcOwner *,const EldenRtcNvmem *,
    const char *named,uint64_t *generation);
/* Bind the named PMK8550 alarm owner plus the owner IRQ-path identity `irq`
 * used to authorize elden_rtc_owner_alarm_ack. Same named/owner/callback
 * qualification before elden_rtc_core_bind_alarm. */
int elden_rtc_owner_bind_alarm(EldenRtcOwner *,const EldenRtcAlarmOwner *,
    const char *named,void *irq,uint64_t *generation);
int elden_rtc_owner_restore(EldenRtcOwner *,uint64_t generation);
int elden_rtc_owner_learn(EldenRtcOwner *,uint64_t generation,uint64_t seconds);
int elden_rtc_owner_alarm_set(EldenRtcOwner *,uint64_t generation,uint64_t seconds,int enabled);
int elden_rtc_owner_alarm_read(EldenRtcOwner *,uint64_t generation,uint64_t *seconds,int *enabled);
/* Alarm acknowledge is callable only from the owner-supplied IRQ path: the
 * `irq` argument must be the exact identity bound at bind_alarm, never an
 * arbitrary client value. */
int elden_rtc_owner_alarm_ack(EldenRtcOwner *,uint64_t generation,void *irq);
/* Facade-level stop: prevents all later provider I/O and publication while the
 * provider registration is retained. It gates the facade's guarded operations
 * only; the live RTC core is not terminated here (its clock lifecycle is the
 * core owner's own elden_rtc_core_stop path). */
int elden_rtc_owner_stop(EldenRtcOwner *,uint64_t generation);
/* Explicit stop -> retire fence: requires a prior stop and refuses while a
 * provider callback is active. A successful retire releases the retained
 * provider registrations and clears the stopped state, leaving the facade
 * reusable for a new qualified bind (a fresh generation is armed; prior
 * generations stay stale). */
int elden_rtc_owner_retire(EldenRtcOwner *,uint64_t generation);
/* Read-only status for tests/status surfaces; no side effects. */
int elden_rtc_owner_status(const EldenRtcOwner *,EldenRtcOwnerStatus *);
#ifdef __cplusplus
}
#endif
#endif
