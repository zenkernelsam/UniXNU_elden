// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only PMIC arbiter v7 resource/ownership audit. Addresses agree with
// TB322FC stock FDT and the SM8750 Linux DTS. No peripheral command is issued.
static void eldenRebootProbe(void) {
  unsigned enabled=0;PE_parse_boot_argn("elden-reboot-probe",&enabled,sizeof(enabled));
  if(enabled!=1)return;
  OSSharedPtr<IOMemoryMap> coreMap,ownerMap;
  uintptr_t core=mapPhys(0x0c400000,0x3000,coreMap);
  uintptr_t owner=mapPhys(0x0c42d000,0x4000,ownerMap);
  char line[180];
  if(!core||!owner){eldenRecoveryProbeLog("ELDEN_REBOOT_PROBE mapping failed\n");return;}
  uint32_t version=read32(core,0),features=read32(core,4),count=features&0x7ff;
  snprintf(line,sizeof(line),"ELDEN_REBOOT_PROBE version=%08x features=%08x count=%u ee=0 bus=0\n",version,features,count);
  eldenRecoveryProbeLog(line);
  if(version<0x70000000U||version>=0x80000000U||!count||count>1024){
    eldenRecoveryProbeLog("ELDEN_REBOOT_PROBE unsupported geometry; no table walk\n");return;
  }
  unsigned matches=0,owned=0;
  for(unsigned apid=0;apid<count;apid++){
    uint32_t entry=read32(core,0x2000+4*apid);
    if(!entry||((entry>>8)&0xfff)!=0x71)continue;
    uint32_t value=read32(owner,4*apid);unsigned ee=value&7;
    matches++;if(ee==0)owned++;
    if(matches<=8){snprintf(line,sizeof(line),"ELDEN_REBOOT_PROBE sid0:7148 apid=%u map=%08x owner=%08x write_ee=%u observer=%x rw=%x\n",apid,entry,value,ee,apid*0x20,apid*0x1000);eldenRecoveryProbeLog(line);}
  }
  snprintf(line,sizeof(line),"ELDEN_REBOOT_PROBE matches=%u owned=%u; register reads only\n",matches,owned);eldenRecoveryProbeLog(line);
}
