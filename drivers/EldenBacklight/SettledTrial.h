/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Aw99706.h"
/* Experimental admission only: exact v44 startup signature, followed by the
 * complete unchanged/clear observation window. This is not read-clear proof
 * and is never used to recover the normal brightness service after a fault. */
typedef struct { bool valid; unsigned samples; } EldenBlSettledTrial;
static inline void elden_bl_trial_begin(EldenBlSettledTrial *t,const EldenAw99706 *s){
    t->samples=0;t->valid=s->failed&&!s->ready&&s->error==EldenBlFault&&s->writes==0&&
        s->observed_id==7&&s->observed_mode==0x65&&s->observed_hi==6&&s->observed_lo==0x3d&&
        s->observed_config==0xf8&&s->observed_open==0x3c&&s->observed_short==0&&s->observed_flags==8&&s->observed_cfg7==5;
}
static inline void elden_bl_trial_sample(EldenBlSettledTrial *t,unsigned ms,int rc,
        unsigned open,unsigned shorted,unsigned flags,unsigned config,unsigned hi,unsigned lo){
    const unsigned times[]={100,1000,2000,4000,8000};
    if(!t->valid)return;
    if(t->samples>=5||ms!=times[t->samples]||rc||open||shorted||flags||config!=0xf8||hi!=6||lo!=0x3d){t->valid=false;return;}
    ++t->samples;
}
static inline bool elden_bl_trial_admitted(const EldenBlSettledTrial *t){return t->valid&&t->samples==5;}
