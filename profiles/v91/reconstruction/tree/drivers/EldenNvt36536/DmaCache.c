/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaCache.h"
int elden_dma_cache_span(uintptr_t base,size_t capacity,uintptr_t address,
                         size_t bytes,size_t line,EldenDmaCacheSpan *out){
    if(!out||!base||!bytes||!capacity||line<32||line>4096||(line&(line-1))||
       (base&(line-1))||(capacity&(line-1))||capacity>UINTPTR_MAX-base||
       address<base||address>=base+capacity||bytes>base+capacity-address)return -22;
    uintptr_t end=address+bytes;
    /* Allocation end is aligned and representable, so rounding cannot wrap. */
    if(end&(line-1))end=(end&~(uintptr_t)(line-1))+line;
    EldenDmaCacheSpan next={address&~(uintptr_t)(line-1),end,line};
    *out=next;return 0;
}
