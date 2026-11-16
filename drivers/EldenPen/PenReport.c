/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Field evidence: stock NT36536 module a3c9fce9 and Armada0164; see source.json.
 * Signed tilt follows stock LDRSB, rather than copying an unsigned conversion. */
#include "PenReport.h"
static uint16_t be16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static int16_t signed8(uint8_t b){return (int16_t)(b<128?(int)b:(int)b-256);}
static uint16_t normalized(uint16_t v,uint16_t max){
    return (uint16_t)(((uint32_t)v*32767u+max/2u)/max);
}
int elden_pen_decode(const uint8_t *p,size_t bytes,const EldenPenLimits *limits,EldenPenFrame *out){
    if(!p||!limits||!out||!limits->max_x||!limits->max_y||
       (limits->max_pressure!=4095&&limits->max_pressure!=8191)||
       !limits->max_tilt||limits->max_tilt>90)return EldenPenArgument;
    if(bytes!=EldenPenPacketBytes)return EldenPenLength;
    EldenPenFrame f={0};f.kind=p[66];
    if(f.kind==EldenPenLeave){*out=f;return EldenPenOk;}
    if(f.kind==EldenPenIdentity){
        for(unsigned i=0;i<6;++i)f.identity[i]=p[67+i];
        *out=f;return EldenPenOk;
    }
    if(f.kind!=EldenPenCoordinate)return EldenPenUnsupported;
    f.raw_x=be16(p+67);f.raw_y=be16(p+69);f.pressure=be16(p+71);
    f.tilt_x=signed8(p[73]);f.tilt_y=signed8(p[74]);f.distance=be16(p+75);
    if(f.raw_x>limits->max_x||f.raw_y>limits->max_y||f.pressure>limits->max_pressure||
       f.tilt_x>limits->max_tilt||f.tilt_x<-(int)limits->max_tilt||
       f.tilt_y>limits->max_tilt||f.tilt_y<-(int)limits->max_tilt)return EldenPenRange;
    f.x=normalized(f.raw_x,limits->max_x);f.y=normalized(f.raw_y,limits->max_y);
    f.tip=(uint8_t)(f.pressure!=0);f.in_range=(uint8_t)(f.pressure!=0||f.distance!=0);
    f.buttons=p[77]&3u;f.battery_raw=p[78];f.tail_raw=p[79];
    *out=f;return EldenPenOk;
}
