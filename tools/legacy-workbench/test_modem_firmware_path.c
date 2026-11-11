/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual FAT -> split MDT -> logical MBN path against the original modem backup. */
#include "FatVolume.h"
#include "QcomMdt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *disk;
static uint64_t disk_bytes,max_read;
static FatVolume volume;
struct image {QcomMdtSplit split;FatFile pieces[QCOM_MDT_MAX_SEGMENTS];};
static int read_disk(void *unused,uint64_t offset,void *out,size_t size) {
    (void)unused;
    if(offset>disk_bytes || size>disk_bytes-offset)return -1;
    if(offset+size>max_read)max_read=offset+size;
    return fseeko(disk,(off_t)offset,SEEK_SET) || fread(out,1,size,disk)!=size ? -1:0;
}
static int read_piece(void *ctx,unsigned piece,uint64_t offset,void *out,size_t size) {
    struct image *image=ctx;
    if(piece>=image->split.count)return -1;
    return fat_read(&volume,&image->pieces[piece],offset,out,size);
}
static int image(const char *name,const char *output) {
    char path[64];FatFile header;snprintf(path,sizeof(path),"image/%s.b00",name);
    if(fat_open(&volume,path,&header) || header.size>65536)return -1;
    uint8_t *bytes=malloc(header.size);struct image *image=calloc(1,sizeof(*image));
    if(!bytes||!image)return -1;
    int result=fat_read(&volume,&header,0,bytes,header.size) || qcom_mdt_split(&image->split,bytes,header.size);
    free(bytes);if(result){free(image);return -2;}
    for(unsigned i=0;i<image->split.count;i++) {
        if(!image->split.piece[i].size)continue;
        snprintf(path,sizeof(path),"image/%s.b%02u",name,i);
        if(fat_open(&volume,path,&image->pieces[i]) || image->pieces[i].size<image->split.piece[i].size){free(image);return -3;}
    }
    uint8_t *whole=malloc((size_t)image->split.size);if(!whole){free(image);return -4;}
    if(qcom_mdt_split_read(&image->split,read_piece,image,0,whole,(size_t)image->split.size)){free(whole);free(image);return -5;}
    FILE *out=fopen(output,"wb");if(!out){free(whole);free(image);return -6;}
    result=fwrite(whole,1,(size_t)image->split.size,out)!=image->split.size || fclose(out);
    printf("%s: logical=%llu pieces=%u FAT-sector=%u read-bound=%llu/%llu\n",name,
        (unsigned long long)image->split.size,image->split.count,volume.sector_bytes,
        (unsigned long long)max_read,(unsigned long long)disk_bytes);
    free(whole);free(image);return result?-7:0;
}
int main(int argc,char **argv) {
    if(argc!=4 || !(disk=fopen(argv[1],"rb")))return 1;
    if(fseeko(disk,0,SEEK_END))return 2;
    off_t size=ftello(disk);if(size<=0)return 3;disk_bytes=(uint64_t)size;
    if(fat_mount(&volume,disk_bytes,read_disk,NULL))return 4;
    int a=image("adsp",argv[2]),b=image("adsp_dtb",argv[3]);fclose(disk);
    if(a||b){fprintf(stderr,"firmware path errors: %d/%d\n",a,b);return 5;}
    puts("PASS: native FAT/MDT logical firmware reads; no device operations");return 0;
}
