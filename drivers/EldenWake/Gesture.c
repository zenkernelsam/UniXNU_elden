/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Gesture.h"
int elden_wake_gesture(const uint8_t *p,size_t n,int enabled,EldenWakeGesture *out){
    if(!p||!out||n<66||n>109||(enabled!=0&&enabled!=1))return EldenWakeArgument;
    if(!enabled)return EldenWakeNoGesture;
    uint8_t sum=0;for(unsigned i=1;i<=65;i++)sum=(uint8_t)(sum+p[i]);
    if(sum)return EldenWakeIntegrity;
    uint8_t raw=p[1]>>3,id=raw;
    if(raw==30&&p[2]==1)id=p[3];
    if(id!=15)return EldenWakeNoGesture;
    EldenWakeGesture value={raw,id,143};*out=value;return EldenWakeDoubleTap;
}
