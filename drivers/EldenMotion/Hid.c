/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Hid.h"
#include "MotionDescriptors.inc"
static const EldenMotionHidKind kinds[2]={{0xff00,3,"Elden Accelerometer"},{0xff00,9,"Elden Gyroscope"}};
const EldenMotionHidKind *elden_motion_hid_kind(unsigned kind){return kind>=1&&kind<=2?&kinds[kind-1]:0;}
const uint8_t *elden_motion_hid_descriptor(unsigned kind,uint32_t *length){
    if(!length)return 0;if(kind==EldenMotionAccel){*length=sizeof(kAccel);return kAccel;}
    if(kind==EldenMotionGyro){*length=sizeof(kGyro);return kGyro;}*length=0;return 0;
}
int elden_motion_hid_event(const EldenMotionSample *s,unsigned negate,uint32_t sequence,EldenMotionHidEvent *out){
    if(!s||!out||!sequence||negate>7||!s->timestamp_ticks||(!s->suid.low&&!s->suid.high)||s->accuracy>3)return -22;
    if((s->kind==EldenMotionAccel&&s->unit!=EldenMotionG)||(s->kind==EldenMotionGyro&&s->unit!=EldenMotionRadiansPerSecond)||!elden_motion_hid_kind(s->kind))return -22;
    EldenMotionHidEvent e={0};e.type=s->kind==EldenMotionAccel?13:20;e.sequence=sequence;e.timestamp_ticks=s->timestamp_ticks;
    for(unsigned i=0;i<3;i++){int32_t v=s->q16[i];if(v==INT32_MIN)return -22;e.q16[i]=negate&(1u<<i)?-v:v;}*out=e;return 0;
}
int elden_motion_hid_interval(uint64_t us,uint32_t *hz){
    if(!hz||us>1000000||(us&&us<2500))return -22;*hz=us?(uint32_t)((1000000+us-1)/us):0;return 0;
}
