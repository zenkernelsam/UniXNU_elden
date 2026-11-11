// SPDX-License-Identifier: BSD-2-Clause-Patent
// Checks real stock firmware with the same QcomMdt core planned for the loader.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "QcomMdt.h"
typedef struct {FILE *file;} Input;
static int read_image(void *context,uint64_t offset,void *to,size_t length){
    Input *input=context;
    if(fseek(input->file,(long)offset,SEEK_SET))return -1;
    return fread(to,1,length,input->file)==length?0:-1;
}
int main(int argc,char **argv){
    if(argc!=5)return 2;
    Input input={fopen(argv[1],"rb")};if(!input.file)return 3;
    if(fseek(input.file,0,SEEK_END))return 4;
    long bytes=ftell(input.file);if(bytes<0)return 5;
    QcomMdtImage image;int rc=qcom_mdt_parse(&image,(uint64_t)bytes,read_image,&input);
    if(rc){fprintf(stderr,"parser %d\n",rc);return 6;}
    const uint64_t base=strtoull(argv[3],NULL,0),capacity=strtoull(argv[4],NULL,0);
    unsigned loads=0;
    for(unsigned i=0;i<image.count;++i){
        if(!image.segment[i].load)continue;
        uint64_t offset;rc=qcom_mdt_place(&image,i,base,capacity,&offset);
        if(rc){fprintf(stderr,"outside segment %u rc %d\n",i,rc);return 7;}
        ++loads;
    }
    uint64_t length=qcom_mdt_metadata_size(&image);if(!length || length>1024*1024)return 8;
    uint8_t *generated=malloc((size_t)length),*original=malloc((size_t)length);
    if(!generated || !original)return 9;
    if(qcom_mdt_metadata(&image,read_image,&input,generated,length))return 10;
    FILE *metadata=fopen(argv[2],"rb");if(!metadata)return 11;
    if(fread(original,1,(size_t)length,metadata)!=length || fgetc(metadata)!=EOF || memcmp(original,generated,(size_t)length))return 12;
    printf("PASS: PHDR=%u loadable=%u span=%llu metadata=%llu original bytes match; region=0x%llx+0x%llx\n",image.count,loads,
           (unsigned long long)qcom_mdt_span(&image),(unsigned long long)length,(unsigned long long)base,(unsigned long long)capacity);
    fclose(metadata);fclose(input.file);free(generated);free(original);return 0;
}
