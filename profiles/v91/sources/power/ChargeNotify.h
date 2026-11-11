/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool up, pending, registered, quarantined;
    uint32_t epoch, requests, timeouts, reply_bytes, reply_type, reply_word;
} EldenChargeNotify;
/* Stock Lenovo notification registration only: owner32778/type2/op4 and
 * four zero payload words. Never changes current, voltage or input routing.
 * One acknowledged registration per real link; timeout requires a new link.
 */
void elden_charge_notify_init(EldenChargeNotify *state);
void elden_charge_notify_link(EldenChargeNotify *state, bool up);
int elden_charge_notify_next(EldenChargeNotify *state, uint8_t out[28]);
void elden_charge_notify_unsent(EldenChargeNotify *state);
void elden_charge_notify_timeout(EldenChargeNotify *state);
/* 1 = matching ACK, -1 = malformed matching ACK, 0 = unrelated/stale. */
int elden_charge_notify_receive(EldenChargeNotify *state, const uint8_t *data, size_t bytes);
#ifdef __cplusplus
}
#endif
