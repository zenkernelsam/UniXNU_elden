/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "TransactionGate.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
static unsigned active,transactions;
static void pause_one(void *p,unsigned us){(void)p;assert(us==1);sched_yield();}
static void *worker(void *arg){
    unsigned owner=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<10000;i++){
        int rc=elden_spmi_transaction_acquire(owner,0,pause_one);
        if(rc==-16){i--;continue;}assert(rc==0);
        assert(__atomic_fetch_add(&active,1,__ATOMIC_RELAXED)==0);
        if(i%17==0)sched_yield();
        assert(__atomic_fetch_sub(&active,1,__ATOMIC_RELAXED)==1);
        __atomic_fetch_add(&transactions,1,__ATOMIC_RELAXED);
        assert(!elden_spmi_transaction_release(owner,false));
    }
    return 0;
}
int main(void){
    assert(elden_spmi_transaction_acquire(0,0,pause_one)==-22);
    assert(!elden_spmi_transaction_acquire(1,0,pause_one));
    assert(elden_spmi_transaction_release(2,false)==-1);
    assert(elden_spmi_transaction_acquire(2,0,pause_one)==-16);
    assert(elden_spmi_transaction_state()==1);assert(!elden_spmi_transaction_release(1,false));
    pthread_t a,b;assert(!pthread_create(&a,0,worker,(void *)(uintptr_t)1));assert(!pthread_create(&b,0,worker,(void *)(uintptr_t)2));
    assert(!pthread_join(a,0)&&!pthread_join(b,0));assert(transactions==20000&&!active);
    assert(!elden_spmi_transaction_acquire(2,0,pause_one));assert(!elden_spmi_transaction_release(2,true));
    assert(elden_spmi_transaction_acquire(1,0,pause_one)==-5);
    assert(elden_spmi_transaction_release(2,false)==-1);
    assert(elden_spmi_transaction_state()==UINT32_MAX);
    puts("PASS 20000 serialized ADC/BT transactions, contention, wrong-owner and poison checks");
}
