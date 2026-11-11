#include <map>
#include <cassert>
#include <cstdio>
#include "EldenUfsIdentity.h"
int main(){
 std::map<unsigned,uint32_t> regs={{0,0x00230406},{0x20,0x4c017e84},{0x24,0x60000053},{0x28,0x5111},
   {0x800,0x80000060},{0x804,0x80000040},{0xc04,0},{0x1000,0x1f000},{0x1800,1},{0x80000,0x0a9f00e0}};
 auto read=[&](unsigned offset){assert(offset<0x100000);return regs[offset];};
 auto test=[&](){return eldenUfsIdentity(0x40,0x8c1000000ULL,32768,read);};
 auto r=test();assert(!r.error&&r.slot==1&&r.bank==0);
 regs[0x80000]|=1;assert(test().error);regs[0x80000]&=~1U;
 regs[0x808]=0x80000040;assert(test().error);regs[0x808]=0;
 regs[0x804]=0x80010040;assert(test().error);regs[0x804]=0x80000040;
 regs[0]|=1;assert(test().error);regs[0]&=~1U;
 assert(eldenUfsIdentity(0x40,0x1000000000ULL,32768,read).error);
 puts("PASS: synthetic SID40/high-DMA qualification; translated, duplicate, masked, disabled and out-of-range cases refused. No register writer exists.");
}
