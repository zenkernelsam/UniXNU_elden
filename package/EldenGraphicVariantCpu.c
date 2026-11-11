/* SPDX-License-Identifier: MIT */
#include "EldenGraphicVariantCpu.h"
#include <math.h>
#include <stdint.h>

CGImageRef elden_graphic_variant_cpu(CGImageRef background,CGImageRef symbol,
    CGRect destination,CGFloat sourceScale,CGFloat outputScale,
    CGColorRef foreground,bool disableColorFill,int *error)
{
    if(error)*error=1;
    if(!background||!symbol||!isfinite(sourceScale)||!isfinite(outputScale)||
       sourceScale<=0||outputScale<=0||sourceScale>64||outputScale>64||
       !isfinite(destination.origin.x)||!isfinite(destination.origin.y)||
       !isfinite(destination.size.width)||!isfinite(destination.size.height)||
       destination.size.width<0||destination.size.height<0)return NULL;
    size_t width=CGImageGetWidth(background),height=CGImageGetHeight(background);
    size_t sw=CGImageGetWidth(symbol),sh=CGImageGetHeight(symbol);
    if(!width||!height||!sw||!sh||width>4096||height>4096||sw>16384||sh>16384)return NULL;
    double ratio=outputScale/sourceScale;
    CGRect placement=CGRectMake(destination.origin.x*outputScale,
        destination.origin.y*outputScale,sw*ratio,sh*ratio);
    if(!isfinite(placement.origin.x)||!isfinite(placement.origin.y)||
       !isfinite(placement.size.width)||!isfinite(placement.size.height)||
       fabs(placement.origin.x)>65536||fabs(placement.origin.y)>65536||
       placement.size.width>16384||placement.size.height>16384)return NULL;
    CGColorSpaceRef colors=CGImageGetColorSpace(background);
    if(colors&&CGColorSpaceGetModel(colors)==kCGColorSpaceModelRGB)CGColorSpaceRetain(colors);
    else colors=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if(!colors){if(error)*error=2;return NULL;}
    CGContextRef context=CGBitmapContextCreate(NULL,width,height,8,width*4,colors,
        kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colors);
    if(!context){if(error)*error=2;return NULL;}
    CGRect canvas=CGRectMake(0,0,width,height);
    CGContextClearRect(context,canvas);
    CGContextSetBlendMode(context,kCGBlendModeNormal);
    CGContextDrawImage(context,canvas,background);
    CGContextSetInterpolationQuality(context,kCGInterpolationHigh);
    if(foreground&&!disableColorFill){
        CGContextSaveGState(context);
        CGContextClipToMask(context,placement,symbol);
        CGContextSetFillColorWithColor(context,foreground);
        CGContextFillRect(context,placement);
        CGContextRestoreGState(context);
    }else CGContextDrawImage(context,placement,symbol);
    CGImageRef image=CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    if(error)*error=image?0:2;
    return image;
}
