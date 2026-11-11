/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Development dispatcher: same qualified root/Data/recovery gate, with each
 * power operation explicitly selected. Firmware check performs only UFS reads.
 * No automatic charger/UCSI control setting is made here.
 */
#if defined(ELDEN_POWER_RUNTIME) && ELDEN_POWER_RUNTIME
extern "C" {
#include "QcomMdt.h"
void sheng_adsp_probe(void);
void elden_pmic_client_start(void);
#if defined(ELDEN_BACKLIGHT_RUNTIME) && ELDEN_BACKLIGHT_RUNTIME
void elden_backlight_runtime_start(void);
#endif
int sheng_fw_open(const char *,void **,uint64_t *);
int sheng_fw_read(void *,uint64_t,void *,size_t);
void sheng_fw_close(void *);
int elden_recovery_armed(void);
extern volatile int32_t native_adsp_stats[16];
}
namespace {
static unsigned gEldenPowerAttempted;
static int eldenMdtRead(void *handle,uint64_t offset,void *data,size_t size) {
    return sheng_fw_read(handle,offset,data,size);
}
static void eldenFirmwareCheck(void *,wait_result_t) {
    static const struct {const char *name;uint64_t bytes,region,region_size;const char *sha;} images[]={
        {"modem:adsp",23066968,0x9ee00000,0x3a80000,"9edad6f458caa609a189b916694eecec99bb0c01fa8d2bd02c630552a7dbba75"},
        {"modem:adsp_dtb",167736,0x9ed80000,0x80000,"72b99f5c0cfc748be5d212a2ee7c590639bacf0e13ddac0a8be97052895584d2"}
    };
    uint8_t *bounce=static_cast<uint8_t *>(IOMalloc(65536));
    QcomMdtImage *mdt=static_cast<QcomMdtImage *>(IOMalloc(sizeof(QcomMdtImage)));
    if(!bounce||!mdt) {
        if(bounce)IOFree(bounce,65536);if(mdt)IOFree(mdt,sizeof(QcomMdtImage));
        IOLog("ELDEN_FIRMWARE_CHECK refused allocation\n");thread_terminate(current_thread());return;
    }
    for(const auto &image:images) {
        void *handle=nullptr;uint64_t size=0;
        int result=sheng_fw_open(image.name,&handle,&size);
        if(!result && size!=image.bytes)result=-20;
        if(!result)result=qcom_mdt_parse(mdt,size,eldenMdtRead,handle);
        if(!result) {
            for(unsigned i=0;i<mdt->count;++i) {
                if(!mdt->segment[i].load)continue;
                uint64_t offset=0;
                if(qcom_mdt_place(mdt,i,image.region,image.region_size,&offset)) {result=-21;break;}
            }
        }
        SHA256_CTX hash;SHA256_Init(&hash);
        for(uint64_t offset=0;!result&&offset<size;) {
            size_t bytes=size-offset>65536?65536:static_cast<size_t>(size-offset);
            if(sheng_fw_read(handle,offset,bounce,bytes)) {result=-22;break;}
            SHA256_Update(&hash,bounce,bytes);offset+=bytes;IOSleep(1);
        }
        char hex[65];uint8_t digest[32];
        if(!result) {
            SHA256_Final(digest,&hash);
            for(unsigned i=0;i<32;++i){hex[2*i]="0123456789abcdef"[digest[i]>>4];hex[2*i+1]="0123456789abcdef"[digest[i]&15];}
            hex[64]=0;if(strcmp(hex,image.sha))result=-23;
        }
        IOLog("ELDEN_FIRMWARE_CHECK name=%s status=%d bytes=%llu SHA=%s writes=0 PAS=0\n",
            image.name,result,size,result?"unqualified":hex);
        if(handle)sheng_fw_close(handle);
        if(result)break;
    }
    IOFree(mdt,sizeof(QcomMdtImage));IOFree(bounce,65536);
    thread_terminate(current_thread());
}
static void eldenAdspMonitor(void *,wait_result_t) {
    int32_t previous=INT32_MIN;
    for(unsigned i=0;i<1200;++i) {
        const int32_t stage=native_adsp_stats[0];
        if(stage!=previous) {
            previous=stage;
            IOLog("ELDEN_ADSP stage=%d error=%d slave=%x items=%x QMP=%x RSC=%x SCM=%x/%x irq=%u\n",
                stage,native_adsp_stats[1],native_adsp_stats[3],native_adsp_stats[4],native_adsp_stats[5],
                native_adsp_stats[6],native_adsp_stats[13],native_adsp_stats[14],native_adsp_stats[2]);
        }
        IOSleep(100);
    }
    thread_terminate(current_thread());
}
static void eldenPowerStart() {
    if(!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted)return;
#if defined(ELDEN_BACKLIGHT_RUNTIME) && ELDEN_BACKLIGHT_RUNTIME
    elden_backlight_runtime_start();
#endif
    unsigned adsp=0,pmic=0,check=0;
    PE_parse_boot_argn("elden-adsp",&adsp,sizeof(adsp));PE_parse_boot_argn("elden-pmic",&pmic,sizeof(pmic));
    PE_parse_boot_argn("elden-modem-fw-check",&check,sizeof(check));
    if(adsp>2 || pmic>1 || check>1 || (pmic && adsp!=2) || (check && adsp))return;
    if(!adsp&&!check)return;
    if(__atomic_exchange_n(&gEldenPowerAttempted,1u,__ATOMIC_ACQ_REL))return;
    if(check) {
        thread_t thread=nullptr;
        if(kernel_thread_start(eldenFirmwareCheck,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
        else IOLog("ELDEN_FIRMWARE_CHECK refused worker creation\n");
    }
    if(adsp) {
        sheng_adsp_probe();
        if(pmic)elden_pmic_client_start();
        thread_t thread=nullptr;
        if(kernel_thread_start(eldenAdspMonitor,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    }
}
}
#else
static inline void eldenPowerStart() {}
#endif
