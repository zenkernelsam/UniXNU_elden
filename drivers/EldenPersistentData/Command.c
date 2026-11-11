/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Command.h"
#include <string.h>
bool elden_data_command_write(EldenDataCommand *p,uint64_t first,uint32_t blocks){
    if(!p||!blocks||blocks>32||!elden_data_fence_begin(&p->fence,first,blocks))return false;
    uint8_t cdb[10]={0x2a,8,(uint8_t)(first>>24),(uint8_t)(first>>16),(uint8_t)(first>>8),(uint8_t)first,
        0,(uint8_t)(blocks>>8),(uint8_t)blocks,0};
    memcpy(p->cdb,cdb,10);p->bytes=blocks*4096;__atomic_store_n(&p->phase,1,__ATOMIC_RELEASE);return true;
}
bool elden_data_command_sync(EldenDataCommand *p,bool after_write){
    if(!p||__atomic_load_n(&p->fence.mode,__ATOMIC_ACQUIRE)!=2||__atomic_load_n(&p->fence.poisoned,__ATOMIC_ACQUIRE))return false;
    if(after_write){if(__atomic_load_n(&p->phase,__ATOMIC_ACQUIRE)!=1||!__atomic_load_n(&p->fence.inflight,__ATOMIC_ACQUIRE))return false;}
    else{
        uint32_t zero=0;if(!__atomic_compare_exchange_n(&p->fence.inflight,&zero,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
    }
    memset(p->cdb,0,10);p->cdb[0]=0x35;p->bytes=0;__atomic_store_n(&p->phase,2,__ATOMIC_RELEASE);return true;
}
bool elden_data_command_allowed(const EldenDataCommand *p,uint8_t lun,const uint8_t *cdb,unsigned size,unsigned direction,uint32_t bytes){
    if(!p||!cdb||lun||size!=10||__atomic_load_n(&p->fence.mode,__ATOMIC_ACQUIRE)!=2||
       __atomic_load_n(&p->fence.poisoned,__ATOMIC_ACQUIRE)||!__atomic_load_n(&p->fence.inflight,__ATOMIC_ACQUIRE))return false;
    unsigned phase=__atomic_load_n(&p->phase,__ATOMIC_ACQUIRE);
    return (phase==1||phase==2)&&direction==(phase==1?1u:0u)&&bytes==p->bytes&&!memcmp(cdb,p->cdb,10)&&
        (phase!=1||!__atomic_load_n(&p->fence.closed,__ATOMIC_ACQUIRE));
}
void elden_data_command_end(EldenDataCommand *p,bool verified){
    if(!p)return;
    bool synced=__atomic_load_n(&p->phase,__ATOMIC_ACQUIRE)==2;
    __atomic_store_n(&p->phase,0,__ATOMIC_RELEASE);elden_data_fence_end(&p->fence,verified&&synced);
}
