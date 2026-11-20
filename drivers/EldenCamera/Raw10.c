/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Raw10.h"
/* Sampling/centre-crop/Bayer-cell method follows the qualified OS17 T970
 * software camera donor. This implementation preserves RAW10 low bits,
 * explicit black/white/WB, bounded caller buffers and all board rotations. */
#include "Raw10Srgb.inc"
static int span(const void *p,size_t n){return p&&n&&n<=UINTPTR_MAX-(uintptr_t)p;}
static uint32_t pixel(const uint8_t *p,uint32_t x){const uint8_t *g=p+(size_t)(x/4)*5;unsigned k=x&3;return (uint32_t)g[k]*4+((g[4]>>(2*k))&3);}
static uint8_t tone(uint32_t value,uint16_t black,uint16_t white,uint32_t gain){
    uint64_t scaled=value>black?(uint64_t)(value-black)*gain:0;
    uint64_t full=(uint64_t)(white-black)*65536;
    unsigned linear=scaled>=full?255u:(unsigned)((scaled*255+full/2)/full);return elden_raw10_srgb[linear];
}
int elden_raw10_bgra(const EldenRaw10Config *c,const uint8_t *raw,size_t raw_bytes,uint8_t *out,size_t out_bytes,size_t out_stride){
    if(!c||!span(raw,raw_bytes)||!span(out,out_bytes)||!c->width||c->width>8192||c->width&3||!c->height||c->height>8192||c->height&1||
       c->stride<c->width/4*5||c->stride>16384||c->bayer>3||c->mirror>1||(c->rotation!=0&&c->rotation!=90&&c->rotation!=180&&c->rotation!=270)||
       !c->output_width||!c->output_height||c->output_width>2048||c->output_height>2048||out_stride<(size_t)c->output_width*4||
       out_stride>SIZE_MAX/c->output_height||raw_bytes<(size_t)c->stride*c->height||out_bytes<out_stride*c->output_height||c->white>1023||c->white<=c->black)return -22;
    for(unsigned i=0;i<3;i++)if(!c->gain_q16[i]||c->gain_q16[i]>4*65536u)return -22;
    if((uintptr_t)raw<(uintptr_t)out+out_bytes&&(uintptr_t)out<(uintptr_t)raw+raw_bytes)return -22;
    uint32_t ow=(c->rotation==90||c->rotation==270)?c->output_height:c->output_width;
    uint32_t oh=(c->rotation==90||c->rotation==270)?c->output_width:c->output_height;
    if(ow>c->width/2||oh>c->height/2)return -22;
    uint32_t cw=c->width,ch=(uint32_t)((uint64_t)cw*oh/ow)&~1u;
    if(ch>c->height){ch=c->height;cw=(uint32_t)((uint64_t)ch*ow/oh)&~1u;}
    if(cw<2*ow||ch<2*oh)return -22;
    uint32_t x0=((c->width-cw)/2)&~1u,y0=((c->height-ch)/2)&~1u;
    static const unsigned red[4]={0,1,2,3},blue[4]={3,2,1,0};
    for(uint32_t y=0;y<oh;y++){
        uint32_t sy=(y0+(uint32_t)((uint64_t)y*ch/oh))&~1u;
        const uint8_t *l0=raw+(size_t)sy*c->stride,*l1=l0+c->stride;
        for(uint32_t x=0;x<ow;x++){
            uint32_t sx=(x0+(uint32_t)((uint64_t)x*cw/ow))&~1u;
            uint32_t v[4]={pixel(l0,sx),pixel(l0,sx+1),pixel(l1,sx),pixel(l1,sx+1)};
            uint32_t r=v[red[c->bayer]],b=v[blue[c->bayer]],g=(v[0]+v[1]+v[2]+v[3]-r-b+1)/2;
            uint32_t dx=x,dy=y;
            if(c->rotation==90){dx=oh-1-y;dy=x;}else if(c->rotation==180){dx=ow-1-x;dy=oh-1-y;}else if(c->rotation==270){dx=y;dy=ow-1-x;}
            if(c->mirror)dx=c->output_width-1-dx;
            uint8_t *p=out+(size_t)dy*out_stride+(size_t)dx*4;
            p[0]=tone(b,c->black,c->white,c->gain_q16[2]);p[1]=tone(g,c->black,c->white,c->gain_q16[1]);p[2]=tone(r,c->black,c->white,c->gain_q16[0]);p[3]=255;
        }
    }
    return 0;
}
