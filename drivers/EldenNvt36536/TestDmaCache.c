/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DmaCache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
int main(void){
    const uintptr_t base=UINT64_C(0xffffffe080000000);
    for(size_t line=32;line<=4096;line*=2){
        for(size_t offset=0;offset<2*line;offset++){
            size_t sizes[]={1,line-1,line,line+1,16384-offset};
            for(unsigned i=0;i<5;i++){
                EldenDmaCacheSpan p={0};size_t n=sizes[i];
                CHECK(!elden_dma_cache_span(base,16384,base+offset,n,line,&p));
                CHECK(p.first<=base+offset&&p.end>=base+offset+n);
                CHECK(p.first>=base&&p.end<=base+16384&&p.line==line);
                CHECK(!(p.first&(line-1))&&!(p.end&(line-1)));
                CHECK(base+offset-p.first<line&&p.end-(base+offset+n)<line);
            }
        }
    }
    for(unsigned i=0;i<9;i++){
        uintptr_t b=base,a=base;size_t cap=16384,n=1,line=64;
        if(i==0)++b;
        if(i==1)--cap;
        if(i==2)--a;
        if(i==3)a+=cap;
        if(i==4)n=cap+1;
        if(i==5)n=0;
        if(i==6)line=96;
        if(i==7)b=UINTPTR_MAX-63;
        if(i==8)line=8192;
        EldenDmaCacheSpan p;memset(&p,0xa5,sizeof(p));EldenDmaCacheSpan prior=p;
        CHECK(elden_dma_cache_span(b,cap,a,n,line,&p)==-22);
        CHECK(!memcmp(&p,&prior,sizeof(p)));
    }
    printf("DMA cache spans: %u assertions, exclusive-line bounds and overflow\n",checks);
}
