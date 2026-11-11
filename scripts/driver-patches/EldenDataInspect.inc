// SPDX-License-Identifier: BSD-2-Clause-Patent
// Called after the qualified display/volatile Data path; read-only survey.
static unsigned gEldenDataInspectStarted;
static void eldenDataInspectWorker(void *,wait_result_t){
    for(unsigned i=0;i<12000&&(!elden_recovery_armed()||!gStoreGeometry.valid);i++)IOSleep(10);
    bool valid=elden_recovery_armed()&&eldenDataValidate();
    char line[320];snprintf(line,sizeof(line),"\nELDEN_DATA_LAYOUT valid=%u root-readonly=1 mode=%u\n",valid,storeMode());eldenSdamLog(line);
    if(valid){
        snprintf(line,sizeof(line),"\nELDEN_DATA_LAYOUT pool=%llx+%llx logical-max=%llu stage0=%llx stage1=%llx log=%llx\n",
            gEldenDataLayout.data.first,gEldenDataLayout.data.blocks,gEldenDataLayout.logical_max,
            gEldenDataLayout.stage[0].first,gEldenDataLayout.stage[1].first,gEldenDataLayout.log.first);eldenSdamLog(line);
        uint8_t *block=static_cast<uint8_t *>(IOMalloc(4096));
        bool read=block&&hostRead4k(0,gEldenDataLayout.data.first,1,block)==kIOReturnSuccess;
        unsigned nonzero=0;if(read)for(unsigned k=0;k<4096;k++)nonzero|=block[k];
        snprintf(line,sizeof(line),"\nELDEN_DATA_LAYOUT pool-header-read=%u zero=%u magic=%08x,%08x writes=0\n",
            read,read&&!nonzero,read?le32(block):0,read?le32(block+4):0);eldenSdamLog(line);
        if(block)IOFree(block,4096);
    }
    thread_terminate(current_thread());
}
extern "C" void elden_data_inspect_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-data-layout-inspect",&mode,sizeof(mode));
    if(mode!=1||__atomic_exchange_n(&gEldenDataInspectStarted,1,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(eldenDataInspectWorker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
}
