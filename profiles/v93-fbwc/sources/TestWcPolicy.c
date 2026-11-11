#include "WcPolicy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,0,0x800000000ULL,0x180000000ULL,1));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,0,0x80000000ULL,0x180000000ULL,1));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,0,0x800000000ULL,0x180000000ULL,3));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,0,0,0,1));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,0,UINT64_MAX-1,2,1));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA+4096,ELDEN_FB_BYTES,1904,3040,7616,32,0,0x800000000ULL,0x180000000ULL,1));
    assert(!elden_fb_wc_layout(ELDEN_FB_PA,ELDEN_FB_BYTES,1904,3040,7616,32,1,0x800000000ULL,0x180000000ULL,1));
    assert(elden_fb_par_check((0x44ULL<<56)|ELDEN_FB_PA,ELDEN_FB_PA)==0);
    assert(elden_fb_par_check(1,ELDEN_FB_PA)==1);
    assert(elden_fb_par_check((0x44ULL<<56)|ELDEN_FB_PA,ELDEN_FB_PA+4096)==2);
    assert(elden_fb_par_check(ELDEN_FB_PA,ELDEN_FB_PA)==3);
    assert(elden_fb_wc_span(0xfffffffa00000000ULL,ELDEN_FB_BYTES));
    assert(!elden_fb_wc_span(UINT64_MAX&~4095ULL,ELDEN_FB_BYTES));
    assert(!elden_fb_wc_span(0,ELDEN_FB_BYTES));
    assert(!elden_fb_wc_span(0xfffffffa00000001ULL,ELDEN_FB_BYTES));
    uint64_t good=(0x44ULL<<56)|ELDEN_FB_PA,last=0;
    assert(elden_fb_par_pair(good,good,ELDEN_FB_PA,&last)==0&&last==good);
    assert(elden_fb_par_pair(1,good,ELDEN_FB_PA,&last)==1&&last==1);
    assert(elden_fb_par_pair(good,1,ELDEN_FB_PA,&last)==9&&last==1);
    assert(elden_fb_par_pair(good,good+4096,ELDEN_FB_PA,&last)==10);
    assert(elden_fb_par_pair(good,ELDEN_FB_PA,ELDEN_FB_PA,&last)==11);
    puts("PASS exact layout, alias exclusion, VA span overflow, and independent read/write PAR permission/PA/attribute checks");
}
