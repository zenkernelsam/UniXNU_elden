// SPDX-License-Identifier: BSD-2-Clause-Patent
// Pure admission rules for one Y700 misc/BCB command field, never a general writer.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace elden_bcb {
static constexpr uint64_t disk_last=248838143, first=89992, last=90247;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t u64(const uint8_t *p){return u32(p)|((uint64_t)u32(p+4)<<32);}
static uint32_t crc(const uint8_t *p,size_t n,bool header=false){uint32_t c=~0U;for(size_t i=0;i<n;i++){c^=(header&&i>=16&&i<20)?0:p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320U&(0U-(c&1)));}return ~c;}
static bool header(const uint8_t *h,bool backup){
  return !memcmp(h,"EFI PART",8)&&u32(h+8)==0x10000&&u32(h+12)==92&&u32(h+20)==0&&
    crc(h,92,true)==u32(h+16)&&u64(h+24)==(backup?disk_last:1)&&u64(h+32)==(backup?1:disk_last)&&
    u64(h+40)==6&&u64(h+48)==disk_last-5&&u64(h+72)==(backup?disk_last-4:2)&&u32(h+80)==32&&u32(h+84)==128;
}
static bool layout(const uint8_t *main,size_t mbytes,const uint8_t *backup,size_t bbytes){
  if(mbytes!=24576||bbytes!=20480||!header(main+4096,false)||!header(backup+16384,true))return false;
  const uint8_t *h=main+4096,*bh=backup+16384,*entries=main+8192;
  if(memcmp(h+56,bh+56,16)||memcmp(entries,backup,4096)||crc(entries,4096)!=u32(h+88)||u32(h+88)!=u32(bh+88))return false;
  static const uint8_t identity[32]={0x1f,0xc9,0xac,0x82,0x7c,0x35,0x68,0x4a,0x9c,0x8f,0x68,0x9e,0x1b,0x1a,0x23,0xa1,
    0xf7,0x4e,0x90,0x28,0xd7,0x79,0x5c,0x81,0x49,0x55,0xe7,0x7b,0xb5,0xee,0x1e,0xf2};
  uint8_t name[72]={ 'm',0,'i',0,'s',0,'c',0 };unsigned found=0;
  for(unsigned i=0;i<32;i++){
    const uint8_t *e=entries+128*i;uint8_t used=0;for(unsigned j=0;j<16;j++)used|=e[j];if(!used)continue;
    uint64_t lo=u64(e+32),hi=u64(e+40);if(lo<6||hi<lo||hi>disk_last-5)return false;
    if(!memcmp(e+56,name,72)){
      if(memcmp(e,identity,32)||lo!=first||hi!=last||u64(e+48))return false;
      found++;
    }else if(lo<=last&&first<=hi)return false;
  }
  return found==1;
}
static bool prepare(const uint8_t before[4096],uint8_t after[4096]){
  uint8_t expected[32]={ 'b','o','o','t','l','o','a','d','e','r' },empty[32]={};
  if(memcmp(before,empty,32)&&memcmp(before,expected,32))return false;
  memcpy(after,before,4096);memcpy(after,expected,32);return true;
}
static bool result(const uint8_t before[4096],const uint8_t after[4096]){
  uint8_t expected[32]={ 'b','o','o','t','l','o','a','d','e','r' };
  return !memcmp(after,expected,32)&&!memcmp(before+32,after+32,4064);
}
static bool command(bool permit,uint8_t lun,const uint8_t *cdb,unsigned length,unsigned direction,uint32_t bytes){
  if(!permit||lun!=0||!cdb||length!=10)return false;
  const uint8_t write[10]={0x2a,0x08,uint8_t(first>>24),uint8_t(first>>16),uint8_t(first>>8),uint8_t(first),0,0,1,0};
  const uint8_t sync[10]={0x35};
  return (direction==1&&bytes==4096&&!memcmp(cdb,write,10)) ||
         (direction==0&&bytes==0&&!memcmp(cdb,sync,10));
}
}
