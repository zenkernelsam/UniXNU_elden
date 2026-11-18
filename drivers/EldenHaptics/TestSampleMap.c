/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "SampleMap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    const EldenHapticsGainPoint curve[]={{0,0},{16384,8192},{32767,16384}};
    EldenHapticsCalibration c={curve,3,16384,24000,100,UINT64_C(1)<<36};
    const int16_t input[]={0,32767,-32768,16384,-16384};int8_t output[5]={1,2,3,4,5};uint64_t cost=123;
    assert(!elden_haptics_samples(input,5,32767,&c,output,&cost));
    assert(output[0]==0&&output[1]==64&&output[2]==-64&&output[3]==32&&output[4]==-32&&cost);
    assert(!elden_haptics_samples(input,5,0,&c,output,&cost));for(unsigned i=0;i<5;++i)assert(output[i]==0);assert(cost==0);
    memset(output,0x5a,5);cost=123;c.square_budget_q30=1;
    assert(elden_haptics_samples(input,5,32767,&c,output,&cost)==-75&&cost==123);for(unsigned i=0;i<5;++i)assert(output[i]==0x5a);
    c.square_budget_q30=UINT64_C(1)<<36;c.drive_peak_q15=0;
    assert(elden_haptics_samples(input,5,32767,&c,output,&cost)==-22);
    c.drive_peak_q15=16384;c.rate_hz=8000;assert(elden_haptics_samples(input,5,32767,&c,output,&cost)==-22);
    c.rate_hz=24000;
    for(unsigned i=0;i<65536;++i){
        int16_t sample=(int16_t)((int)i-32768);int8_t value;uint64_t squared;
        assert(!elden_haptics_samples(&sample,1,32767,&c,&value,&squared));
        assert(value>=-64&&value<=64);assert(sample==0?value==0:sample<0?value<=0:value>=0);
    }
    puts("PASS: 65536 signed sample conversions, measured-LUT peer/zero gain, atomic failure, accounting bounds; no electrical calibration claim");
}
