/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent packed10bit -> linear/sRGB reference and exact rotation axes. */
#include "Raw10.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"raw10 line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static void pack(uint8_t *row,unsigned x,unsigned v){unsigned group=x/4,k=x%4;row[group*5+k]=(uint8_t)(v/4);row[group*5+4]=(uint8_t)((row[group*5+4]&~(3u<<(2*k)))|((v&3)<<(2*k)));}
static unsigned gamma_byte(unsigned v,unsigned black,unsigned white,unsigned gain){double linear=v>black?(double)(v-black)*gain/(65536.0*(white-black)):0;
    if(linear>1)linear=1;unsigned quantized=(unsigned)(linear*255+0.5);double q=quantized/255.0;
    return (unsigned)((q<=0.0031308?12.92*q:1.055*pow(q,1/2.4)-0.055)*255+0.5);}
int main(void){uint8_t raw[48],out[128];
    for(unsigned bayer=0;bayer<4;bayer++)for(unsigned rotation=0;rotation<4;rotation++)for(unsigned mirror=0;mirror<2;mirror++){
        cases++;memset(raw,0,sizeof(raw));memset(out,0xcd,sizeof(out));
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++)pack(raw+y*12,x,(((y/2)*4+x/2)*83+(y%2)*31+(x%2)*13+3)&1023);
        unsigned width=rotation&1?2:4,height=rotation&1?4:2;
        EldenRaw10Config cfg={8,4,12,bayer,rotation*90,mirror,width,height,0,1023,{65536,65536,65536}};
        CHECK(!elden_raw10_bgra(&cfg,raw,sizeof(raw),out,sizeof(out),32));
        for(unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++){
            unsigned c[4]={(((y*4+x)*83+3)&1023),(((y*4+x)*83+16)&1023),(((y*4+x)*83+34)&1023),(((y*4+x)*83+47)&1023)};
            const unsigned red[]={0,1,2,3},blue[]={3,2,1,0};unsigned r=c[red[bayer]],b=c[blue[bayer]],g=(c[0]+c[1]+c[2]+c[3]-r-b+1)/2;
            unsigned dx=x,dy=y;if(rotation==1){dx=1-y;dy=x;}if(rotation==2){dx=3-x;dy=1-y;}if(rotation==3){dx=y;dy=3-x;}if(mirror)dx=width-1-dx;
            uint8_t *p=out+dy*32+dx*4;CHECK(p[0]==gamma_byte(b,0,1023,65536)&&p[1]==gamma_byte(g,0,1023,65536)&&p[2]==gamma_byte(r,0,1023,65536)&&p[3]==255);
        }
        for(unsigned y=0;y<height;y++)for(unsigned x=width*4;x<32;x++)CHECK(out[y*32+x]==0xcd);
    }
    for(unsigned value=0;value<1024;value++){
        cases++;memset(raw,0,sizeof(raw));memset(out,0xcd,sizeof(out));for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++)pack(raw+y*12,x,value);
        EldenRaw10Config cfg={8,4,12,0,0,0,4,2,64,1023,{65536,131072,32768}};
        CHECK(!elden_raw10_bgra(&cfg,raw,sizeof(raw),out,sizeof(out),32));
        CHECK(out[0]==gamma_byte(value,64,1023,32768)&&out[1]==gamma_byte(value,64,1023,131072)&&out[2]==gamma_byte(value,64,1023,65536));
    }
    for(unsigned bad=0;bad<13;bad++){
        cases++;memset(out,0xcd,sizeof(out));EldenRaw10Config c={8,4,12,0,0,0,4,2,0,1023,{65536,65536,65536}};size_t n=sizeof(raw),bytes=sizeof(out),stride=32;
        if(bad==0)c.width=7;if(bad==1)c.height=3;if(bad==2)c.stride=9;if(bad==3)c.bayer=4;if(bad==4)c.rotation=45;if(bad==5)c.gain_q16[1]=0;
        if(bad==6)c.gain_q16[1]=262145;if(bad==7)c.black=c.white;if(bad==8)c.output_width=5;if(bad==9)n=47;if(bad==10)bytes=32;if(bad==11)stride=3;if(bad==12)c.output_width=0;
        CHECK(elden_raw10_bgra(&c,raw,n,out,bytes,stride)==-22);for(unsigned i=0;i<sizeof(out);i++)CHECK(out[i]==0xcd);
    }
    EldenRaw10Config good={8,4,12,0,0,0,4,2,0,1023,{65536,65536,65536}};CHECK(elden_raw10_bgra(&good,raw,sizeof(raw),raw,sizeof(raw),16)==-22);
    printf("PASS RAW10 fullbits/cell/rotation/mirror/calibration/refusal: %u cases/%u checks; pixels synthetic host fixtures\n",cases,checks);return 0;
}
