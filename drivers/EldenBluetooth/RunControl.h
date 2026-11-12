/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
/* Same single-worker lifecycle as the qualified touch stop path. A request
 * never certifies radio reset or UART release; only the worker can finish. */
enum EldenWcnRunState {
    EldenWcnIdle,EldenWcnQueued,EldenWcnRunning,EldenWcnStopping,EldenWcnStopped,EldenWcnHeld
};
typedef struct {unsigned state;} EldenWcnRunControl;
static inline unsigned elden_wcn_run_state(EldenWcnRunControl *c){
    return __atomic_load_n(&c->state,__ATOMIC_ACQUIRE);
}
static inline bool elden_wcn_run_live(EldenWcnRunControl *c){
    return elden_wcn_run_state(c)==EldenWcnRunning;
}
static inline bool elden_wcn_run_queue(EldenWcnRunControl *c){
    unsigned old=EldenWcnIdle;
    return __atomic_compare_exchange_n(&c->state,&old,EldenWcnQueued,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline bool elden_wcn_run_enter(EldenWcnRunControl *c){
    unsigned old=EldenWcnQueued;
    return __atomic_compare_exchange_n(&c->state,&old,EldenWcnRunning,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline unsigned elden_wcn_run_stop(EldenWcnRunControl *c){
    for(;;){
        unsigned old=elden_wcn_run_state(c);
        if(old>=EldenWcnStopping)return old;
        unsigned next=old==EldenWcnIdle?EldenWcnStopped:EldenWcnStopping;
        if(__atomic_compare_exchange_n(&c->state,&old,next,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return next;
    }
}
static inline void elden_wcn_run_complete(EldenWcnRunControl *c,bool retained){
    __atomic_store_n(&c->state,retained?EldenWcnHeld:EldenWcnStopped,__ATOMIC_RELEASE);
}
#ifdef __cplusplus
extern "C" {
#endif
/* Native adapter APIs. The stop requester never touches hardware itself. */
int elden_wcn_stop_requested(void);
int elden_wcn_request_stop(void);
#ifdef __cplusplus
}
#endif
