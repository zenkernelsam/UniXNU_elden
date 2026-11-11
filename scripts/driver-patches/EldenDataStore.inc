// SPDX-License-Identifier: BSD-2-Clause-Patent
// Fixed Data pool inside ipados; preserves the root and future stage/log slots.
// Included after StoreGeometry/storeMode in the existing UFS translation unit.
static bool eldenDataArg(const char *name){unsigned value=0;return PE_parse_boot_argn(name,&value,sizeof(value))&&value==1;}
static bool eldenDataValidate(void){
    if(!gHost.ready||gHost.poisoned||gRootLun!=0||strcmp(UFS_ROOT_PARTITION,"ipados")||
       !gStoreGeometry.valid||gLuns[0].blockBytes!=4096)return false;
    uint8_t *memory=static_cast<uint8_t *>(IOMalloc(40960));if(!memory)return false;
    bool ok=false;EldenDataGptHeader primary={},backup={};
    if(hostRead4k(0,1,1,memory)!=kIOReturnSuccess||
       elden_data_gpt_header(memory,4096,gRootLunLast,false,&primary))goto done;
    if(hostRead4k(0,gRootLunLast,1,memory+4096)!=kIOReturnSuccess||
       elden_data_gpt_header(memory+4096,4096,gRootLunLast,true,&backup))goto done;
    if(hostRead4k(0,primary.entries,(primary.count*128+4095)/4096,memory+8192)!=kIOReturnSuccess||
       hostRead4k(0,backup.entries,(backup.count*128+4095)/4096,memory+24576)!=kIOReturnSuccess||
       elden_data_gpt_pair(&primary,memory+8192,primary.count*128,&backup,memory+24576,backup.count*128))goto done;
    if(hostRead4k(0,ELDEN_DATA_FIRST,1,memory)!=kIOReturnSuccess)goto done;
    {
        SHA256_CTX hash;uint8_t digest[32];SHA256_Init(&hash);SHA256_Update(&hash,memory,4096);SHA256_Final(digest,&hash);
        static const uint8_t expected[32]={0x66,0x84,0xc6,0x52,0xb8,0x5e,0xca,0xa8,0x07,0x69,0xc8,0xec,0x37,0xeb,0x5c,0x4c,
            0xec,0x51,0x00,0x85,0x1b,0x59,0x2a,0x55,0xe5,0xf2,0x07,0x78,0x97,0x62,0x1e,0xab};
        if(memcmp(digest,expected,32))goto done;
    }
    if(elden_data_layout(&gEldenDataLayout,gStoreGeometry.first,gStoreGeometry.last,gStoreGeometry.imageBytes))goto done;
    ok=true;
done:
    IOFree(memory,40960);return ok;
}
static int eldenDataStoreOpen(uint64_t blocks,uint64_t *first,const char **why){
    const char *reason="";unsigned mode=storeMode();
    if(!mode)reason="off";
    else if(gStoreRegionBlocks)reason="already open";
    else if(!eldenDataValidate())reason="GPT pair/root header/layout refused";
    else if(!blocks||blocks>gEldenDataLayout.data.blocks)reason="Data pool capacity";
    else if(mode==2&&!eldenDataArg("elden-data-write-reviewed"))reason="write qualification not reviewed";
    else{
        EldenDataRange requested={gEldenDataLayout.data.first,blocks};
        if(mode==2){
            gEldenDataVerify=static_cast<uint8_t *>(IOMalloc(kDataMax));
            if(!gEldenDataVerify)reason="readback allocation";
        }
        if(!*reason&&!elden_data_fence_open(&gEldenDataCommand.fence,mode,requested))reason="closed owner";
        if(!*reason&&mode==2&&!hostStoreOpen(requested.first,requested.blocks))reason="transport window";
        if(!*reason){
            gStoreRegionFirst=requested.first;gStoreRegionBlocks=blocks;gEldenDataValidated=true;
            if(first)*first=requested.first;
        }else{
            elden_data_fence_poison(&gEldenDataCommand.fence);
            if(gEldenDataVerify){IOFree(gEldenDataVerify,kDataMax);gEldenDataVerify=nullptr;}
        }
    }
    if(why)*why=reason;return *reason?0:1;
}
static int eldenDataStoreRead(uint64_t rel,uint32_t blocks,void *destination){
    if(!gStoreRegionBlocks||!destination||!blocks||rel>=gStoreRegionBlocks||blocks>gStoreRegionBlocks-rel||
       __atomic_load_n(&gEldenDataCommand.fence.poisoned,__ATOMIC_ACQUIRE))return 0;
    while(blocks){
        uint32_t count=blocks>kDataMax/4096?kDataMax/4096:blocks;
        if(hostRead4k(0,gStoreRegionFirst+rel,count,destination)!=kIOReturnSuccess){elden_data_fence_poison(&gEldenDataCommand.fence);return 0;}
        destination=static_cast<uint8_t *>(destination)+count*4096;blocks-=count;rel+=count;
    }
    return 1;
}
static int eldenDataStoreWrite(uint64_t rel,uint32_t blocks,const void *source){
    if(storeMode()!=2||!gEldenDataValidated||!gEldenDataVerify||!source||!blocks||
       rel>=gStoreRegionBlocks||blocks>gStoreRegionBlocks-rel)return 0;
    while(blocks){
        uint32_t count=blocks>kDataMax/4096?kDataMax/4096:blocks;if(count>32)count=32;
        uint64_t absolute=gStoreRegionFirst+rel;
        if(!elden_data_command_write(&gEldenDataCommand,absolute,count))return 0;
        bool ok=hostWrite4k(0,absolute,count,source,true)==kIOReturnSuccess;
        if(ok)ok=elden_data_command_sync(&gEldenDataCommand,true)&&hostSyncCache()==kIOReturnSuccess;
        // Do not infer FUA support from a successful WRITE. Explicit sync and
        // complete data readback are required before this first port reports it.
        if(ok)ok=hostRead4k(0,absolute,count,gEldenDataVerify)==kIOReturnSuccess&&
            !memcmp(source,gEldenDataVerify,count*4096);
        elden_data_command_end(&gEldenDataCommand,ok);if(!ok)return 0;
        source=static_cast<const uint8_t *>(source)+count*4096;blocks-=count;rel+=count;
    }
    return 1;
}
static int eldenDataStoreSync(void){
    if(storeMode()!=2||!gEldenDataValidated||!elden_data_command_sync(&gEldenDataCommand,false))return 0;
    bool ok=hostSyncCache()==kIOReturnSuccess;elden_data_command_end(&gEldenDataCommand,ok);return ok;
}
extern "C" int native_ufs_store_initialize_allowed(void){
    return gEldenDataValidated&&storeMode()==2&&eldenDataArg("elden-data-initialize")&&
        eldenDataArg("elden-data-backup-reviewed")&&!gEldenDataCommand.fence.poisoned;
}
extern "C" int elden_data_panic_reset_ready(void){
    // Panic context: no allocation, lock or new command. Existing durable
    // transactions remain crash-recoverable; uncertain commands hold reset.
    return elden_data_fence_close(&gEldenDataCommand.fence);
}
extern "C" int elden_data_recovery_close(void){
    if(storeMode()<2)return 1;
    if(!elden_data_fence_close(&gEldenDataCommand.fence))return 0;
    return eldenDataStoreSync()&&elden_data_fence_reset_ready(&gEldenDataCommand.fence);
}
extern "C" int elden_data_rpc_ready(void){
    return storeMode()==2&&gEldenDataValidated&&gEldenDataCommand.fence.mode==2&&
        !__atomic_load_n(&gEldenDataCommand.fence.closed,__ATOMIC_ACQUIRE)&&
        !__atomic_load_n(&gEldenDataCommand.fence.poisoned,__ATOMIC_ACQUIRE);
}
extern "C" int elden_data_recovery_reset_ready(void){return elden_data_fence_reset_ready(&gEldenDataCommand.fence);}
