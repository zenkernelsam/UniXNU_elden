"""Compare actual old/new PTE builders across memory types and DRAM boundaries."""
import argparse, subprocess, json
from pathlib import Path
from rt_memory_patch import derive

def body(data):
    text=data.decode();start=text.index('wimg_to_pte(unsigned int wimg, __unused pmap_paddr_t pa)\n{');end=text.index('\n}',start)+2
    return 'static pt_entry_t\n'+text[start:end]

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    old=a.source.read_bytes();new=derive(old)
    declarations='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t pt_entry_t;
typedef uint64_t pmap_paddr_t;
#ifndef __unused
#define __unused
#endif
#define HAS_FEAT_XS 0
#define VM_WIMG_MASK 0xff
#define VM_WIMG_IO 7
#define VM_WIMG_RT 0x17
#define VM_WIMG_POSTED 0x27
#define VM_WIMG_POSTED_REORDERED 0x2e
#define VM_WIMG_POSTED_COMBINED_REORDERED 0x26
#define VM_WIMG_WCOMB 6
#define VM_WIMG_WTHRU 0xb
#define VM_WIMG_COPYBACK 2
#define VM_WIMG_INNERWBACK 0x12
#define CACHE_ATTRINDX_WRITEBACK 0
#define CACHE_ATTRINDX_DEFAULT 0
#define CACHE_ATTRINDX_WRITECOMB 1
#define CACHE_ATTRINDX_WRITETHRU 2
#define CACHE_ATTRINDX_DISABLE 3
#define CACHE_ATTRINDX_RT 3
#define CACHE_ATTRINDX_POSTED 5
#define CACHE_ATTRINDX_POSTED_REORDERED 6
#define CACHE_ATTRINDX_POSTED_COMBINED_REORDERED 7
#define CACHE_ATTRINDX_INNERWRITEBACK 0
#define ARM_PTE_ATTRINDX(x) ((uint64_t)(x)<<2)
#define ARM_PTE_SH(x) ((uint64_t)(x)<<8)
#define ARM_PTE_NX (1ull<<54)
#define ARM_PTE_PNX (1ull<<53)
#define SH_OUTER_MEMORY 2
#define SH_INNER_MEMORY 3
static bool is_dram_addr(uint64_t p){return p>=0x80000000ull && p<0xb00000000ull;}
static bool pa_valid(uint64_t p){return p>=0x8c007c000ull && p<0xa4007c000ull;}
'''
    test='''
int main(void){
 const uint64_t addresses[]={0,0x100000,0xfc800000,0x8c007bfff,0x8c007c000,0x8c007c001,0xa4007bfff,0xa4007c000,UINT64_MAX};
 unsigned checks=0;
 for(unsigned high=0;high<4;high++)for(unsigned type=0;type<256;type++)for(unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++){
  unsigned flags=type|(high<<8);uint64_t pa=addresses[i],a=old_pte(flags,pa),b=new_pte(flags,pa);
  if(type==VM_WIMG_RT && pa_valid(pa)){
   assert((b&28)==4);assert((a&28)==12);assert((a&~28ull)==(b&~28ull));
  }else assert(a==b);
  checks++;
 }
 printf("PASS %u actual PTE comparisons; managed RT only, MMIO and other modes unchanged\\n",checks);
}
'''
    source=a.output/'test.c';source.write_text(declarations+body(old).replace('wimg_to_pte(', 'old_pte(',1)+'\n'+body(new).replace('wimg_to_pte(', 'new_pte(',1)+test)
    binary=a.output/'test';command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(binary)]
    subprocess.run(command,check=True);r=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
    (a.output/'result.json').write_text(json.dumps({'passed':True,'stdout':r.stdout,'stderr':r.stderr,'command':command,'scope':'Actual function bodies; constants/managed-range classification supplied by fixture; hardware mappings still require device evidence'},indent=2)+'\n');print(r.stdout)

if __name__=='__main__':main()
