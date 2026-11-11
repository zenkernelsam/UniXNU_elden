/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FAT lookup and partial-block reads follow the original sheng ADSP reader.
 * This board adapter has its own cache and no disk, GPIO, PCIe or PMIC writes.
 */
#include "ModemFiles.h"
#include <string.h>

int elden_wifi_file_path_allowed(const char *path) {
    static const char *const paths[]={"image/peach/amss20.bin","image/peach/bdwlan.elf",
        "image/peach/regdb.bin","image/peach/phy_ucode20.elf","image/peach/aux_ucode20.elf",
        "image/peach/data20.msc","image/peach/qdss_trace_config_v2.cfg","image/tmel_peach_20.elf"};
    if(!path)return 0;
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i)if(!strcmp(path,paths[i]))return 1;
    /* No automatic BDF selection: only read a specifically named OEM eXX file.
     * This permits later QMI-selected board data without arbitrary path access. */
    const char *prefix="image/peach/bdwlan.e";const size_t n=strlen(prefix);
    if(strncmp(path,prefix,n) || strlen(path)!=n+2)return 0;
    int value=0;
    for(unsigned i=0;i<2;++i) {
        char c=path[n+i];int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
        if(digit<0)return 0;
        value=value*16+digit;
    }
    return value>=1 && value<=0x41;
}
void elden_wifi_files_init(EldenWifiFiles *f,const EldenWifiFileIo *io) {
    memset(f,0,sizeof(*f));if(io)f->io=*io;f->cached=UINT64_MAX;
}
static int poison(EldenWifiFiles *f,int rc) {
    f->poisoned=1;f->cached=UINT64_MAX;f->fat.cached_sector=UINT64_MAX;
    f->last_error=rc;return rc;
}
int elden_wifi_files_partition_read(void *context,uint64_t offset,void *dst,size_t bytes) {
    EldenWifiFiles *f=context;
    if(!f || !f->io.read || (!dst && bytes) || !f->blocks || f->blocks>UINT64_MAX/4096)return EldenWifiFileArgument;
    if(f->poisoned)return EldenWifiFilePoisoned;
    const uint64_t limit=f->blocks*4096;
    if(offset>limit || bytes>limit-offset)return EldenWifiFileArgument;
    uint8_t *out=dst;
    while(bytes) {
        const uint64_t lba=offset/4096;const size_t within=(size_t)(offset%4096);
        if(!within && bytes>=4096) {
            const uint32_t count=bytes/4096>ELDEN_WIFI_RUN_BLOCKS?ELDEN_WIFI_RUN_BLOCKS:(uint32_t)(bytes/4096);
            if(!f->io.read(f->io.context,f->lun,f->first+lba,count,out))return poison(f,EldenWifiFileReadError);
            ++f->reads;out+=(size_t)count*4096;offset+=(uint64_t)count*4096;bytes-=(size_t)count*4096;
        } else {
            if(f->cached!=lba) {
                f->cached=UINT64_MAX;
                if(!f->io.read(f->io.context,f->lun,f->first+lba,1,f->block))return poison(f,EldenWifiFileReadError);
                ++f->reads;f->cached=lba;
            }
            const size_t count=4096-within<bytes?4096-within:bytes;
            memcpy(out,f->block+within,count);out+=count;offset+=count;bytes-=count;
        }
    }
    return 0;
}
int elden_wifi_files_mount(EldenWifiFiles *f) {
    if(!f || !f->io.find || !f->io.read || !f->io.expected_blocks)return EldenWifiFileArgument;
    if(f->poisoned)return EldenWifiFilePoisoned;
    if(f->mounted)return 0;
    uint32_t lun=0;uint64_t first=0,blocks=0;
    if(!f->io.find(f->io.context,"modem_a",&lun,&first,&blocks))return EldenWifiFileNotReady;
    if(lun!=4 || first!=f->io.expected_first || blocks!=f->io.expected_blocks ||
        blocks>UINT64_MAX/4096 || first>UINT64_MAX-blocks)return EldenWifiFileIdentity;
    f->lun=lun;f->first=first;f->blocks=blocks;
    int rc=fat_mount(&f->fat,blocks*4096,elden_wifi_files_partition_read,f);
    if(rc)return poison(f,f->poisoned?EldenWifiFileReadError:EldenWifiFileGeometry);
    if(f->fat.sector_bytes!=4096 || f->fat.fat32)return poison(f,EldenWifiFileGeometry);
    f->mounted=1;return 0;
}
int elden_wifi_file_open(EldenWifiFiles *f,const char *path,EldenWifiFile *out) {
    if(!f || !out || !elden_wifi_file_path_allowed(path))return EldenWifiFileArgument;
    int rc=elden_wifi_files_mount(f);if(rc)return rc;
    FatFile file;rc=fat_open(&f->fat,path,&file);
    if(rc==FatNotFound)return EldenWifiFileAbsent;
    if(rc)return poison(f,EldenWifiFileReadError);
    if(file.directory)return EldenWifiFileArgument;
    if(!file.size || file.size>ELDEN_WIFI_FILE_MAX)return EldenWifiFileTooBig;
    *out=(EldenWifiFile){f,file};return 0;
}
int elden_wifi_file_read(EldenWifiFile *file,uint64_t offset,void *dst,size_t bytes) {
    if(!file || !file->owner || (!dst && bytes) || offset>file->file.size || bytes>file->file.size-offset)
        return EldenWifiFileArgument;
    EldenWifiFiles *f=file->owner;if(f->poisoned)return EldenWifiFilePoisoned;
    int rc=fat_read(&f->fat,&file->file,offset,dst,bytes);
    if(rc)return poison(f,EldenWifiFileReadError);
    return 0;
}
