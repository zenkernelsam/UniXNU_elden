/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SMMU_SHARED_H
#define ELDEN_SMMU_SHARED_H
#include "BootRoute.h"
#include "upstream/EldenWifiSmmu4KTableMemory.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenSmmuSharedWifi=0,EldenSmmuSharedCamera=1,EldenSmmuSharedSensor=2,EldenSmmuSharedGroups=3};
typedef struct {uint64_t iova,bytes;} EldenSmmuSharedAperture;
typedef struct {uint64_t owner,epoch;uint32_t groups,sensor_session;EldenSmmuSharedAperture apertures[3];} EldenSmmuSharedConfig;
typedef struct {uint32_t bank,asid,first_slot,slots,id0,id1,id2;uint64_t route_epoch;} EldenSmmuSharedLease;
typedef struct {
    EldenWifiSmmu4KConfig config;EldenWifiSmmu4KResult tables;
    EldenWifiSmmu4KRange ranges[8];unsigned range_count,capacity_pages;
    EldenWifiSmmu4KTableIo io;
    /* Entire actual retained data backing, including unmapped guard pages.
     * Cross-group overlap checks include CPU-owned guards and the whole table
     * capacity, rather than only DMA-visible leaves. */
    uint64_t data_physical,data_bytes;
} EldenSmmuSharedMemory;
typedef struct {
    void *context;
    /* Real sole original shared SMMU/power/boot owner. Serializes all APIs.
     * Observe/cleanup retain MMIO/power/IRQs/data. early proves before touch
     * seal; quiesced is actual master/DMA idle, never a local request ledger. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    int (*early)(void *,uint64_t owner,uint64_t epoch);
    int (*quiesced)(void *,uint64_t owner,uint64_t epoch,unsigned group);
    int (*memory)(void *,uint64_t owner,uint64_t epoch,unsigned group,const EldenSmmuSharedMemory *,int cleanup);
    /* Real global/context IRQ registration(1), status observation(2), context
     * mask/in-flight+GIC drain(0). Action3 drains global97 only after every
     * actual attempted context is disabled/TLB-synchronized; actual fresh
     * context reads are mandatory. No global drain inferred from masked IRQs.
     * Global97 is registered once; context INTID is from exact83-entry map. */
    int (*irq)(void *,uint64_t owner,uint64_t epoch,unsigned group,uint32_t bank,int action);
    /* Original touch route owner, serialized by same platform lease, supplies
     * actual slot7 baseline/active/tombstone expectation. No other register
     * override allowed. The shared core still independently reads it. */
    int (*peer_route)(void *,uint64_t,uint64_t,uint32_t offset,uint32_t baseline,uint32_t *expected);
    uint64_t (*now_us)(void *);
    EldenWifiSmmu4KIo io;
    /* Original physical authority proves exclusive bank/ASID/stream/aperture
     * ownership and exact platform ACTLR/security requirements. Called fresh
     * before each actual write and observation. No assumed reset ACTLR or
     * caller-supplied identity alone constitutes this proof. */
    int (*bank)(void *,uint64_t,uint64_t,unsigned,const EldenSmmuSharedLease *,int cleanup);
} EldenSmmuSharedOps;
typedef union {uint64_t alignment;uint8_t bytes[32768];} EldenSmmuShared;
typedef struct {
    uint32_t stage,groups,installed,active,retired,route_reserved,route_committed;
    uint32_t selected_banks[3],selected_asids[3],route_slots[6],route_count,reads,writes;
    int error;uint32_t last_offset,last_value,translation_mode;
} EldenSmmuSharedStatus;
/* Original exclusive EARLY owner configures one exact batch before allocation
 * and any route write. No default group/aperture or automatic startup. */
int elden_smmu_shared_open(EldenSmmuShared *,const EldenSmmuSharedConfig *,const EldenSmmuSharedOps *);
int elden_smmu_shared_lease(EldenSmmuShared *,uint64_t,uint64_t,unsigned,EldenSmmuSharedLease *);
int elden_smmu_shared_install(EldenSmmuShared *,uint64_t,uint64_t,unsigned,const EldenSmmuSharedMemory *);
int elden_smmu_shared_commit(EldenSmmuShared *,uint64_t,uint64_t);
int elden_smmu_shared_early(EldenSmmuShared *,uint64_t,uint64_t);
int elden_smmu_shared_admit(EldenSmmuShared *,uint64_t,uint64_t,unsigned,int cleanup);
int elden_smmu_shared_observe(EldenSmmuShared *,uint64_t,uint64_t,unsigned,int cleanup);
/* Retains exact SMR/S2CR/CBAR/CBA2R and all actual memory toreset. Disables only
 * a matched owned context after physical master idle and real context IRQ/TLB
 * drain. Global IRQ is retained while any peer context is active. */
int elden_smmu_shared_retire(EldenSmmuShared *,uint64_t,uint64_t,unsigned);
int elden_smmu_shared_status(EldenSmmuShared *,EldenSmmuSharedStatus *);
int elden_smmu_shared_configuration(EldenSmmuShared *,uint64_t,uint64_t,EldenSmmuSharedConfig *);
/* Actual read-only context/table observation while an early batch has some
 * installed groups and has not committed yet. No DMA readiness follows. */
int elden_smmu_shared_observe_prepared(EldenSmmuShared *,uint64_t,uint64_t,unsigned);
#ifdef __cplusplus
}
#endif
#endif
