/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { EldenSpmiAdcOwner=1, EldenSpmiBtOwner=2 };
typedef void (*EldenSpmiDelay)(void *,unsigned);
/* One bus0/EE0 gate shared by ADC and WCN. Called only from their worker
 * threads. The transaction must not sleep or log while holding the gate.
 * Timeout poisons the bus gate; no later normal client may issue a command.
 * The established recovery route remains independent after stopping clients. */
int elden_spmi_transaction_acquire(unsigned owner,void *context,EldenSpmiDelay delay);
int elden_spmi_transaction_release(unsigned owner,bool uncertain);
uint32_t elden_spmi_transaction_state(void);
#ifdef __cplusplus
}
#endif
