/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SENSOR_NATIVE_PACK_H
#define ELDEN_SENSOR_NATIVE_PACK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Digests are supplied by reviewed real Android capture/firmware evidence.
 * No defaults or public device identity are fabricated in a build. */
typedef struct {
    uint8_t device_sha256[32],build_sha256[32],adsp_sha256[32],adsp_dtb_sha256[32],registry_sha256[32];
} EldenSensorPackIdentity;
typedef struct {
    uint32_t version;uint64_t bytes;
    uint8_t sha256[32],namespace_sha256[32];EldenSensorPackIdentity identity;
} EldenSensorPackSeal;
typedef struct {
    void *context;uint64_t owner,epoch;
    /*0only on actual same-device/build/firmware/registry identity and current
     * Data/transport ownership. Cleanup may run after public revocation. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,const EldenSensorPackIdentity *,int cleanup);
} EldenSensorPackAuthority;
typedef struct EldenSensorPackCache EldenSensorPackCache;
typedef struct {uint32_t state,reads,stopping;int error;uint64_t bytes,owner,epoch;} EldenSensorPackStatus;
/* Exact fixed RAM-Data path /private/var/ngpu/sensors/pack.bin. Native opens
 * one retained vnode, verifies regular/root/0444 metadata before and after
 * full read, SHA256 and original pack geometry. No DSP/ADB/SMMU access here.
 * Size=1only before Data ready; other failure latches and retains allocation.
 * Cache is immutable and held until reset. No re-open on every DSP read. */
int elden_sensor_pack_native_bind(const EldenSensorPackSeal *,const EldenSensorPackAuthority *,EldenSensorPackCache **);
int elden_sensor_pack_native_size(EldenSensorPackCache *,uint64_t owner,uint64_t epoch,uint64_t *);
int elden_sensor_pack_native_read(EldenSensorPackCache *,uint64_t owner,uint64_t epoch,uint64_t offset,void *,uint32_t length);
int elden_sensor_pack_native_stop(EldenSensorPackCache *,uint64_t owner,uint64_t epoch);
int elden_sensor_pack_native_status(EldenSensorPackCache *,EldenSensorPackStatus *);
#ifdef __cplusplus
}
#endif
#endif
