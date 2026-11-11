/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Command.h"
#include "Gpt.h"
#include "EldenDataBoard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"%d %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t *file(const char *name,size_t *bytes){
    FILE *f=fopen(name,"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *p=malloc((size_t)n);CHECK(p!=NULL);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return p;
}
int main(int argc,char **argv){
    CHECK(argc==3);size_t pn,bn;uint8_t *p=file(argv[1],&pn),*b=file(argv[2],&bn);CHECK(pn==24576&&bn==20480);
    uint64_t last=0;for(unsigned i=0;i<8;i++)last|=(uint64_t)p[4096+32+i]<<(8*i);
    EldenDataGptHeader ph,bh;
    CHECK(!elden_data_gpt_header(p+4096,4096,last,false,&ph));
    CHECK(!elden_data_gpt_header(b+bn-4096,4096,last,true,&bh));
    const uint8_t *pa=p+ph.entries*4096;const uint8_t *ba=b+(bh.entries-(last-(bn/4096-1)))*4096;
    CHECK(!elden_data_gpt_pair(&ph,pa,ph.count*128,&bh,ba,bh.count*128));
    p[4096+56]^=1;CHECK(elden_data_gpt_header(p+4096,4096,last,false,&ph)<0);p[4096+56]^=1;
    CHECK(!elden_data_gpt_header(p+4096,4096,last,false,&ph));
    EldenDataGptHeader wrong=bh;wrong.first++;CHECK(elden_data_gpt_pair(&ph,pa,ph.count*128,&wrong,ba,bh.count*128)<0);
    CHECK(elden_data_gpt_pair(&ph,pa,1,&bh,ba,bh.count*128)<0);
    free(p);free(b);
    EldenDataLayout layout;CHECK(!elden_data_layout(&layout,ELDEN_DATA_FIRST,ELDEN_DATA_LAST,ELDEN_DATA_ROOT_BYTES));
    EldenDataCommand c={};CHECK(elden_data_fence_open(&c.fence,2,layout.data));
    CHECK(!elden_data_command_write(&c,layout.root.first,1));
    CHECK(!elden_data_command_write(&c,layout.data.first,33));
    CHECK(elden_data_command_write(&c,layout.data.first,2));
    CHECK(elden_data_command_allowed(&c,0,c.cdb,10,1,8192));
    CHECK(!elden_data_command_allowed(&c,4,c.cdb,10,1,8192));
    CHECK(!elden_data_command_allowed(&c,0,c.cdb,10,0,8192));
    CHECK(!elden_data_command_allowed(&c,0,c.cdb,10,1,4096));
    uint8_t altered[10];
    for(unsigned i=0;i<10;i++){
        memcpy(altered,c.cdb,10);altered[i]^=1;CHECK(!elden_data_command_allowed(&c,0,altered,10,1,8192));
    }
    CHECK(!elden_data_command_write(&c,layout.data.first+4,1));
    CHECK(elden_data_command_sync(&c,true));CHECK(elden_data_command_allowed(&c,0,c.cdb,10,0,0));
    elden_data_command_end(&c,true);CHECK(c.fence.issued==1&&c.fence.durable==1&&!c.fence.poisoned);
    CHECK(elden_data_fence_close(&c.fence));
    CHECK(elden_data_command_sync(&c,false));CHECK(!elden_data_fence_reset_ready(&c.fence));
    CHECK(elden_data_command_allowed(&c,0,c.cdb,10,0,0));elden_data_command_end(&c,true);
    CHECK(elden_data_fence_reset_ready(&c.fence));
    memset(&c,0,sizeof(c));CHECK(elden_data_fence_open(&c.fence,2,layout.data));
    CHECK(elden_data_command_write(&c,layout.data.first,1));elden_data_command_end(&c,true);
    CHECK(c.fence.poisoned&&!c.fence.durable); // A write completion without sync is insufficient.
    CHECK(!elden_data_command_sync(&c,false));
    memset(&c,0,sizeof(c));CHECK(elden_data_fence_open(&c.fence,2,layout.data));
    CHECK(elden_data_command_write(&c,layout.data.first,1));CHECK(!elden_data_fence_close(&c.fence));
    CHECK(!elden_data_command_allowed(&c,0,c.cdb,10,1,4096));elden_data_command_end(&c,false);
    CHECK(!elden_data_fence_reset_ready(&c.fence));
    printf("PASS %u actual GPT pair and scoped WRITE/SYNC/completion checks\n",checks);return 0;
}
