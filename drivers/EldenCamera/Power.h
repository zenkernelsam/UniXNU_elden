/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_POWER_H
#define ELDEN_CAMERA_POWER_H
#include "Core.h"
#include "SharedI2c10.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    int (*read)(void *,uint8_t reg,uint8_t *value,int cleanup);
    int (*write)(void *,uint8_t reg,uint8_t value,int cleanup);
    int (*delay_us)(void *,unsigned);
    /* Original camera owner proves that target's sensor/CSI/DMA is inactive
     * before its last supply reference may be withdrawn. Probe's idle path
     * also needs actual ownership, not a successful dummy receiver callback. */
    int (*inactive)(void *,uint64_t owner,uint64_t epoch,unsigned sensor);
    /* Original regulator owner proves no other live consumer on this numbered
     * LDO1..7 before voltage mutation or final disable. LDO7 ALSO supplies the
     * rear lens; two sensor counters cannot prove that lens has retired. */
    int (*rail_unused)(void *,uint64_t owner,uint64_t epoch,unsigned ldo);
} EldenCameraPowerIo;
typedef union {uint64_t alignment;uint8_t bytes[1024];} EldenCameraPower;
typedef struct {uint32_t ready,stopped,changed,users[7],active_users[7],voltage_mask,enable_mask;
    uint32_t system_owned,system_retained_for_peer;uint8_t original_enable,observed_enable;int error;} EldenCameraPowerStatus;
int elden_camera_power_open(EldenCameraPower *,const EldenCameraPowerIo *);
int elden_camera_power_resource(EldenCameraPower *,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned resource,int on);
int elden_camera_power_status(EldenCameraPower *,EldenCameraPowerStatus *);
/* Requires no live owned references and actual both-camera inactivity.
 * Restores only our changed selectors/enable bits, preserving all unrelated
 * live channels/SYS. No WL reset, speculative supply drop or raw user access. */
int elden_camera_power_stop(EldenCameraPower *);
/* Native concrete read/write adapter for the sole shared backlight SE2 port.
 * Physical input-supply/PMIC-reset/sensor inactivity authority stays mandatory. */
typedef struct {EldenCameraI2c10Port *port;EldenCameraI2c10Authority authority;
    void *context;int (*inactive)(void *,uint64_t,uint64_t,unsigned);int (*delay_us)(void *,unsigned);
    int (*rail_unused)(void *,uint64_t,uint64_t,unsigned);} EldenCameraPowerBus;
int elden_camera_power_bind_bus(EldenCameraPower *,EldenCameraPowerBus *);
/* Retained concrete session: camera retirement proof is generated from the
 * actual WL ledger AND both original inactive callbacks before shared close.
 * External authority remains mandatory; no startup caller/physical gate is
 * invented. Original owner serializes open/resource/stop callbacks, including
 * against its physical provider changes. Zero-initialize once and retain this
 * storage until full reset. Shared retirement may refuse while core is busy. */
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t,uint64_t,int);
    int (*inactive)(void *,uint64_t,uint64_t,unsigned);
    int (*delay_us)(void *,unsigned);
    int (*rail_unused)(void *,uint64_t,uint64_t,unsigned);
} EldenCameraPowerOwner;
typedef struct {
    EldenCameraPower power;EldenCameraPowerBus bus;EldenCameraPowerOwner owner;
    uint32_t attempted,initialized,closed;
} EldenCameraPowerSession;
int elden_camera_power_session_open(EldenCameraPowerSession *,const EldenCameraPowerOwner *);
int elden_camera_power_session_resource(EldenCameraPowerSession *,uint64_t,uint64_t,unsigned,unsigned,int);
int elden_camera_power_session_stop(EldenCameraPowerSession *);
#ifdef __cplusplus
}
#endif
#endif
