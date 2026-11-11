/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenWcnRail {EldenWcnS5f,EldenWcnL3f,EldenWcnL2f,EldenWcnS4d,EldenWcnS7i,EldenWcnS3g,EldenWcnRailCount};
typedef enum EldenWcnRail EldenWcnRail;
typedef struct {uint16_t mv;uint8_t status,enable,mode;} EldenWcnRailState;
typedef struct {const char *resource;unsigned ppid;uint16_t minimum,maximum;} EldenWcnRailSpec;
extern const EldenWcnRailSpec elden_wcn_rail_specs[EldenWcnRailCount];
enum EldenWcnPowerState {EldenWcnPowerIdle,EldenWcnPowerPreparing,EldenWcnPowerReady,
                         EldenWcnPowerStopped,EldenWcnPowerClosed,EldenWcnPowerFailed};
typedef struct {
    void *context;
    int (*preflight)(void *); /* exact cmd-db/APID/GPIO/AOSS/parent validation */
    int (*rail_read)(void *,EldenWcnRail,EldenWcnRailState *);
    int (*rail_vote)(void *,EldenWcnRail,uint16_t mv,uint8_t mode,bool enable);
    int (*qmp)(void *,const char *);
    int (*bt_set)(void *,bool level); /* complete + strict readback */
    int (*bt_get)(void *,bool *level);
    int (*wlan_get)(void *,bool *level); /* never writes another WLAN consumer */
    int (*parents)(void *,uint32_t *main_hz,uint32_t *even_hz);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,unsigned);
} EldenWcnPowerIo;
typedef struct {
    EldenWcnPowerIo io;
    EldenWcnRailState before[EldenWcnRailCount];
    uint16_t target[EldenWcnRailCount];
    uint64_t epoch;
    unsigned state,stage,qmp_messages,rail_votes;
    int error;
    bool quarantined,control_started,bt_changed,baseline_votes_retained;
} EldenWcnPower;
enum {EldenWcnPowerAgain=-11,EldenWcnPowerArgument=-22,EldenWcnPowerIdentity=-19,
      EldenWcnPowerProtocol=-71,EldenWcnPowerTimeout=-110};
/* Physical callbacks, exact board resources and a single shared owner.
 * Retain inherited shared rail votes while boot consumers remain unknown.
 * No RF_CLK1, regulator disable, implicit retry or unconditional successful lease. */
int elden_wcn_power_acquire(EldenWcnPower *,uint64_t epoch);
int elden_wcn_power_stop(EldenWcnPower *,uint64_t epoch);
int elden_wcn_power_finish(EldenWcnPower *,uint64_t epoch);
int elden_wcn_power_check(const EldenWcnPower *,uint64_t epoch,unsigned operation);
/* Taycan GPLL0 fixed rate: L low16, alpha16, USER_CTL postdiv10:13.
 * Inputs are a stable live read; false/zero when not running or unsupported. */
int elden_wcn_parent_decode(uint32_t mode,uint32_t opmode,uint32_t l,uint32_t alpha,
    uint32_t user,uint32_t *main_hz,uint32_t *even_hz);
typedef struct {
    char pin[33];
    uint32_t wake,sleep;
    bool wake_valid,sleep_valid;
} EldenWcnPmuParam;
typedef struct {uint32_t wake,sleep;bool wake_valid,sleep_valid;} EldenWcnPdcVote;
/* Pure whole-input OL-CPR preflight, mapped max aggregation, no sends on error. */
int elden_wcn_cpr_plan(const EldenWcnPmuParam *,size_t,EldenWcnPdcVote out[4]);
int elden_wcn_power_cpr(EldenWcnPower *,uint64_t,const EldenWcnPmuParam *,size_t);
#ifdef __cplusplus
}
#endif
