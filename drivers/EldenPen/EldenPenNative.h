/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_PEN_NATIVE_H
#define ELDEN_PEN_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All calls on the existing touch sampling worker, never an interrupt context.
 * start is opt-in via elden-pen=1; no register or firmware writes are added.
 * frame_owner is the caller's real serialized sampling-owner identity — the
 * live worker Context — declared at start and rechecked on every packet by
 * the guarded owner path. It fences foreign/stale frames; it is never an
 * invented or stale token. */
int elden_pen_native_start(uintptr_t frame_owner);
int elden_pen_native_packet(const uint8_t *,size_t,uintptr_t frame_owner);
int elden_pen_native_fault(void);
int elden_pen_native_stop(void);
/* Original touch worker only. Reversible actual release keeps the same pen
 * service and clock; resume refuses an unacknowledged lift or terminal stop. */
int elden_pen_native_pause(void);
int elden_pen_native_resume(void);
#ifdef __cplusplus
}
#endif
#endif
