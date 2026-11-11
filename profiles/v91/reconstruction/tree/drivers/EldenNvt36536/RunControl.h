/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
enum EldenTouchRunState {EldenTouchIdle,EldenTouchQueued,EldenTouchRunning,EldenTouchStopping,EldenTouchStopped,EldenTouchHeld};
typedef struct {unsigned state;} EldenTouchRunControl;
static inline unsigned elden_touch_run_state(EldenTouchRunControl *c){return __atomic_load_n(&c->state,__ATOMIC_ACQUIRE);}
static inline bool elden_touch_run_queue(EldenTouchRunControl *c){
    unsigned expected=EldenTouchIdle;
    return __atomic_compare_exchange_n(&c->state,&expected,EldenTouchQueued,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline bool elden_touch_run_enter(EldenTouchRunControl *c){
    unsigned expected=EldenTouchQueued;
    return __atomic_compare_exchange_n(&c->state,&expected,EldenTouchRunning,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline unsigned elden_touch_run_request_stop(EldenTouchRunControl *c){
    for(;;){
        unsigned old=elden_touch_run_state(c);
        if(old>=EldenTouchStopping)return old;
        unsigned next=old==EldenTouchIdle?EldenTouchStopped:EldenTouchStopping;
        if(__atomic_compare_exchange_n(&c->state,&old,next,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return next;
    }
}
/* Only the queued/active worker (or its failed-creation path) completes it,
 * after resource cleanup. A stop request alone never certifies cleanup. */
static inline void elden_touch_run_complete(EldenTouchRunControl *c,bool retained){
    __atomic_store_n(&c->state,retained?EldenTouchHeld:EldenTouchStopped,__ATOMIC_RELEASE);
}
