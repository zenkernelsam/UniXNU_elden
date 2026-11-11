/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PdContract.h"
#include <string.h>
int elden_pd_contract(const uint32_t *pdos,unsigned count,uint32_t rdo,EldenPdContract *out){
    if(!pdos||!out||!count||count>7)return -22;
    unsigned position=(rdo>>28)&7;if(!position||position>count)return -34;
    uint32_t pdo=pdos[position-1];if(!pdo)return -34;
    EldenPdContract c;memset(&c,0,sizeof(c));c.position=position;c.type=pdo>>30;
    c.give_back=(rdo&(1u<<27))!=0;c.capability_mismatch=(rdo&(1u<<26))!=0;
    if(c.type==0){
        c.min_mv=c.max_mv=((pdo>>10)&1023)*50;c.source_ma=(pdo&1023)*10;
        c.requested_mv=c.min_mv;c.exact_voltage=true;c.requested_ma=((rdo>>10)&1023)*10;
    }else if(c.type==1||c.type==2){
        c.min_mv=((pdo>>10)&1023)*50;c.max_mv=((pdo>>20)&1023)*50;
        if(c.type==1){c.source_mw=(pdo&1023)*250;c.requested_mw=((rdo>>10)&1023)*250;}
        else{c.source_ma=(pdo&1023)*10;c.requested_ma=((rdo>>10)&1023)*10;}
    }else{
        if(((pdo>>28)&3)!=0)return -95; /* AVS/EPR has a different contract. */
        c.min_mv=((pdo>>8)&255)*100;c.max_mv=((pdo>>17)&255)*100;c.source_ma=(pdo&127)*50;
        c.requested_mv=((rdo>>9)&2047)*20;c.requested_ma=(rdo&127)*50;c.exact_voltage=true;
        if(c.requested_mv<c.min_mv||c.requested_mv>c.max_mv)return -34;
    }
    if(!c.min_mv||c.max_mv<c.min_mv)return -34;
    if(c.type==1){if(!c.requested_mw||c.requested_mw>c.source_mw)return -34;}
    else if(!c.requested_ma||c.requested_ma>c.source_ma)return -34;
    if(c.exact_voltage)c.requested_mw=(unsigned)((uint64_t)c.requested_mv*c.requested_ma/1000);
    *out=c;return 0;
}
