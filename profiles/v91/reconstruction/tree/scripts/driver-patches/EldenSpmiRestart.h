// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>
#include "EldenRebootResources.h"
namespace elden_spmi {
template<class Core,class Owner>
static bool select(Core core,Owner owner,unsigned *selected){
  if(core(0)!=ELDEN_REBOOT_VERSION || (core(4)&0x7ff)!=ELDEN_REBOOT_APID_COUNT)return false;
  unsigned owned=0,chosen=0;
  for(unsigned i=0;i<ELDEN_REBOOT_APID_COUNT;i++){
    uint32_t entry=core(0x2000+4*i);
    if(!entry || ((entry>>8)&0xfff)!=((ELDEN_REBOOT_SID<<8)|(ELDEN_REBOOT_REASON_REG>>8)))continue;
    if((owner(4*i)&7)==ELDEN_REBOOT_EE){owned++;chosen=i;}
  }
  if(owned!=1 || chosen!=ELDEN_REBOOT_APID)return false;
  *selected=chosen;return true;
}
static uint8_t bootloader(uint8_t before){
  return (uint8_t)((before & ~ELDEN_REBOOT_REASON_MASK) |
         ((ELDEN_REBOOT_BOOTLOADER<<ELDEN_REBOOT_REASON_SHIFT)&ELDEN_REBOOT_REASON_MASK));
}
static constexpr uint32_t read_command=(1U<<27)|((ELDEN_REBOOT_REASON_REG&255)<<4);
static constexpr uint32_t write_command=((ELDEN_REBOOT_REASON_REG&255)<<4);
}
