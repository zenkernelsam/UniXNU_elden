/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_SMMU_SHARED_NATIVE_H
#define ELDEN_SMMU_SHARED_NATIVE_H
#include "Shared.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint64_t physical;uintptr_t mapping;size_t bytes;} EldenSmmuNativeMapping;
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Original sole boot/SMMU owner retains the actual two mappings, clocks,
     * power and all callbacks toreset. No new mapping or legacy Wi-Fi SMMU
     * IRQ owner may be installed concurrently. Serializes EARLY/start/stop
     * with the real touch owner; mapping validates retained IOKit aliases. */
    int (*admit)(void *,uint64_t,uint64_t,int cleanup);
    int (*mapping)(void *,uint64_t,uint64_t,const EldenSmmuNativeMapping *,const EldenSmmuNativeMapping *,int cleanup);
    int (*early)(void *,uint64_t,uint64_t);
    int (*quiesced)(void *,uint64_t,uint64_t,unsigned group);
    int (*memory)(void *,uint64_t,uint64_t,unsigned,const EldenSmmuSharedMemory *,int cleanup);
    /* Approves the observed ACTLR with original exact security/ASID/aperture
     * ownership. There is no guessed default, firmware rewrite or privilege
     * bypass. Readback alone is not approval. */
    int (*bank)(void *,uint64_t,uint64_t,unsigned,const EldenSmmuSharedLease *,uint32_t actlr,int cleanup);
    int (*touch)(void *,uint64_t,uint64_t,uint32_t,uint32_t,uint32_t *);
} EldenSmmuNativeAuthority;
typedef struct EldenSmmuNative EldenSmmuNative;
typedef struct {
    uint32_t prepared,global_registered,context_registered,context_drained,global_drained,inflight[4];
    uint32_t faulted_groups,global_fault,read_errors,spurious[4],status[4],syndrome[4];uint64_t address[3];
    int error;EldenSmmuSharedStatus shared;
} EldenSmmuNativeStatus;
/* Adopts the two actual original mappings; never maps/reset global hardware
 * or registers a second global97 handler. It prepares the exact early batch
 * through real MMIO and GIC. Native object/handlers/memory remain toreset even
 * on failure. No automatic caller or tablet access. */
int elden_smmu_native_open(const EldenSmmuSharedConfig *,const EldenSmmuNativeAuthority *,
    const EldenSmmuNativeMapping *,const EldenSmmuNativeMapping *,EldenSmmuNative **);
int elden_smmu_native_shared(EldenSmmuNative *,uint64_t,uint64_t,EldenSmmuShared **);
int elden_smmu_native_status(EldenSmmuNative *,EldenSmmuNativeStatus *);
#ifdef __cplusplus
}
#endif
#endif
