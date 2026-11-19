/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_WAKE_IRQ_H
#define ELDEN_WAKE_IRQ_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenWakeTlmm=0,EldenWakePdc=1,EldenWakeGic=2};
enum {EldenWakeRouteEmpty=0,EldenWakeRouteOwned=1,EldenWakeRouteArmed=2,EldenWakeRouteHeld=3,EldenWakeRouteStopped=4};
enum {EldenWakeGpio=162,EldenWakePdcPin=109,EldenWakeParentSpi=620,EldenWakeIntid=652};
typedef struct {
    void *context;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    int (*read)(void *,unsigned space,uint32_t reg,uint32_t *);
    int (*write)(void *,unsigned space,uint32_t reg,uint32_t);
    /* Exclusive real ArmCpu attach/mask/drain, not the shared TLMM summary.
     * mask on must return only after actual own GIC enable bit is clear. */
    int (*attach)(void *,unsigned intid,int edge);
    int (*mask)(void *,unsigned intid,int on);
    int (*drain)(void *,unsigned intid);
} EldenWakeIrqIo;
typedef struct {
    EldenWakeIrqIo io;uint64_t owner,epoch;
    uint32_t state,version,pdc_cfg,pdc_bank,tlmm_cfg,pin_ctl;
    uint32_t changed_pdc,changed_tlmm,attached;int error;
} EldenWakeIrq;
/* Actual original SM8750 direct-PDC route: GPIO162->pin109->SPI620/INTID652.
 * Normal DMA/SPI worker must already be paused and drained under the caller's
 * real owner/epoch gate. No pin mux/drive, rail, SE, SMMU or summary240 writes.
 * Caller serializes thread operations and all legacy shared-bank RMW peers;
 * the native adapter refuses <3.2 until a real common bank provider exists.
 * IRQ only masks and records pending.
 * No system/display wake or command-delivery success is implied by ownership. */
int elden_wake_irq_acquire(EldenWakeIrq *,const EldenWakeIrqIo *,uint64_t owner,uint64_t epoch);
int elden_wake_irq_arm(EldenWakeIrq *,uint64_t owner,uint64_t epoch);
int elden_wake_irq_disarm(EldenWakeIrq *,uint64_t owner,uint64_t epoch);
int elden_wake_irq_stop(EldenWakeIrq *,uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
