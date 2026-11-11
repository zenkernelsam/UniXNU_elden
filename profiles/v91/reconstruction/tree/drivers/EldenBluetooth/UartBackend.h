/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Wcn7861Startup.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Kernel-only API. The real shared WCN owner must grant an epoch-bound power/
 * BT_EN reset/parent-clock lease; a boot argument or live ADSP is insufficient.
 * The default providers refuse, and no boot worker calls this automatically. */
int elden_wcn_uart_backend_open(uint64_t epoch,EldenWcnStartupIo *out,void **handle);
int elden_wcn_uart_backend_close(void *handle);
int elden_wcn_power_lease_check(uint64_t epoch,unsigned operation);
uint32_t elden_wcn_power_parent_hz(uint64_t epoch,unsigned source);
#ifdef __cplusplus
}
#endif
