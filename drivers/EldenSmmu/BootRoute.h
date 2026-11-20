/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t slot,bank,smr,s2cr,cbar,cba2r;
} EldenSmmuBootRoute;
enum {EldenSmmuBootMaxRoutes=6};
/* One atomic early transaction may contain exact Wi-Fi1401, camera1c00 and
 * all4 ADSP sessionN(100N/104N/106N/108N,N3..8) routes. No masks/aliases or
 * other consumers. Shared sensor rows must share the same actual bank image;
 * Wi-Fi/camera/sensor groups must have different banks and distinct slots.
 * Legacy single-Wi-Fi APIs retain their exact one-row/no-append behavior. */
int elden_smmu_boot_routes_reserve(const void *provider,uint64_t epoch,const EldenSmmuBootRoute *,unsigned count);
int elden_smmu_boot_routes_commit(const void *provider,uint64_t epoch,const EldenSmmuBootRoute *observed,unsigned count);
int elden_smmu_boot_routes_peek(const void *provider,uint64_t epoch,uint32_t offset,uint32_t baseline,uint32_t *expected);
int elden_smmu_boot_routes_poison(const void *provider,uint64_t epoch);
int elden_smmu_boot_routes_early(const void *provider,uint64_t epoch);
enum {EldenSmmuBootOpen,EldenSmmuBootPreparing,EldenSmmuBootReady,
      EldenSmmuBootSealedEmpty,EldenSmmuBootSealedReady,EldenSmmuBootPoisoned};
/* One early provider, before UFS/Data publication permits touch DMA startup.
 * Reserve AFTER real baseline/ownership preflight and BEFORE the first SMMU
 * write. The registry is bookkeeping, not MMIO/lease/table/fault-handler proof.
 * The provider must own its bank/tables and keep WLAN physically quiesced. */
int elden_smmu_boot_route_reserve(const void *provider,const EldenSmmuBootRoute *);
/* observed must contain actual post-program register reads; caller must also
 * verify the context/table/translation state, not copy the intended values.
 * Failed programming/readback must poison; no retry or late replacement. */
int elden_smmu_boot_route_commit(const void *provider,const EldenSmmuBootRoute *observed);
int elden_smmu_boot_route_poison(const void *provider);
/* Called before the first touch snapshot. Empty/ready seal permanently. An
 * in-progress provider refuses: startup ordering must ensure it is terminal.
 * The published mapping then stays unchanged, including through stopping,
 * until platform reset; no DMA/power claim follows from registry membership. */
int elden_smmu_boot_route_seal(void);
int elden_smmu_boot_route_expected(uint32_t offset,uint32_t baseline,uint32_t *expected);
unsigned elden_smmu_boot_route_state(void);
#ifdef __cplusplus
}
#endif
