/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Brightness.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    EldenBlFade f={0};uint64_t r=elden_bl_request(INT64_MAX,UINT64_MAX);
    assert((r&65535)==4095&&((r>>16)&65535)==10000&&(r>>63)==1);
    assert((elden_bl_request(-1,0)&65535)==0);
    assert((elden_bl_request(INT32_MIN,0)&65535)==0);
    assert((elden_bl_request((int32_t)UINT32_MAX,0)&65535)==0);
    assert((elden_bl_request(INT32_MAX,0)&65535)==4095);
    elden_bl_fade_begin(&f,elden_bl_request(1000,65536),0,100);
    assert(f.ms==1000&&elden_bl_fade_level(&f,100)==0);
    assert(elden_bl_fade_level(&f,600)==500&&elden_bl_fade_level(&f,1100)==1000);
    /* Supersede a rising fade using the last value really written. */
    elden_bl_fade_begin(&f,elden_bl_request(0,32768),500,600);
    assert(f.ms==500&&elden_bl_fade_level(&f,850)==250&&elden_bl_fade_level(&f,1100)==0);
    assert(elden_bl_fade_level(&f,599)==500);
    elden_bl_fade_begin(&f,elden_bl_request(4095,0),0,1000);
    assert(elden_bl_fade_level(&f,1000)==4095);
    puts("CoreBrightness 16.16 fade, bounds and replacement checks passed");
}
