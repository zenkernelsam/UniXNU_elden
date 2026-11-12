/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
/* Include NativeLegacyRequest.h first: retain its established bounds rules. */
static inline void elden_fb_store_pair(volatile void *destination,uint64_t a,uint64_t b)
{
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("stp %x0, %x1, [%2]"::"r"(a),"r"(b),"r"(destination):"memory");
#else
    volatile uint64_t *out=(volatile uint64_t *)destination;out[0]=a;out[1]=b;
#endif
}

/* -1: ineligible, use the unchanged copier. 0: invalid layout. 1: copied.
 * Pair stores are selected only after Normal-NC mapping qualification; they
 * are never substituted for arbitrary device-memory register accesses.
 */
static __attribute__((noinline)) int elden_fb_copy_pairs(int normalNc,volatile void *destination,
    uint64_t destinationBytes,uint64_t destinationStride,const void *source,
    uint64_t sourceBytes,uint64_t sourceStride,uint32_t width,uint32_t height)
{
    if(!normalNc)return -1;
    if(!native_legacy_copy_layout(destination,destinationBytes,destinationStride,
        source,sourceBytes,sourceStride,width,height))return 0;
    uintptr_t dst=(uintptr_t)destination,src=(uintptr_t)source;
    if((dst|src|destinationStride|sourceStride)&15)return -1;
    if(dst<src+sourceBytes&&src<dst+destinationBytes)return -1;
    const uint64_t rowBytes=(uint64_t)width*4;
    for(uint32_t row=0;row<height;row++){
        volatile unsigned char *out=(volatile unsigned char *)(dst+row*destinationStride);
        const unsigned char *in=(const unsigned char *)(src+row*sourceStride);
        uint64_t offset=0;
        for(;offset+64<=rowBytes;offset+=64){
            for(unsigned pair=0;pair<4;pair++){
                uint64_t a,b;uint64_t at=offset+pair*16;
                __builtin_memcpy(&a,in+at,8);__builtin_memcpy(&b,in+at+8,8);
                elden_fb_store_pair(out+at,a,b);
            }
        }
        for(;offset+16<=rowBytes;offset+=16){
            uint64_t a,b;__builtin_memcpy(&a,in+offset,8);__builtin_memcpy(&b,in+offset+8,8);
            elden_fb_store_pair(out+offset,a,b);
        }
        for(;offset<rowBytes;offset+=4){
            uint32_t pixel;__builtin_memcpy(&pixel,in+offset,4);
            *(volatile uint32_t *)(out+offset)=pixel;
        }
    }
    return 1;
}
