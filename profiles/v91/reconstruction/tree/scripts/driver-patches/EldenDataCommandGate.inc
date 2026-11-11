// SPDX-License-Identifier: BSD-2-Clause-Patent
// Included inside the existing NativeUfs transport owner before RootCommands.
extern "C" {
#include "EldenPersistentData/Layout.h"
#include "EldenPersistentData/Fence.h"
#include "EldenPersistentData/Command.h"
#include "EldenPersistentData/Gpt.h"
}
#include "EldenDataBoard.h"
static EldenDataCommand gEldenDataCommand={};
static EldenDataLayout gEldenDataLayout={};
static bool gEldenDataValidated;
static uint8_t *gEldenDataVerify;
static bool eldenDataCommandAllowed(uint8_t lun,const uint8_t *cdb,unsigned length,unsigned direction,uint32_t bytes){
    return elden_data_command_allowed(&gEldenDataCommand,lun,cdb,length,direction,bytes);
}
static bool eldenDataWriteRange(uint64_t lba,uint32_t blocks){
    if(!blocks||blocks>32||lba>UINT32_MAX||!elden_data_contains(gEldenDataCommand.fence.window,lba,blocks))return false;
    uint8_t cdb[10]={0x2a,8,uint8_t(lba>>24),uint8_t(lba>>16),uint8_t(lba>>8),uint8_t(lba),
        0,uint8_t(blocks>>8),uint8_t(blocks),0};
    return eldenDataCommandAllowed(0,cdb,10,1,blocks*4096);
}
static bool eldenDataSyncScope(void){
    const uint8_t cdb[10]={0x35};return eldenDataCommandAllowed(0,cdb,10,0,0);
}
