/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_WAKE_NATIVE_IRQ_H
#define ELDEN_WAKE_NATIVE_IRQ_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Only the original touch owner invokes these, after its real worker/DMA
 * pause proof. The supplied gate must validate that same owner/epoch and
 * current bus/drain state. No callback success defaults are supplied.
 * Native never starts a competing SPI worker and performs no pin/rail writes. */
int elden_wake_native_irq_acquire(uint64_t owner,uint64_t epoch,void *context,
    int (*gate)(void *,uint64_t owner,uint64_t epoch,int cleanup));
int elden_wake_native_irq_arm(uint64_t owner,uint64_t epoch);
int elden_wake_native_irq_disarm(uint64_t owner,uint64_t epoch);
int elden_wake_native_irq_stop(uint64_t owner,uint64_t epoch);
/* IRQ handler masks only owned652, increments pending and wakes this event.
 * Original worker waits without periodic SPI reads while gesture path is armed.
 * This is a wake-request event, not a decoded gesture or system-wake success. */
const void *elden_wake_native_irq_event(void);
int elden_wake_native_irq_take(uint64_t owner,uint64_t epoch,uint32_t *events);
/* No latch consumption; after assert_wait the owning worker uses this to
 * close IRQ-arrived-before-sleep. No provider/MMIO callback after assert_wait;
 * this is1pending/0none/error state only, not fresh hardware admission proof. */
int elden_wake_native_irq_pending(uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
