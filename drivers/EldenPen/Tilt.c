/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Tilt.h"
#include "TiltConstants.h"
static uint64_t root64(uint64_t x){
    uint64_t result=0,bit=UINT64_C(1)<<62;
    while(bit>x)bit>>=2;
    while(bit){
        if(x>=result+bit){x-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return result;
}
/* Defined arithmetic shift: no implementation-defined negative right shift. */
static int64_t shift(int64_t x,unsigned n){
    if(x>=0)return x>>n;
    return -(int64_t)(((uint64_t)(-x)+((UINT64_C(1)<<n)-1))>>n);
}
static int32_t angle(int64_t x,int64_t y){
    if(!x)return y>0?90*65536:y<0?-90*65536:0;
    if(!y)return x>0?0:180*65536;
    int32_t result=0;
    if(x<0){result=y>=0?180*65536:-180*65536;x=-x;y=-y;}
    for(unsigned i=0;i<sizeof(elden_pen_atan)/sizeof(*elden_pen_atan);++i){
        if(!y)break;
        int64_t previous=x;
        if(y>0){x+=shift(y,i);y-=shift(previous,i);result+=elden_pen_atan[i];}
        else{x-=shift(y,i);y+=shift(previous,i);result-=elden_pen_atan[i];}
    }
    return result;
}
int elden_pen_tilt(int16_t tx,int16_t ty,EldenPenTilt *out){
    if(!out||tx< -60||tx>60||ty< -60||ty>60)return -1;
    int64_t x=elden_pen_tan[tx<0?-tx:tx],y=elden_pen_tan[ty<0?-ty:ty];
    if(tx<0)x=-x;if(ty<0)y=-y;
    uint64_t radius=root64((uint64_t)(x*x+y*y));
    EldenPenTilt result={angle((int64_t)radius,UINT64_C(1)<<24),angle(x,y)};
    if(result.azimuth_degrees<0)result.azimuth_degrees+=360*65536;
    if(result.azimuth_degrees>=360*65536)result.azimuth_degrees-=360*65536;
    if(result.altitude_degrees<0||result.altitude_degrees>90*65536)return -1;
    *out=result;return 0;
}
