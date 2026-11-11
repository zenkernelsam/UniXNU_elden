/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "BtGeni.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenWcnUartSpace {EldenWcnUartGcc,EldenWcnUartPins,EldenWcnUartSe};
enum EldenWcnUartAdmission {EldenWcnUartAcquire=1,EldenWcnUartLive,EldenWcnUartRadioStopped};
typedef enum EldenWcnUartSpace EldenWcnUartSpace;
typedef enum EldenWcnUartAdmission EldenWcnUartAdmission;
enum {EldenWcnUartArgument=-22,EldenWcnUartBusy=-16,EldenWcnUartAgain=-11,
      EldenWcnUartClock=-19,EldenWcnUartProtocol=-71,EldenWcnUartTimeout=-110};
typedef struct {
    void *context;
    uint32_t (*read)(void *,enum EldenWcnUartSpace,uint32_t);
    void (*write)(void *,enum EldenWcnUartSpace,uint32_t,uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,unsigned);
    /* Verified radio power/reset lease and current GPLL0 parent rate, not
     * boot arguments or nominal guessed frequencies. No power writes here. */
    int (*admit)(void *,enum EldenWcnUartAdmission);
    uint32_t (*parent_hz)(void *,unsigned source);
    /* Serialize read/modify/write of the shared GCC vote with other users. */
    int (*gcc_lock)(void *,bool acquire);
} EldenWcnUartIo;
typedef struct {
    EldenWcnUartIo io;PbGeni engine;
    uint32_t vote,rcg[5],pin[4],configuration[40];
    uint32_t baud,level;
    unsigned stage,tx_bytes,rx_bytes,discarded_rx;
    int error;
    bool clock_changed,rate_changed,pins_changed,engine_changed;
    bool acquired,quarantined,rfr_ready;
} EldenWcnUart;
/* Single executor owns UART14/pins24..27. Full physical radio/reset/parent
 * qualification precedes writes; FIFO-disabled/GPI/active/nonempty refuse.
 * Lossless 1x8 FIFO packing uses the Linux packing stop-vector semantics.
 * No SMMU, SE firmware, PMIC/RPMh, RF clock or shared PLL programming. */
int elden_wcn_uart_acquire(EldenWcnUart *);
int elden_wcn_uart_baud(EldenWcnUart *,uint32_t baud);
int elden_wcn_uart_flow(EldenWcnUart *,bool ready);
int elden_wcn_uart_write(EldenWcnUart *,const uint8_t *,size_t,unsigned timeout_ms);
int elden_wcn_uart_read(EldenWcnUart *,uint8_t *,size_t,size_t *bytes);
/* Requires the same physical owner to verify radio-stopped. Outstanding RX
 * can then be discarded explicitly; unknown SE state retains every resource. */
int elden_wcn_uart_release(EldenWcnUart *);
#ifdef __cplusplus
}
#endif
