/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "GeniSpi.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenTouchSpace {EldenTouchGcc,EldenTouchPins,EldenTouchIrq,EldenTouchSe,EldenTouchQup};
typedef struct {
    void *context;
    uint32_t (*read)(void *,enum EldenTouchSpace,uint32_t);
    void (*write)(void *,enum EldenTouchSpace,uint32_t,uint32_t);
    void (*delay)(void *,unsigned);
} EldenTouchIo;
typedef struct {
    EldenTouchIo io;struct geni_spi spi;
    uint32_t original_vote,original_cmd,original_cfg;
    uint32_t pin[4],irq_pin,engine[32];
    uint32_t entry_gsi,entry_dma,entry_tx,entry_rx,entry_m_irq,entry_s_irq;
    uint32_t entry_dma_tx_status,entry_dma_rx_status,entry_dma_tx_in,entry_dma_rx_in;
    bool touched_clock,touched_cfg,touched_pins,touched_engine;
    bool acquired,quarantined;
    bool survey_only; /* clock-qualified entry reads; no SE/pin/rate writes */
    bool dma_mode;
    uint32_t dma_saved[8]; /* IRQ enables and pointers, never trigger LEN regs */
    unsigned stage;int error;
} EldenTouchPlatform;
/* Caller owns SE4 and GPIO48..51/162, serializes the shared GCC vote and keeps
 * all mappings until release succeeds. No QUP firmware, SMMU or DMA writes.
 * The SE must be idle and have an enabled FIFO interface before takeover.
 */
int elden_touch_platform_acquire(EldenTouchPlatform *);
/* Resume after survey acquire under an externally qualified DMA route.
 * The caller has installed private DMA buffers/cache ops, and retains them. */
int elden_touch_platform_activate_dma(EldenTouchPlatform *);
int elden_touch_platform_transfer(EldenTouchPlatform *,const uint8_t *,uint8_t *,size_t);
int elden_touch_platform_release(EldenTouchPlatform *);
#ifdef __cplusplus
}
#endif
