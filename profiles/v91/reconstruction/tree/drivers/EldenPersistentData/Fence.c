/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Fence.h"
#include <string.h>
bool elden_data_fence_open(EldenDataFence *f,unsigned mode,EldenDataRange window){
    if(!f||mode<1||mode>2||!window.blocks||window.first>UINT32_MAX||
       window.blocks-1>UINT32_MAX-window.first)return false;
    /* A failed or closed owner cannot be reopened in the same boot. */
    if(__atomic_load_n(&f->mode,__ATOMIC_ACQUIRE)||__atomic_load_n(&f->poisoned,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&f->closed,__ATOMIC_ACQUIRE))return false;
    f->window=window;__atomic_store_n(&f->mode,mode,__ATOMIC_RELEASE);return true;
}
bool elden_data_fence_begin(EldenDataFence *f,uint64_t first,uint32_t blocks){
    if(!f||__atomic_load_n(&f->mode,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&f->closed,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&f->poisoned,__ATOMIC_ACQUIRE)||!elden_data_contains(f->window,first,blocks))return false;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&f->inflight,&zero,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
    /* A close racing this reservation wins before any command is issued. */
    if(__atomic_load_n(&f->closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&f->poisoned,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&f->inflight,0,__ATOMIC_RELEASE);return false;
    }
    if(__atomic_load_n(&f->issued,__ATOMIC_ACQUIRE)==UINT64_MAX){
        elden_data_fence_poison(f);__atomic_store_n(&f->inflight,0,__ATOMIC_RELEASE);return false;
    }
    __atomic_add_fetch(&f->issued,1,__ATOMIC_ACQ_REL);return true;
}
void elden_data_fence_poison(EldenDataFence *f){if(f)__atomic_store_n(&f->poisoned,1,__ATOMIC_RELEASE);}
void elden_data_fence_end(EldenDataFence *f,bool verified){
    if(!f||!__atomic_load_n(&f->inflight,__ATOMIC_ACQUIRE))return;
    if(verified)__atomic_store_n(&f->durable,__atomic_load_n(&f->issued,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
    else elden_data_fence_poison(f);
    __atomic_store_n(&f->inflight,0,__ATOMIC_RELEASE);
}
bool elden_data_fence_reset_ready(const EldenDataFence *f){
    if(!f)return false;
    unsigned mode=__atomic_load_n(&f->mode,__ATOMIC_ACQUIRE);
    if(!mode||mode==1)return true;
    return __atomic_load_n(&f->closed,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&f->poisoned,__ATOMIC_ACQUIRE)&&
        !__atomic_load_n(&f->inflight,__ATOMIC_ACQUIRE)&&
        __atomic_load_n(&f->issued,__ATOMIC_ACQUIRE)==__atomic_load_n(&f->durable,__ATOMIC_ACQUIRE);
}
bool elden_data_fence_close(EldenDataFence *f){
    if(!f)return false;
    __atomic_store_n(&f->closed,1,__ATOMIC_RELEASE);return elden_data_fence_reset_ready(f);
}
