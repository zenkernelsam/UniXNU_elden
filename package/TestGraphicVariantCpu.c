#include "EldenGraphicVariantCpu.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CGImageRef solid(unsigned size,double r,double g,double b,double a){
 CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 CGContextRef c=CGBitmapContextCreate(NULL,size,size,8,size*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);assert(c);CGColorSpaceRelease(cs);
 CGContextSetRGBFillColor(c,r,g,b,a);CGContextFillRect(c,CGRectMake(0,0,size,size));
 CGImageRef image=CGBitmapContextCreateImage(c);CGContextRelease(c);return image;
}
static void pixels(CGImageRef image,unsigned char *data){
 CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);CGContextRef c=CGBitmapContextCreate(data,4,4,8,16,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);assert(c);CGColorSpaceRelease(cs);
 CGContextSetBlendMode(c,kCGBlendModeCopy);CGContextDrawImage(c,CGRectMake(0,0,4,4),image);CGContextRelease(c);
}
int main(void){
 CGImageRef bg=solid(4,1,0,0,1),symbol=solid(2,0,0,1,1);int error=-1;unsigned char data[64]={0};CGRect d=CGRectMake(1,1,2,2);
 CGImageRef result=elden_graphic_variant_cpu(bg,symbol,d,1,1,NULL,false,&error);assert(result&&error==0&&CGImageGetWidth(result)==4&&CGImageGetHeight(result)==4);pixels(result,data);assert(data[0]==255&&data[2]==0&&data[20]==0&&data[22]==255);CGImageRelease(result);
 CGImageRef large=solid(4,0,0,1,1);result=elden_graphic_variant_cpu(bg,large,d,2,1,NULL,false,&error);assert(result);pixels(result,data);assert(data[0]==255&&data[22]==255);CGImageRelease(result);CGImageRelease(large);
 CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);CGFloat green[]={0,1,0,1};CGColorRef tint=CGColorCreate(cs,green);CGColorSpaceRelease(cs);
 result=elden_graphic_variant_cpu(bg,symbol,d,1,1,tint,false,&error);assert(result);pixels(result,data);assert(data[20]==0&&data[21]==255&&data[22]==0);CGImageRelease(result);
 result=elden_graphic_variant_cpu(bg,symbol,d,1,1,tint,true,&error);assert(result);pixels(result,data);assert(data[21]==0&&data[22]==255);CGImageRelease(result);CGColorRelease(tint);
 CGImageRef half=solid(2,0,0,1,0.5);result=elden_graphic_variant_cpu(bg,half,d,1,1,NULL,false,&error);assert(result);pixels(result,data);assert(abs((int)data[20]-127)<=2&&abs((int)data[22]-128)<=2&&data[23]==255);CGImageRelease(result);CGImageRelease(half);
 assert(!elden_graphic_variant_cpu(bg,symbol,d,0,1,NULL,false,&error)&&error==1);
 assert(!elden_graphic_variant_cpu(bg,symbol,d,NAN,1,NULL,false,&error)&&error==1);
 assert(!elden_graphic_variant_cpu(bg,symbol,d,1e-100,1,NULL,false,&error)&&error==1);
 assert(!elden_graphic_variant_cpu(NULL,symbol,d,1,1,NULL,false,&error)&&error==1);
 result=elden_graphic_variant_cpu(bg,symbol,d,1,1,NULL,false,&error);CGImageRelease(bg);CGImageRelease(symbol);assert(result);pixels(result,data);assert(data[22]==255);CGImageRelease(result);
 puts("PASS real CPU composition: placement/scale, tint, bypass, alpha, bounds, owned-image lifetime");
}
