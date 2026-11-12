#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "NativeLegacyRequest.h"
#include "CopyPairs.h"
static uint32_t randomWord=1;
static unsigned nextByte(void){randomWord=randomWord*1664525U+1013904223U;return randomWord>>24;}
int main(void)
{
    unsigned tested=0;
    for(unsigned width=1;width<=67;width++)for(unsigned height=1;height<=7;height++){
        uint64_t ds=((width*4+15)&~15U)+16,ss=ds+16,n=ds*height,m=ss*height;
        unsigned char *dst=aligned_alloc(16,n+64),*src=aligned_alloc(16,m+64),*ref=malloc(n+64);
        assert(dst&&src&&ref);memset(dst,0xa5,n+64);memset(ref,0xa5,n+64);
        for(uint64_t k=0;k<m+64;k++)src[k]=(unsigned char)nextByte();
        assert(elden_fb_copy_pairs(0,dst,n,ds,src,m,ss,width,height)==-1&&!memcmp(dst,ref,n+64));
        assert(elden_fb_copy_pairs(1,dst+4,n,ds,src,m,ss,width,height)==-1&&!memcmp(dst,ref,n+64));
        assert(elden_fb_copy_pairs(1,dst,n,ds,src+1,m,ss,width,height)==-1&&!memcmp(dst,ref,n+64));
        assert(elden_fb_copy_pairs(1,dst,n-1,ds,src,m,ss,width,height)==0&&!memcmp(dst,ref,n+64));
        assert(elden_fb_copy_pairs(1,dst,n,ds,src,m-1,ss,width,height)==0&&!memcmp(dst,ref,n+64));
        for(unsigned row=0;row<height;row++)memcpy(ref+row*ds,src+row*ss,width*4);
        assert(elden_fb_copy_pairs(1,dst,n,ds,src,m,ss,width,height)==1&&!memcmp(dst,ref,n+64));
        assert(elden_fb_copy_pairs(1,dst,n,ds,dst,n,ds,width,height)==-1);
        memset(dst,0xa5,n+64);memset(ref,0xa5,n+64);
        for(unsigned row=0;row<height;row++)memcpy(ref+4+row*ds,src+1+row*ss,width*4);
        assert(elden_fb_copy_pairs(1,dst+4,n,ds,src+1,m,ss,width,height)==-1);
        assert(native_legacy_copy_pixels(dst+4,n,ds,src+1,m,ss,width,height)==1&&!memcmp(dst,ref,n+64));
        free(dst);free(src);free(ref);tested++;
    }
    assert(elden_fb_copy_pairs(1,(void *)(UINTPTR_MAX-15),64,16,(void *)16,64,16,4,4)==0);
    printf("PASS %u padded rectangles, tails, eligibility, overlap and bounds; hardware speed untested\n",tested);
}
