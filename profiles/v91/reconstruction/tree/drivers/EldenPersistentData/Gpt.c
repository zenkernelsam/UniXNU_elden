/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Gpt.h"
#include "EldenDataBoard.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t le64(const uint8_t *p){return le32(p)|(uint64_t)le32(p+4)<<32;}
static uint32_t crc(const uint8_t *p,size_t n,bool header){
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<n;i++){c^=header&&i>=16&&i<20?0:p[i];for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
    return ~c;
}
int elden_data_gpt_header(const uint8_t *p,size_t n,uint64_t last_lba,bool backup,EldenDataGptHeader *out){
    if(!p||!out||n!=4096||memcmp(p,"EFI PART",8)||le32(p+8)!=0x10000||le32(p+20))return EldenDataGptHeaderError;
    uint32_t size=le32(p+12);if(size<92||size>4096||crc(p,size,true)!=le32(p+16))return EldenDataGptHeaderError;
    EldenDataGptHeader h={0};h.current=le64(p+24);h.backup=le64(p+32);h.first=le64(p+40);h.last=le64(p+48);
    h.entries=le64(p+72);h.count=le32(p+80);h.entries_crc=le32(p+88);memcpy(h.disk,p+56,16);
    if(last_lba>UINT32_MAX||last_lba<10||h.current!=(backup?last_lba:1)||h.backup!=(backup?1:last_lba)||
       !h.count||h.count>128||le32(p+84)!=128||h.first<6||h.last<h.first||h.last>=last_lba)return EldenDataGptHeaderError;
    uint64_t blocks=((uint64_t)h.count*128+4095)/4096;
    if(backup?(h.entries<=h.last||h.entries>=last_lba||blocks>last_lba-h.entries):
              (h.entries<2||h.entries>=h.first||blocks>h.first-h.entries))return EldenDataGptHeaderError;
    *out=h;return 0;
}
int elden_data_gpt_pair(const EldenDataGptHeader *p,const uint8_t *a,size_t an,
                        const EldenDataGptHeader *q,const uint8_t *b,size_t bn){
    if(!p||!q||!a||!b||p->current!=q->backup||p->backup!=q->current||p->first!=q->first||p->last!=q->last||
       p->count!=q->count||p->entries_crc!=q->entries_crc||memcmp(p->disk,q->disk,16))return EldenDataGptPair;
    size_t bytes=(size_t)p->count*128;if(an<bytes||bn<bytes||memcmp(a,b,bytes)||crc(a,bytes,false)!=p->entries_crc)return EldenDataGptEntries;
    unsigned targets=0;
    for(unsigned i=0;i<p->count;i++){
        const uint8_t *e=a+i*128;unsigned used=0;for(unsigned k=0;k<16;k++)used|=e[k];if(!used)continue;
        uint64_t first=le64(e+32),last=le64(e+40);
        if(first<p->first||last>p->last||last<first)return EldenDataGptEntries;
        for(unsigned j=0;j<i;j++){
            const uint8_t *f=a+j*128;unsigned present=0;for(unsigned k=0;k<16;k++)present|=f[k];if(!present)continue;
            uint64_t ffirst=le64(f+32),flast=le64(f+40);
            if((first<=flast&&ffirst<=last)||!memcmp(e+16,f+16,16))return EldenDataGptEntries;
        }
        static const uint8_t name[12]={'i',0,'p',0,'a',0,'d',0,'o',0,'s',0};
        if(!memcmp(e+56,name,sizeof(name))&&e[68]==0&&e[69]==0){
            static const uint8_t type[16]={0xef,0x57,0x34,0x7c,0,0,0xaa,0x11,0xaa,0x11,0,0x30,0x65,0x43,0xec,0xac};
            if(++targets>1||first!=ELDEN_DATA_FIRST||last!=ELDEN_DATA_LAST||le64(e+48)||
               memcmp(e,type,16)||memcmp(e+16,elden_data_partition_guid,16))return EldenDataGptTarget;
            for(unsigned k=68;k<128;k++)if(e[k])return EldenDataGptTarget;
        }
    }
    return targets==1?0:EldenDataGptTarget;
}
