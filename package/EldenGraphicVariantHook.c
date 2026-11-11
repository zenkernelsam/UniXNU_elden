/* SPDX-License-Identifier: MIT
 * Exact 21H461 CoreUI compatibility path, only in the selected IconServices
 * process. It returns real CPU-composited bitmaps; extra CI effects remain a
 * documented limitation until the GPU/effect pipeline is implemented. */
#include "EldenGraphicVariantCpu.h"
#include "EldenIconCpuPaths.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <objc/runtime.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

extern int elden_marker_syscall(int,...) __asm("_syscall");
typedef CGImageRef (*VariantImp)(id,SEL,CGFloat,CGImageRef,CGImageRef,CGRect,bool,CGFloat);
typedef id (*ObjectGetter)(id,SEL);
typedef CGColorRef (*ColorGetter)(id,SEL);
static Class variantClass,optionsClass,cfColorClass;
static VariantImp originalVariant;
static ObjectGetter getOptions,getForeground;
static ColorGetter getColor;
static SEL optionsSelector,foregroundSelector,colorSelector;
static IMP colorImplementation;
static const char variantTypes[]="^{CGImage=}84@0:8d16^{CGImage=}24^{CGImage=}32{CGRect={CGPoint=dd}{CGSize=dd}}40B72d76";
static const char coreuiPath[]="/System/Library/PrivateFrameworks/CoreUI.framework/CoreUI";
static const uint8_t coreuiUUID[]={0xd6,0x29,0x14,0xc6,0xc4,0xdc,0x3c,0x00,0xa1,0x75,0xb5,0x15,0xa6,0xb9,0x01,0xa3};

