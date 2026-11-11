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
    puts("PASS exact layout, physmap exclusion, console alias refusal, overflow, PAR fault/PA/attribute checks");
}
