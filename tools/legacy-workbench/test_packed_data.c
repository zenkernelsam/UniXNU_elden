#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <openssl/sha.h>
#define STATIC static
#define CONST const
#define TRUE true
#define FALSE false
#define EFI_PAGE_SIZE 4096
#define SIZE_256MB (256ULL<<20)
typedef void VOID;typedef bool BOOLEAN;typedef uint8_t UINT8;typedef uint32_t UINT32;typedef uint64_t UINT64;typedef size_t UINTN;
static UINT32 ReadUnaligned32(const UINT32 *p){UINT32 v;memcpy(&v,p,4);return v;}
static UINT64 ReadUnaligned64(const UINT64 *p){UINT64 v;memcpy(&v,p,8);return v;}
static int CompareMem(const void *a,const void *b,size_t n){return memcmp(a,b,n);}
static void CopyMem(void *a,const void *b,size_t n){memcpy(a,b,n);}
#include "../research/pinned-root-20261107/PackedRamdisk.h"
int main(int argc,char **argv){
 _Static_assert(sizeof(size_t)==8,"64-bit firmware size model");assert(argc==2);
 FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);rewind(f);
 uint8_t *input=malloc((size_t)length);assert(input&&fread(input,1,length,f)==(size_t)length);fclose(f);
 size_t size=0;bool packed=false;
 assert(RamdiskLayout(input,length,&size,&packed)&&packed&&size==(512U<<20));
 uint8_t *expanded=calloc(1,size);assert(expanded);CopyRamdisk(expanded,input,length,true);
 uint8_t d[32];SHA256(expanded,size,d);char hex[65];for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",d[i]);
 assert(!strcmp(hex,"8104894952be1abadd7abaacbf2ca8d823c9789c525e2a3cbb2e57369745df33"));free(expanded);
 uint64_t old=ReadUnaligned64((const UINT64*)(input+24)),value=65537;memcpy(input+24,&value,8);
 assert(!RamdiskLayout(input,length,&size,&packed));memcpy(input+24,&old,8);
 old=ReadUnaligned64((const UINT64*)(input+32));value=512ULL<<20;memcpy(input+32,&value,8);
 assert(!RamdiskLayout(input,length,&size,&packed));memcpy(input+32,&old,8);
 assert(!RamdiskLayout(input,length-1,&size,&packed));free(input);
 puts("PASS: actual DarwinBoot C parser/copy reconstruct 512MiB SHA; bad count, extent and truncation rejected.");
}
