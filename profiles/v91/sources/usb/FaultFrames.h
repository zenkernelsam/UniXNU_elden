/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
#define ELDEN_FAULT_FRAMES 16
typedef int (*EldenFaultReadPair)(void *,uint64_t,uint64_t *);
typedef struct { uint64_t addresses[ELDEN_FAULT_FRAMES]; unsigned count,reason; int read_error; } EldenFaultFrames;
static inline void elden_fault_frames(EldenFaultFrames *out,uint64_t fp,EldenFaultReadPair read,void *arg)
{
    out->count=0;out->reason=0;out->read_error=0;
    for(unsigned i=0;i<ELDEN_FAULT_FRAMES&&fp;i++){
        if(fp<4096||fp>=(1ULL<<39)-16||(fp&15)){out->reason=1;return;}
        uint64_t pair[2];int error=read(arg,fp,pair);
        if(error){out->reason=2;out->read_error=error;return;}
        if(pair[1]&&(pair[1]<4096||pair[1]>=(1ULL<<39)||(pair[1]&3))){out->reason=3;return;}
        if(pair[1])out->addresses[out->count++]=pair[1];
        if(!pair[0])return;
        if(pair[0]<=fp||pair[0]-fp>1048576){out->reason=4;return;}
        fp=pair[0];
    }
    if(fp)out->reason=5; // bounded truncation, not a complete backtrace
}
