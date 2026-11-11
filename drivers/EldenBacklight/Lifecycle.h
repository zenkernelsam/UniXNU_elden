/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
typedef struct { volatile unsigned state,stop; } EldenBlLifecycle;
/* state: 0 unclaimed,1 queued/running,2 terminal. Stop may close idle before
 * the first worker is queued; a claimed worker remains owned until cleanup. */
static inline int elden_bl_worker_claim(EldenBlLifecycle *s){
    unsigned expected=0;return __atomic_compare_exchange_n(&s->state,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline void elden_bl_worker_stop(EldenBlLifecycle *s){
    __atomic_store_n(&s->stop,1u,__ATOMIC_RELEASE);unsigned expected=0;
    __atomic_compare_exchange_n(&s->state,&expected,2u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline int elden_bl_worker_stopping(const EldenBlLifecycle *s){return __atomic_load_n(&s->stop,__ATOMIC_ACQUIRE)!=0;}
static inline int elden_bl_worker_done(const EldenBlLifecycle *s){return __atomic_load_n(&s->state,__ATOMIC_ACQUIRE)==2;}
static inline void elden_bl_worker_finish(EldenBlLifecycle *s){__atomic_store_n(&s->state,2u,__ATOMIC_RELEASE);}
