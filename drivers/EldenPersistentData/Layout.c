/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Layout.h"
#include "EldenDataBoard.h"
#include <string.h>
bool elden_data_contains(EldenDataRange r,uint64_t first,uint64_t blocks){
    return blocks&&r.blocks&&first>=r.first&&first-r.first<r.blocks&&blocks<=r.blocks-(first-r.first);
}
bool elden_data_overlap(EldenDataRange a,EldenDataRange b){
    if(!a.blocks||!b.blocks)return false;
    /* Difference comparisons avoid overflow of exclusive end addresses. */
    return a.first<=b.first?b.first-a.first<a.blocks:a.first-b.first<b.blocks;
}
bool elden_data_window_blocks(uint64_t bytes,uint64_t *blocks){
    if(!blocks||!bytes||(bytes&4095)||bytes>ELDEN_DATA_LOGICAL_MAX)return false;
    uint64_t pages=bytes/4096,bitmap=(pages+32767)/32768;
    *blocks=pages+bitmap+1;return true;
}
int elden_data_layout(EldenDataLayout *out,uint64_t first,uint64_t last,uint64_t root_bytes){
    if(!out)return EldenDataLayoutArgument;
    if(first!=ELDEN_DATA_FIRST||last!=ELDEN_DATA_LAST||root_bytes!=ELDEN_DATA_ROOT_BYTES||
       last>UINT32_MAX||first>last||root_bytes%4096)return EldenDataLayoutGeometry;
    EldenDataLayout p;memset(&p,0,sizeof(p));p.partition=(EldenDataRange){first,last-first+1};
    p.root=(EldenDataRange){first,root_bytes/4096};
    uint64_t cursor=(first+p.root.blocks+1024+511)&~UINT64_C(511);
    p.root_guard=(EldenDataRange){first+p.root.blocks,cursor-(first+p.root.blocks)};
    p.metadata=(EldenDataRange){cursor,512};cursor+=512;
    for(unsigned i=0;i<2;i++){p.stage[i]=(EldenDataRange){cursor,ELDEN_DATA_STAGE_BYTES/4096};cursor+=p.stage[i].blocks;}
    uint64_t window=0;if(!elden_data_window_blocks(ELDEN_DATA_LOGICAL_MAX,&window))return EldenDataLayoutGeometry;
    p.logical_max=ELDEN_DATA_LOGICAL_MAX;p.window_max=window;
    p.data=(EldenDataRange){cursor,window};cursor+=window;
    p.log=(EldenDataRange){last+1-ELDEN_DATA_LOG_BYTES/4096,ELDEN_DATA_LOG_BYTES/4096};
    if(cursor>p.log.first||p.root.blocks+1024>p.partition.blocks)return EldenDataLayoutGeometry;
    p.spare=(EldenDataRange){cursor,p.log.first-cursor};
    EldenDataRange ranges[]={p.root,p.root_guard,p.metadata,p.stage[0],p.stage[1],p.data,p.spare,p.log};
    for(unsigned i=0;i<sizeof(ranges)/sizeof(ranges[0]);i++){
        if(ranges[i].blocks&&!elden_data_contains(p.partition,ranges[i].first,ranges[i].blocks))return EldenDataLayoutGeometry;
        for(unsigned j=0;j<i;j++)if(elden_data_overlap(ranges[i],ranges[j]))return EldenDataLayoutGeometry;
    }
    p.valid=true;*out=p;return 0;
}