static void glyph_note(unsigned stage,uint64_t data){
    static unsigned successes,failures;
    if(stage==2){if(__atomic_fetch_add(&successes,1u,__ATOMIC_RELAXED)>=8)return;}
    else if(stage!=1&&__atomic_fetch_add(&failures,1u,__ATOMIC_RELAXED)>=8)return;
    (void)elden_marker_syscall(SYS_kdebug_trace,(unsigned long)ELDEN_ICON_GLYPH_TRACE_CODE,
        (unsigned long)stage,(unsigned long)data,1UL,(unsigned long)ELDEN_ICON_TRACE_MAGIC);
}
static int in_class(id object,Class expected){
    Class cls=object?object_getClass(object):Nil;
    for(unsigned i=0;cls&&i<32;i++,cls=class_getSuperclass(cls))if(cls==expected)return 1;
    return 0;
}
static int framework_slide(intptr_t *slide){
    for(uint32_t i=0;i<_dyld_image_count();i++){
        const char *path=_dyld_get_image_name(i);if(!path||strcmp(path,coreuiPath))continue;
        const struct mach_header_64 *h=(const void *)_dyld_get_image_header(i);
        if(!h||h->magic!=MH_MAGIC_64||h->sizeofcmds>65536||h->ncmds>1024)return 0;
        const uint8_t *at=(const uint8_t *)(h+1),*end=at+h->sizeofcmds;unsigned found=0;
        for(uint32_t n=0;n<h->ncmds;n++){
            if((size_t)(end-at)<sizeof(struct load_command))return 0;
            const struct load_command *lc=(const void *)at;
            if(lc->cmdsize<sizeof(*lc)||lc->cmdsize>(size_t)(end-at))return 0;
            if(lc->cmd==LC_UUID){
                if(lc->cmdsize!=sizeof(struct uuid_command)||memcmp(((const struct uuid_command *)lc)->uuid,coreuiUUID,16))return 0;
                found++;
            }
            at+=lc->cmdsize;
        }
        if(found!=1||at!=end)return 0;*slide=_dyld_get_image_vmaddr_slide(i);return 1;
    }
    return 0;
}
static Method checked_method(Class cls,const char *name,unsigned argc,const char *type,uintptr_t address,const uint8_t *bytes,size_t length){
    Method m=cls?class_getInstanceMethod(cls,sel_registerName(name)):NULL;
    if(!m||method_getNumberOfArguments(m)!=argc||strcmp(method_getTypeEncoding(m),type))return NULL;
    IMP imp=method_getImplementation(m);
    return (uintptr_t)imp==address&&!memcmp((const void *)imp,bytes,length)?m:NULL;
}
static CGImageRef cpu_variant(id self,SEL command,CGFloat sourceScale,CGImageRef background,
    CGImageRef symbol,CGRect destination,bool disableColorFill,CGFloat outputScale){
    if(!in_class(self,variantClass)){glyph_note(3,20);return originalVariant(self,command,sourceScale,background,symbol,destination,disableColorFill,outputScale);}
    id options=getOptions(self,optionsSelector);CGColorRef tint=NULL;bool cfColor=false;
    if(options){
        if(!in_class(options,optionsClass)){glyph_note(3,21);return originalVariant(self,command,sourceScale,background,symbol,destination,disableColorFill,outputScale);}
        id color=getForeground(options,foregroundSelector);
        if(color&&!disableColorFill){
            if(in_class(color,cfColorClass)&&CFGetTypeID((CFTypeRef)color)==CGColorGetTypeID()){
                tint=(CGColorRef)color;cfColor=true;
            }else{
            Method m=class_getInstanceMethod(object_getClass(color),colorSelector);
            if(!m||method_getNumberOfArguments(m)!=2||strcmp(method_getTypeEncoding(m),"^{CGColor=}16@0:8")||method_getImplementation(m)!=colorImplementation){
                glyph_note(3,22);return originalVariant(self,command,sourceScale,background,symbol,destination,disableColorFill,outputScale);
            }
            tint=getColor(color,colorSelector);
            }
        }
    }
    int error=0;CGImageRef image=elden_graphic_variant_cpu(background,symbol,destination,sourceScale,outputScale,tint,disableColorFill,&error);
    uint64_t data=(background?CGImageGetWidth(background):0)|((uint64_t)(background?CGImageGetHeight(background):0)<<16);
    data|=((uint64_t)(tint!=NULL)<<32)|((uint64_t)disableColorFill<<33)|(UINT64_C(1)<<34)|((uint64_t)cfColor<<35)|((uint64_t)(unsigned)error<<40);
    glyph_note(image?2:4,data);return image;
}
void elden_install_graphic_variant_cpu(void){
    const char *process=getprogname(),*enabled=getenv("ELDEN_ICON_CPU_MODE");
    if(!process||strcmp(process,"iconservicesagent")||!enabled||strcmp(enabled,"0")){glyph_note(3,1);return;}
    if(!dlopen(coreuiPath,RTLD_NOW|RTLD_LOCAL)){glyph_note(3,2);return;}
    intptr_t slide=0;if(!framework_slide(&slide)){glyph_note(3,3);return;}
    variantClass=objc_getClass("_CUIGraphicVariantVectorGlyph");optionsClass=objc_getClass("CUIVectorGlyphGraphicVariantOptions");Class colors=objc_getClass("CUIColor");
    static const uint8_t v[]={0xff,0x43,0x02,0xd1,0xef,0x3b,0x01,0x6d,0xed,0x33,0x02,0x6d,0xeb,0x2b,0x03,0x6d};
    static const uint8_t o[]={0xe8,0x6f,0x25,0x90,0x08,0xd1,0x82,0xb9,0x00,0x68,0x68,0xf8,0xc0,0x03,0x5f,0xd6};
    static const uint8_t f[]={0x00,0x14,0x40,0xf9,0xc0,0x03,0x5f,0xd6};
    static const uint8_t c[]={0x00,0x04,0x40,0xf9,0xc0,0x03,0x5f,0xd6};
    Method vm=checked_method(variantClass,"_createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:",8,variantTypes,UINT64_C(0x1a318679c)+(uintptr_t)slide,v,sizeof(v));
    Method om=checked_method(variantClass,"options",2,"@16@0:8",UINT64_C(0x1a3187744)+(uintptr_t)slide,o,sizeof(o));
    Method fm=checked_method(optionsClass,"monochromeForegroundColor",2,"@16@0:8",UINT64_C(0x1a3187d04)+(uintptr_t)slide,f,sizeof(f));
    Method cm=checked_method(colors,"CGColor",2,"^{CGColor=}16@0:8",UINT64_C(0x1a31ac518)+(uintptr_t)slide,c,sizeof(c));
    if(!vm||!om||!fm||!cm){glyph_note(3,4);return;}
    // Obtain the real CFColor bridge class from a CoreGraphics-created value;
    // do not guess class names or cast an arbitrary Objective-C object.
    CGColorSpaceRef referenceSpace=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGFloat components[]={0,0,0,1};
    CGColorRef reference=referenceSpace?CGColorCreate(referenceSpace,components):NULL;
    if(referenceSpace)CGColorSpaceRelease(referenceSpace);
    if(!reference){glyph_note(3,7);return;}
    cfColorClass=object_getClass((id)reference);CGColorRelease(reference);
    if(!cfColorClass){glyph_note(3,7);return;}
    optionsSelector=sel_registerName("options");foregroundSelector=sel_registerName("monochromeForegroundColor");colorSelector=sel_registerName("CGColor");
    getOptions=(ObjectGetter)method_getImplementation(om);getForeground=(ObjectGetter)method_getImplementation(fm);colorImplementation=method_getImplementation(cm);getColor=(ColorGetter)colorImplementation;
    IMP before=method_getImplementation(vm);originalVariant=(VariantImp)before;
    IMP previous=method_setImplementation(vm,(IMP)cpu_variant);
    if(previous!=before){if(method_getImplementation(vm)==(IMP)cpu_variant)method_setImplementation(vm,previous);glyph_note(3,5);return;}
    if(method_getImplementation(vm)!=(IMP)cpu_variant){glyph_note(3,6);return;}
    glyph_note(1,0);
}
