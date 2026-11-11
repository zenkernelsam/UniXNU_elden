/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "FatVolume.h"
#ifdef __cplusplus
extern "C" {
#endif
#define ELDEN_WIFI_BLOCK_BYTES 4096u
#define ELDEN_WIFI_RUN_BLOCKS 256u
#define ELDEN_WIFI_FILE_MAX (32u<<20)
enum {EldenWifiFileOk=0,EldenWifiFileArgument=-1,EldenWifiFileNotReady=-2,
    EldenWifiFileIdentity=-3,EldenWifiFileGeometry=-4,EldenWifiFileReadError=-5,
    EldenWifiFileAbsent=-6,EldenWifiFileTooBig=-7,EldenWifiFilePoisoned=-8};
typedef struct {
    void *context;
    int (*find)(void *,const char *,uint32_t *,uint64_t *,uint64_t *);
    int (*read)(void *,uint32_t,uint64_t,uint32_t,void *);
    uint64_t expected_first,expected_blocks;
} EldenWifiFileIo;
/* One executor per context. Independent FAT/block caches coexist with the
 * ADSP reader; all physical reads still use the one native UFS owner/lock.
 * A storage/chain failure quarantines this context until a new boot.
 */
typedef struct {
    EldenWifiFileIo io;
    FatVolume fat;
    uint32_t lun,reads;
    uint64_t first,blocks,cached;
    uint8_t block[ELDEN_WIFI_BLOCK_BYTES];
    int mounted,poisoned,last_error;
} EldenWifiFiles;
typedef struct {EldenWifiFiles *owner;FatFile file;} EldenWifiFile;
void elden_wifi_files_init(EldenWifiFiles *,const EldenWifiFileIo *);
int elden_wifi_files_mount(EldenWifiFiles *);
int elden_wifi_file_open(EldenWifiFiles *,const char *,EldenWifiFile *);
int elden_wifi_file_read(EldenWifiFile *,uint64_t,void *,size_t);
int elden_wifi_file_path_allowed(const char *);
/* Exposed only for range/fault tests; offset is relative to modem_a. */
int elden_wifi_files_partition_read(void *,uint64_t,void *,size_t);
#ifdef __cplusplus
}
#endif
