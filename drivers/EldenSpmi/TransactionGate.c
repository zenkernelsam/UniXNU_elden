/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "TransactionGate.h"
#include <limits.h>
static uint32_t gate;
int elden_spmi_transaction_acquire(unsigned owner,void *context,EldenSpmiDelay delay){
    if((owner!=EldenSpmiAdcOwner&&owner!=EldenSpmiBtOwner)||!delay)return -22;
    for(unsigned i=0;i<20000;i++){
        uint32_t empty=0;
        if(__atomic_compare_exchange_n(&gate,&empty,owner,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
        if(empty==UINT32_MAX)return -5;
        delay(context,1);
    }
    return -16; /* No command was issued and another owner's gate is untouched. */
}
int elden_spmi_transaction_release(unsigned owner,bool uncertain){
    if(owner!=EldenSpmiAdcOwner&&owner!=EldenSpmiBtOwner)return -22;
    uint32_t expected=owner;
    return __atomic_compare_exchange_n(&gate,&expected,uncertain?UINT32_MAX:0u,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)?0:-1;
}
uint32_t elden_spmi_transaction_state(void){return __atomic_load_n(&gate,__ATOMIC_ACQUIRE);}
