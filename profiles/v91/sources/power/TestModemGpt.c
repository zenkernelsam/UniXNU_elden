/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "EldenModemGpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks; if (!(x)) {fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t le32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t le64(const uint8_t *p) {return le32(p)|(uint64_t)le32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static void put64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;i++)p[i]=v>>(8*i);}
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t v=UINT32_MAX;
    for(size_t i=0;i<n;i++){v^=p[i];for(unsigned b=0;b<8;b++)v=(v>>1)^(0xedb88320u&(0u-(v&1)));}
    return ~v;
}
static void header_crc(uint8_t *p) {put32(p+16,0);put32(p+16,crc(p,le32(p+12)));}
static void header_refused(uint8_t *p,size_t n,uint64_t last) {
    EldenModemGptHeader a,b;memset(&a,0xa5,sizeof(a));memcpy(&b,&a,sizeof(a));
    CHECK(elden_modem_gpt_header(p,n,last,&a)!=0);CHECK(!memcmp(&a,&b,sizeof(a)));
}
static void entries_refused(EldenModemGptHeader *h,uint8_t *p,size_t n) {
    EldenModemPartition a,b;memset(&a,0xa5,sizeof(a));memcpy(&b,&a,sizeof(a));
    CHECK(elden_modem_gpt_partition(h,p,n,&a)!=0);CHECK(!memcmp(&a,&b,sizeof(a)));
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);
    uint8_t original[24576];CHECK(fread(original,1,sizeof(original),f)==sizeof(original));CHECK(fclose(f)==0);
    uint8_t *block=original+4096;uint64_t last=le64(block+32);EldenModemGptHeader header;
    CHECK(elden_modem_gpt_header(block,4096,last,&header)==0);
    size_t bytes=header.entry_count*128;CHECK(header.entries_lba*4096+bytes<=sizeof(original));
    uint8_t *entries=original+header.entries_lba*4096;EldenModemPartition partition;
    CHECK(elden_modem_gpt_partition(&header,entries,bytes,&partition)==0);
    CHECK(partition.first>=header.first_usable && partition.first+partition.blocks-1<=header.last_usable);
    for(size_t n=0;n<4096;n+=31)header_refused(block,n,last);
    for(size_t n=0;n<bytes;n+=127)entries_refused(&header,entries,n);
    header_refused(block,4096,UINT32_MAX);
    uint8_t bad[4096];memcpy(bad,block,4096);bad[56]^=1;header_refused(bad,4096,last);
    memcpy(bad,block,4096);put32(bad+84,129);header_crc(bad);header_refused(bad,4096,last);
    memcpy(bad,block,4096);put32(bad+80,129);header_crc(bad);header_refused(bad,4096,last);
    memcpy(bad,block,4096);put64(bad+72,last);header_crc(bad);header_refused(bad,4096,last);
    memcpy(bad,block,4096);put64(bad+48,last);header_crc(bad);header_refused(bad,4096,last);
    uint8_t *modified=malloc(bytes);CHECK(modified!=NULL);memcpy(modified,entries,bytes);
    modified[17]^=1;entries_refused(&header,modified,bytes);
    unsigned found=0,empty=0;
    for(unsigned i=0;i<header.entry_count;i++) {
        uint8_t *e=entries+i*128;
        if(le64(e+32)==partition.first && le64(e+40)+1==partition.first+partition.blocks)found=i;
        if(!memcmp(e,(uint8_t[16]){0},16))empty=i;
    }
    CHECK(found!=empty);memcpy(modified,entries,bytes);memcpy(modified+empty*128,entries+found*128,128);
    EldenModemGptHeader mh=header;mh.entries_crc=crc(modified,bytes);entries_refused(&mh,modified,bytes);
    memcpy(modified,entries,bytes);put64(modified+found*128+40,header.last_usable+1);
    mh.entries_crc=crc(modified,bytes);entries_refused(&mh,modified,bytes);
    memcpy(modified,entries,bytes);memset(modified+found*128,0,16);
    mh.entries_crc=crc(modified,bytes);entries_refused(&mh,modified,bytes);
    free(modified);
    printf("PASS: %u checks; actual LUN4 GPT modem_a=%llu+%llu; no device operations\n",checks,
        (unsigned long long)partition.first,(unsigned long long)partition.blocks);
    return 0;
}
