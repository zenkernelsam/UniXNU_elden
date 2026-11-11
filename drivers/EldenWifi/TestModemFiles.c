/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define _POSIX_C_SOURCE 200809L
#include "ModemFiles.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {FILE *file;unsigned finds,reads,fail,absent,bad_identity;} Host;
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static int find(void *p,const char *name,uint32_t *lun,uint64_t *first,uint64_t *blocks) {
    Host *h=p;++h->finds;CHECK(!strcmp(name,"modem_a"));
    if(h->absent)return 0;
    *lun=h->bad_identity?0:4;*first=4870;*blocks=89600;return 1;
}
static int read_blocks(void *p,uint32_t lun,uint64_t lba,uint32_t blocks,void *out) {
    Host *h=p;++h->reads;CHECK(lun==4 && lba>=4870 && lba-4870+blocks<=89600 && blocks>=1 && blocks<=256);
    if(h->fail)return 0;
    return !fseeko(h->file,(off_t)((lba-4870)*4096),SEEK_SET) && fread(out,4096,blocks,h->file)==blocks;
}
int main(int argc,char **argv) {
    CHECK(argc==3);Host h={0};h.file=fopen(argv[1],"rb");CHECK(h.file);
    EldenWifiFileIo io={&h,find,read_blocks,4870,89600};EldenWifiFiles *f=calloc(1,sizeof(*f));CHECK(f);
    elden_wifi_files_init(f,&io);EldenWifiFile file={0};
    const char *paths[]={"image/peach/amss20.bin","image/peach/phy_ucode20.elf",
        "image/peach/aux_ucode20.elf","image/tmel_peach_20.elf","image/peach/data20.msc",
        "image/peach/regdb.bin","image/peach/bdwlan.elf"};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i) {
        CHECK(!elden_wifi_file_open(f,paths[i],&file));
        unsigned char *b=malloc(file.file.size);CHECK(b);
        /* Exercise short edges and large multi-block runs. */
        CHECK(!elden_wifi_file_read(&file,0,b,17));
        CHECK(!elden_wifi_file_read(&file,17,b+17,file.file.size-17));
        char dest[256];CHECK(snprintf(dest,sizeof(dest),"%s/%u.bin",argv[2],i)>0);
        FILE *out=fopen(dest,"wb");CHECK(out);
        CHECK(fwrite(b,1,file.file.size,out)==file.file.size);fclose(out);free(b);
        unsigned before=h.reads;CHECK(elden_wifi_file_read(&file,file.file.size,0,1)==EldenWifiFileArgument);
        CHECK(h.reads==before);
    }
    CHECK(elden_wifi_file_path_allowed("image/peach/bdwlan.e01"));
    CHECK(elden_wifi_file_path_allowed("image/peach/bdwlan.e41"));
    const char *bad[]={"image/peach/bdwlan.e00","image/peach/bdwlan.e42","image/peach/bdwlan.e1",
        "image/peach/bdwlan.e0z","../image/peach/amss20.bin","image/peach/../adsp.b00",
        "image/adsp.b00","image/peach/amss.bin","image/tmel_peach_10.elf"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        unsigned before=h.reads;EldenWifiFile unchanged=file;
        CHECK(elden_wifi_file_open(f,bad[i],&unchanged)==EldenWifiFileArgument);
        CHECK(h.reads==before && !memcmp(&file,&unchanged,sizeof(file)));
    }
    EldenWifiFiles *second=calloc(1,sizeof(*second));CHECK(second);elden_wifi_files_init(second,&io);
    EldenWifiFile other;CHECK(!elden_wifi_file_open(second,paths[1],&other));
    uint8_t a[53],b[53];
    CHECK(!elden_wifi_file_read(&file,11,a,sizeof(a)));CHECK(!elden_wifi_file_read(&other,37,b,sizeof(b)));
    CHECK(!elden_wifi_file_read(&file,11,b,sizeof(b)) && !memcmp(a,b,sizeof(a)));
    CHECK(elden_wifi_files_partition_read(f,89600ull*4096,a,1)==EldenWifiFileArgument);
    h.fail=1;f->cached=UINT64_MAX;f->fat.cached_sector=UINT64_MAX;
    CHECK(elden_wifi_file_read(&file,100,a,sizeof(a))==EldenWifiFileReadError);
    unsigned before=h.reads;CHECK(elden_wifi_file_read(&file,100,a,sizeof(a))==EldenWifiFilePoisoned && h.reads==before);
    h.fail=0;h.absent=1;elden_wifi_files_init(second,&io);before=h.finds;
    CHECK(elden_wifi_files_mount(second)==EldenWifiFileNotReady && h.finds==before+1);
    h.absent=0;h.bad_identity=1;before=h.reads;
    CHECK(elden_wifi_files_mount(second)==EldenWifiFileIdentity && h.reads==before);
    fclose(h.file);free(second);free(f);
    printf("PASS real modem Wi-Fi files: %u checks,7 exact outputs, range/identity/isolated caches/quarantine\n",checks);
    return 0;
}
