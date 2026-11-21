/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "SampleMap.h"
int elden_haptics_calibration_check(const EldenHapticsCalibration *c){
    if(!c||!c->points||c->count<2||c->count>256||!c->drive_peak_q15||c->drive_peak_q15>32767||
       (c->rate_hz!=12000&&c->rate_hz!=24000&&c->rate_hz!=48000)||!c->max_frames||c->max_frames>48000||!c->square_budget_q30)return -22;
    if(c->points[0].magnitude_q15||c->points[0].drive_q15||c->points[c->count-1].magnitude_q15!=32767)return -22;
    for(unsigned i=0;i<c->count;++i){
        if(c->points[i].drive_q15>c->drive_peak_q15||c->points[i].magnitude_q15>32767)return -22;
        if(i&&(c->points[i-1].magnitude_q15>=c->points[i].magnitude_q15||c->points[i-1].drive_q15>c->points[i].drive_q15))return -22;
    }
    return 0;
}
static uint32_t drive(const EldenHapticsCalibration *c,uint32_t magnitude){
    for(unsigned i=1;i<c->count;++i){
        const EldenHapticsGainPoint *a=&c->points[i-1],*b=&c->points[i];
        if(magnitude<=b->magnitude_q15){
            uint32_t distance=b->magnitude_q15-a->magnitude_q15;
            return a->drive_q15+((magnitude-a->magnitude_q15)*(b->drive_q15-a->drive_q15)+distance/2)/distance;
        }
    }
    return c->points[c->count-1].drive_q15;
}
int elden_haptics_samples(const int16_t *input,size_t count,uint16_t intensity,
    const EldenHapticsCalibration *c,int8_t *output,uint64_t *cost){
    if(!input||!output||!cost||!count||intensity>32767||elden_haptics_calibration_check(c)||count>c->max_frames)return -22;
    /* Two pure passes avoid an output buffer proportional to the stream while
     * retaining atomic output on failure. Caller owns immutable input/LUT. */
    uint64_t squares=0;
    for(size_t i=0;i<count;++i){
        int32_t sample=input[i];uint32_t magnitude=(uint32_t)(sample<0?-sample:sample);
        if(magnitude>32767)magnitude=32767;
        magnitude=(magnitude*intensity+16383u)/32767u;
        uint64_t value=drive(c,magnitude);uint64_t square=value*value;
        if(square>c->square_budget_q30||squares>c->square_budget_q30-square)return -75;
        squares+=square;
    }
    for(size_t i=0;i<count;++i){
        int32_t sample=input[i];uint32_t magnitude=(uint32_t)(sample<0?-sample:sample);
        if(magnitude>32767)magnitude=32767;
        magnitude=(magnitude*intensity+16383u)/32767u;
        int32_t value=(int32_t)((drive(c,magnitude)*127u+16383u)/32767u);
        output[i]=(int8_t)(sample<0?-value:value);
    }
    *cost=squares;return 0;
}
