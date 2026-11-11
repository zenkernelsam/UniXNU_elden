/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Read-only original Peach firmware through the qualified native UFS owner.
 * No MHI/PCIe/PMIC/SMMU/GPIO operation and no fabricated network interface.
 */
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <libkern/crypto/sha2.h>
#include "ModemFiles.h"
#include "EldenWifiResources.h"
#include "EldenWifiFirmwareInputs.h"
extern "C" {
int native_ufs_part_find(const char *,uint32_t *,uint64_t *,uint64_t *);
int native_ufs_part_read(uint32_t,uint64_t,uint32_t,void *);
int elden_recovery_armed(void);
extern volatile int native_owner_data_status,elden_init_exec_accepted;
void elden_usb_log_putc(char);
volatile int32_t elden_wifi_fw_stats[8];
}
namespace {
unsigned attempted;
int find(void *,const char *name,uint32_t *lun,uint64_t *first,uint64_t *blocks) {
    return native_ufs_part_find(name,lun,first,blocks);
}
int read(void *,uint32_t lun,uint64_t first,uint32_t blocks,void *dst) {
    return native_ufs_part_read(lun,first,blocks,dst);
}
void report(const char *text) {for(const char *p=text;*p;++p)elden_usb_log_putc(*p);}
void worker(void *,wait_result_t) {
    for(unsigned i=0;i<12000 && (!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted);++i)IOSleep(10);
    if(!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted) {
        elden_wifi_fw_stats[0]=-1;report("\nELDEN_WIFI_FW refused root/recovery gate\n");thread_terminate(current_thread());return;
    }
    EldenWifiFiles *fs=(EldenWifiFiles *)IOMallocData(sizeof(EldenWifiFiles));
    uint8_t *buffer=(uint8_t *)IOMallocData(65536);
    if(!fs || !buffer) {
        if(fs)IOFreeData(fs,sizeof(*fs));if(buffer)IOFreeData(buffer,65536);
        elden_wifi_fw_stats[0]=-2;thread_terminate(current_thread());return;
    }
    const EldenWifiFileIo io={nullptr,find,read,ELDEN_WIFI_MODEM_FIRST,ELDEN_WIFI_MODEM_BLOCKS};
    elden_wifi_files_init(fs,&io);elden_wifi_fw_stats[0]=1;
    for(unsigned i=0;i<sizeof(elden_wifi_firmware_inputs)/sizeof(elden_wifi_firmware_inputs[0]);++i) {
        const auto &expected=elden_wifi_firmware_inputs[i];EldenWifiFile file={};
        int rc=elden_wifi_file_open(fs,expected.path,&file);
        if(!rc && file.file.size!=expected.bytes)rc=-20;
        SHA256_CTX hash;SHA256_Init(&hash);
        for(uint64_t offset=0;!rc && offset<expected.bytes;) {
            size_t n=expected.bytes-offset>65536?65536:(size_t)(expected.bytes-offset);
            rc=elden_wifi_file_read(&file,offset,buffer,n);if(rc)break;
            SHA256_Update(&hash,buffer,n);offset+=n;IOSleep(1);
        }
        char hex[65]={};
        if(!rc) {
            uint8_t digest[32];SHA256_Final(digest,&hash);
            for(unsigned j=0;j<32;++j){hex[j*2]="0123456789abcdef"[digest[j]>>4];hex[j*2+1]="0123456789abcdef"[digest[j]&15];}
            if(strcmp(hex,expected.sha256))rc=-21;
        }
        char line[240];snprintf(line,sizeof(line),
            "\nELDEN_WIFI_FW file=%s status=%d bytes=%u SHA=%s READ_ONLY=1 radio=0\n",
            expected.path,rc,file.file.size,rc?"unqualified":hex);report(line);
        elden_wifi_fw_stats[1]=(int32_t)i;elden_wifi_fw_stats[2]=rc;elden_wifi_fw_stats[3]=(int32_t)fs->reads;
        if(rc){elden_wifi_fw_stats[0]=-3;break;}
        elden_wifi_fw_stats[4]=(int32_t)(i+1);
    }
    if(elden_wifi_fw_stats[0]>0)elden_wifi_fw_stats[0]=2;
    IOFreeData(buffer,65536);IOFreeData(fs,sizeof(*fs));thread_terminate(current_thread());
}
}
extern "C" void elden_wifi_fw_check_start(void) {
    unsigned mode=0;PE_parse_boot_argn("elden-wifi-fw-check",&mode,sizeof(mode));
    if(mode!=1 || __atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)!=KERN_SUCCESS){elden_wifi_fw_stats[0]=-2;return;}
    thread_deallocate(thread);
}
