/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Wcn7861Startup.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Internal single-executor continuation, not a second UART owner. The write
 * callback returns zero only after the whole packet has physically drained;
 * it must honor CTS and its deadline. A queued/partial write is insufficient.
 * The owning physical reset is the only way to clear an uncertain session. */
int elden_wcn_wire_fail(EldenWcnStartup *,int);
int elden_wcn_wire_send(EldenWcnStartup *,const uint8_t *,size_t);
int elden_wcn_wire_frame(EldenWcnStartup *);
#ifdef __cplusplus
}
#endif
