/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_NATIVE_DMA_H
#define ELDEN_CAMERA_NATIVE_DMA_H
#include "Pipeline.h"
#include "../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint64_t iova;uint32_t buffers[2];} EldenCameraDmaConfig;
typedef struct {
    uint32_t id0,id1,id2,bank,slot,asid;uint64_t route_epoch;
} EldenCameraDmaLease;
typedef struct {
    EldenCameraBuffer buffer;uint64_t physical,mapped_bytes;uint32_t sensor,stride;
} EldenCameraDmaSlot;
typedef struct {
    uint64_t owner,epoch,iova,allocation_bytes,data_physical,table_physical;
    uint32_t count,table_bytes;uint16_t stream_id,reserved;
    EldenCameraDmaLease lease;EldenCameraDmaSlot slots[8];
    const volatile uint64_t *table_mapping;
    EldenWifiSmmu4KConfig table_config;EldenWifiSmmu4KResult tables;
    EldenWifiSmmu4KContextImage context;
} EldenCameraDmaView;
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t,uint64_t,int cleanup);
    /* Real original boot/route owner must prove EARLY before touch snapshot/
     * route seal; current one-WiFi registry does not admit a camera by itself.
     * Normal install requires this proof before and after reservation/build.
     * No late-route exception or assumed independent SMMU ownership. */
    int (*early)(void *,uint64_t,uint64_t);
    /* Reserves actual low IOVA, bank/ASID and exclusive exact SID1c00. Both
     * sensors share this domain. Failure is not proof no action executed. */
    int (*reserve)(void *,uint64_t,uint64_t,const EldenCameraDmaConfig *,uint64_t bytes,EldenCameraDmaLease *);
    int (*install)(void *,uint64_t,uint64_t,const EldenCameraDmaView *);
    /* Actual immutable same-epoch SMMU context/table/route/fault checks.
     * completion1 additionally proves device-written visibility for buffer;
     * no synthetic SOF/frame/DMA status. Nonblocking admit checks separately. */
    int (*observe)(void *,uint64_t,uint64_t,const EldenCameraDmaView *,const EldenCameraBuffer *,int completion);
    /* After original source/receiver/IRQ/DMA stopped: observe real domain/TLB/
     * fault-handler retirement. Immutable early SMR/S2CR route and all memory
     * remain reserved to full reset (touch seal), never reusable/free here. */
    int (*retire)(void *,uint64_t,uint64_t,const EldenCameraDmaView *,unsigned reservation_attempted,unsigned install_attempted);
} EldenCameraDmaOwner;
typedef struct EldenCameraNativeDma EldenCameraNativeDma;
typedef struct {
    uint32_t stage,reason,access,attribute,reservation_attempted,install_attempted,installed,stopping,retired;
    int error;uint64_t offset,physical,expected_physical,segment_bytes,par,bytes;
    uint32_t buffers,table_pages,leaf_pages,device_owned,completed,borrowed,returned;
} EldenCameraDmaStatus;
/* Explicit one-use retained camera arena,2..4buffers per enabled target.
 * Actual owned high-PA descriptor ->32bitIOVA,guard pages not SMMU mapped;
 * same original IOBuffer/PAR/UniquePrefaultNC methods as sensor memory.
 * No automatic start/second SMMU/guessed bank/fixed PA or device pixels. */
int elden_camera_dma_open(const EldenCameraDmaConfig *,const EldenCameraDmaOwner *,EldenCameraNativeDma **);
int elden_camera_dma_view(EldenCameraNativeDma *,uint64_t,uint64_t,EldenCameraDmaView *);
int elden_camera_dma_admit(EldenCameraNativeDma *,uint64_t,uint64_t,int cleanup);
int elden_camera_dma_buffer(EldenCameraNativeDma *,uint64_t,uint64_t,unsigned sensor,const EldenCameraBuffer *,int completion);
/* Called by the actual pipeline dequeue wrapper, after real SOF/done and
 * visibility. Frame remains borrowed until explicit release; initial queue,
 * completion, borrow and return are different states, no premature reuse. */
int elden_camera_dma_claim_frame(EldenCameraNativeDma *,uint64_t,uint64_t,unsigned sensor,const EldenCameraFrame *);
int elden_camera_dma_release_frame(EldenCameraNativeDma *,uint64_t,uint64_t,unsigned sensor,const EldenCameraFrame *);
int elden_camera_dma_stop(EldenCameraNativeDma *,uint64_t,uint64_t);
int elden_camera_dma_status(EldenCameraNativeDma *,EldenCameraDmaStatus *);
#ifdef __cplusplus
}
#endif
#endif
