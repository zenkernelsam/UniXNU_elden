// SPDX-License-Identifier: BSD-2-Clause-Patent
// One shutdown owner. A local deadline may supersede an uncommitted USB ACK.
#pragma once
static inline bool elden_reboot_request(volatile unsigned *state) {
    unsigned expected=0;
    return __atomic_compare_exchange_n(state,&expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline bool elden_reboot_ack_commit(volatile unsigned *state) {
    unsigned expected=1;
    return __atomic_compare_exchange_n(state,&expected,2u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline bool elden_reboot_ack_cancel(volatile unsigned *state) {
    unsigned expected=1;
    return __atomic_compare_exchange_n(state,&expected,0u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline bool elden_reboot_deadline_commit(volatile unsigned *state) {
    unsigned expected=__atomic_load_n(state,__ATOMIC_ACQUIRE);
    while(expected<2) {
        if(__atomic_compare_exchange_n(state,&expected,2u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return true;
    }
    return false;
}
