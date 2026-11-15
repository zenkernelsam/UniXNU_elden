/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
struct EldenGpuCallGate {uint32_t closing=0,active=0;};
static inline bool eldenGpuEnter(EldenGpuCallGate *g) {
    if(__atomic_load_n(&g->closing,__ATOMIC_ACQUIRE))return false;
    uint32_t old=__atomic_load_n(&g->active,__ATOMIC_RELAXED);
    do {if(old==UINT32_MAX)return false;} while(!__atomic_compare_exchange_n(&g->active,&old,old+1,false,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED));
    if(!__atomic_load_n(&g->closing,__ATOMIC_ACQUIRE))return true;
    __atomic_fetch_sub(&g->active,1u,__ATOMIC_RELEASE);return false;
}
static inline void eldenGpuLeave(EldenGpuCallGate *g) {__atomic_fetch_sub(&g->active,1u,__ATOMIC_RELEASE);}
static inline void eldenGpuClose(EldenGpuCallGate *g) {__atomic_store_n(&g->closing,1u,__ATOMIC_RELEASE);}
static inline bool eldenGpuClosed(EldenGpuCallGate *g) {return __atomic_load_n(&g->closing,__ATOMIC_ACQUIRE)!=0;}
static inline bool eldenGpuEmpty(EldenGpuCallGate *g) {return __atomic_load_n(&g->active,__ATOMIC_ACQUIRE)==0;}
class EldenGpuCallLease {
    EldenGpuCallGate *gate_;bool entered_;
public:
    explicit EldenGpuCallLease(EldenGpuCallGate *g):gate_(g),entered_(eldenGpuEnter(g)){}
    ~EldenGpuCallLease(){if(entered_)eldenGpuLeave(gate_);}
    explicit operator bool() const {return entered_;}
    EldenGpuCallLease(const EldenGpuCallLease &)=delete;
    EldenGpuCallLease &operator=(const EldenGpuCallLease &)=delete;
};
