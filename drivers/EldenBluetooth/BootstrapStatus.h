/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stddef.h>
#include <stdint.h>
enum EldenBtBootState {EldenBtBootUnavailable,EldenBtBootStarting,EldenBtBootReady,
    EldenBtBootStopped,EldenBtBootFailed,EldenBtBootStopping};
typedef struct {
    uint32_t version,state;
    uint64_t epoch;
    uint16_t manufacturer,patch;
    uint32_t reserved;
    uint8_t address[6]; /* Network/display order, from verified Read_BD_ADDR. */
    uint16_t reserved2;
} EldenBtBootStatus;
#ifdef __cplusplus
static_assert(sizeof(EldenBtBootStatus)==32,"Bootstrap ABI");
extern "C" {
#else
_Static_assert(sizeof(EldenBtBootStatus)==32,"Bootstrap ABI");
#endif
/* Single writer: startup caller before worker creation, then that worker.
 * The stop requester never publishes hardware completion. */
void elden_bt_bootstrap_publish(unsigned state,uint64_t epoch,uint16_t manufacturer,uint16_t patch,const uint8_t address[6]);
#ifdef __cplusplus
}
#endif
