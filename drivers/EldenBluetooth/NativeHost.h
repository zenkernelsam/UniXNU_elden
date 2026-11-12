/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Wcn7861Startup.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Publish only after actual controller HCI ready and an active power lease.
 * No substitute controller, guessed capability responses or persistent keys. */
int elden_bt_host_publish(const EldenWcnStartupIo *,const uint8_t address[6],
    const PbH4Rx *,const uint8_t *rest,size_t rest_bytes);
/* Runs the single UART executor; returns after guarded withdrawal. */
void elden_bt_host_run(void);
/* 0 unpublished, 1 service/control available, 2 withdrawing or withdrawn. */
unsigned elden_bt_host_state(void);
#ifdef __cplusplus
}
#endif
