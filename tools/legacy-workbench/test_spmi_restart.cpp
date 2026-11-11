#include "EldenSpmiRestart.h"
#include <cassert>
#include <cstdio>
int main(){
  uint32_t core[0x3000/4]={},owner[0x4000/4]={};unsigned chosen=0;
  core[0]=0x70020000;core[1]=0x80060380;
  core[0x2000/4+525]=0x00007100;owner[525]=3;
  core[0x2000/4+586]=0x01007100;owner[586]=0;
  core[0x2000/4+675]=0x00007100;owner[675]=1;
  auto c=[&](unsigned o){assert(o<sizeof(core)&&o%4==0);return core[o/4];};
  auto w=[&](unsigned o){assert(o<sizeof(owner)&&o%4==0);return owner[o/4];};
  assert(elden_spmi::select(c,w,&chosen)&&chosen==586);
  core[0]=0x80000000;assert(!elden_spmi::select(c,w,&chosen));core[0]=0x70020000;
  core[1]=897;assert(!elden_spmi::select(c,w,&chosen));core[1]=0x80060380;
  owner[586]=3;assert(!elden_spmi::select(c,w,&chosen));owner[586]=0;
  core[0x2000/4+600]=0x00007100;assert(!elden_spmi::select(c,w,&chosen));core[0x2000/4+600]=0;
  core[0x2000/4+586]=0x01007200;assert(!elden_spmi::select(c,w,&chosen));core[0x2000/4+586]=0x01007100;
  for(unsigned i=0;i<256;i++){auto after=elden_spmi::bootloader((uint8_t)i);assert((after&1)==(i&1));assert((after&0xfe)==4);}
  assert(elden_spmi::read_command==0x08000480&&elden_spmi::write_command==0x480);
  puts("PASS measured SPMI selection, 5 refusal cases, 256 preserved-bit vectors and exact commands");
}
