/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Layout.h"
#include "Fence.h"
#include "EldenDataBoard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks;
int main(void){
    EldenDataLayout p;memset(&p,0xa5,sizeof(p));EldenDataLayout saved=p;
    CHECK(elden_data_layout(&p,ELDEN_DATA_FIRST+1,ELDEN_DATA_LAST,ELDEN_DATA_ROOT_BYTES)<0);
    CHECK(!memcmp(&p,&saved,sizeof(p)));
    CHECK(elden_data_layout(&p,ELDEN_DATA_FIRST,ELDEN_DATA_LAST,ELDEN_DATA_ROOT_BYTES)==0&&p.valid);
    CHECK(p.root.first==ELDEN_DATA_FIRST&&p.root.blocks*4096==ELDEN_DATA_ROOT_BYTES);
    CHECK(p.root_guard.blocks>=1024);
    CHECK(!elden_data_overlap(p.root,p.data)&&!elden_data_overlap(p.data,p.log));
    CHECK(p.stage[0].blocks*4096==ELDEN_DATA_STAGE_BYTES&&p.stage[1].blocks==p.stage[0].blocks);
    CHECK(p.data.blocks==p.window_max&&p.logical_max==ELDEN_DATA_LOGICAL_MAX);
    CHECK(p.log.first+p.log.blocks==ELDEN_DATA_LAST+1);
    uint64_t total=p.root.blocks+p.root_guard.blocks+p.metadata.blocks+p.stage[0].blocks+
        p.stage[1].blocks+p.data.blocks+p.spare.blocks+p.log.blocks;
    CHECK(total==p.partition.blocks);
    uint64_t window=0;CHECK(elden_data_window_blocks(536870912,&window)&&window==131077);
    CHECK(window<p.data.blocks);uint64_t first=p.data.first;
    CHECK(elden_data_window_blocks(ELDEN_DATA_LOGICAL_MAX,&window)&&window==p.window_max&&first==p.data.first);
    CHECK(!elden_data_window_blocks(1,&window)&&!elden_data_window_blocks(UINT64_MAX,&window));
    CHECK(elden_data_contains(p.data,p.data.first,p.data.blocks));
    CHECK(!elden_data_contains(p.data,p.data.first-1,1)&&!elden_data_contains(p.data,p.data.first+p.data.blocks,1));
    CHECK(!elden_data_contains(p.data,p.data.first,UINT64_MAX));
    CHECK(elden_data_overlap((EldenDataRange){UINT64_MAX-8,8},(EldenDataRange){UINT64_MAX-4,4}));
    CHECK(!elden_data_overlap((EldenDataRange){UINT64_MAX-8,4},(EldenDataRange){UINT64_MAX-4,4}));
    EldenDataFence f={};CHECK(elden_data_fence_reset_ready(&f));
    CHECK(elden_data_fence_open(&f,1,p.data));CHECK(!elden_data_fence_begin(&f,p.data.first,1));
    CHECK(elden_data_fence_reset_ready(&f)&&elden_data_fence_close(&f));
    CHECK(!elden_data_fence_open(&f,2,p.data));
    memset(&f,0,sizeof(f));CHECK(elden_data_fence_open(&f,2,p.data));
    CHECK(!elden_data_fence_reset_ready(&f));
    CHECK(!elden_data_fence_begin(&f,p.root.first,1));
    CHECK(elden_data_fence_begin(&f,p.data.first,1));CHECK(f.issued==1&&f.inflight==1);
    CHECK(!elden_data_fence_begin(&f,p.data.first+1,1));CHECK(!elden_data_fence_close(&f));
    CHECK(!elden_data_fence_reset_ready(&f));
    elden_data_fence_end(&f,true);CHECK(elden_data_fence_reset_ready(&f));
    CHECK(!elden_data_fence_begin(&f,p.data.first,1));
    memset(&f,0,sizeof(f));CHECK(elden_data_fence_open(&f,2,p.data));
    CHECK(elden_data_fence_begin(&f,p.data.first,1));elden_data_fence_end(&f,false);
    CHECK(f.poisoned&&f.issued==1&&f.durable==0&&!elden_data_fence_close(&f));
    CHECK(!elden_data_fence_begin(&f,p.data.first,1));
    memset(&f,0,sizeof(f));CHECK(elden_data_fence_open(&f,2,p.data));f.issued=f.durable=UINT64_MAX;
    CHECK(!elden_data_fence_begin(&f,p.data.first,1)&&f.poisoned&&!f.inflight);
    printf("PASS %u stable-tail, exclusion, completion/freeze/overflow checks\n",checks);return 0;
}
