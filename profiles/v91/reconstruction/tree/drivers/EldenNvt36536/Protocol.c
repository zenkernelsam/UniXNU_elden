/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TB322FC NT36536 framing/map/no-flash upload adapted from h0cheung's
 * novatek-nvt36xxx-spi.c@6f97250ead155fc3b980856323e99a4623dae73d.
 * Bounds, atomic preflight and bus quarantine belong to this board adapter.
 * GPIO/clock/SMMU/interrupt ownership remain with the platform owner.
 */
#include "Protocol.h"
#include <string.h>
#define ENG_RESET 0x7fff80u
#define SW_RESET 0x1fb43eu
#define TRIM 0x1fb104u
#define CASCADE 0x1fb12cu
#define BOOT_READY 0x1fb50du
#define AUTO_COPY 0x1fc825u
#define ILM_DES 0x1fcc30u
#define ILM_LEN 0x1fcc34u
#define ILM_CRC 0x1fcc38u
#define DLM_DES 0x1fcc40u
#define DLM_LEN 0x1fcc44u
#define DLM_CRC 0x1fcc48u
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t crc_words(const uint8_t *p,uint32_t size){
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<(size+4)/4;i++){
        crc^=le32(p+i*4);
        for(unsigned bit=0;bit<32;bit++)crc=(crc&0x80000000u)?(crc<<1)^0x04c11db7u:crc<<1;
    }
    return crc;
}
static int partition(EldenNvtImage *im,uint32_t file,uint32_t ram,uint32_t size,uint32_t crc){
    if(!size)return 0;
    if(im->count==ELDEN_NVT_FW_PARTITIONS||file>=im->bytes||size>=im->bytes-file||
       ram>0x7fffff||size>0x7fffff-ram)return EldenNvtFirmware;
    im->part[im->count++]=(EldenNvtPartition){file,ram,size,crc};return 0;
}
int elden_nvt_image(EldenNvtImage *out,const uint8_t *data,size_t bytes){
    if(!out||!data)return EldenNvtArgument;
    if(bytes<4096||bytes>1024*1024||memcmp(data+bytes-3,"NVT",3)||
       (unsigned)data[bytes-4096]+data[bytes-4095]!=255)return EldenNvtFirmware;
    uint32_t end=le32(data),limit=end;
    if(end<48||end>bytes||end%16)return EldenNvtFirmware;
    if((data[0x28]&16)||((data[0x20]&2)&&(data[0x29]&1)))return EldenNvtUnsupported;
    EldenNvtImage image;memset(&image,0,sizeof(image));image.data=data;image.bytes=bytes;image.cascade=(data[0x20]&2)!=0;
    for(uint32_t p=48;p<=end-16;p+=16){if(le32(data+p+4)&&!le32(data+p+8)){limit=p+16;break;}}
    if(image.cascade&&limit>end/2)return EldenNvtFirmware;
    for(unsigned i=0;i<2;i++){
        uint32_t file=le32(data+i*12),ram=le32(data+i*12+4),size=le32(data+i*12+8),crc=le32(data+24+i*4);
        if(!size||partition(&image,file,ram,size,crc))return EldenNvtFirmware;
        uint64_t rounded=(uint64_t)((size+4)/4)*4;
        if(file>bytes||rounded>bytes-file||crc_words(data+file,size)!=crc)return EldenNvtFirmware;
    }
    for(uint32_t p=48;p<=limit-16;p+=16){
        if(partition(&image,le32(data+p+8),le32(data+p),le32(data+p+4),le32(data+p+12)))return EldenNvtFirmware;
    }
    if(image.cascade){
        uint32_t p=2*limit-16;
        if(p>end-16||!le32(data+p+4)||le32(data+p+8)!=limit||
           partition(&image,le32(data+p+8),le32(data+p),le32(data+p+4),le32(data+p+12)))return EldenNvtFirmware;
    }
    if(image.count<2)return EldenNvtFirmware;
    *out=image;return 0;
}
static int valid(EldenNvtDevice *d){
    return d&&d->transfer&&d->delay_us&&d->tx&&d->rx&&d->tx_bytes>=ELDEN_NVT_FW_CHUNK+1&&d->rx_bytes>=110;
}
static int transfer(EldenNvtDevice *d,size_t bytes,bool receive){
    if(!valid(d)||bytes>d->tx_bytes||(receive&&bytes>d->rx_bytes))return EldenNvtArgument;
    if(d->poisoned)return EldenNvtBus;
    int error=d->transfer(d->context,d->tx,receive?d->rx:NULL,bytes);
    if(error){d->poisoned=true;d->ready=false;d->error=error;return error;}
    return 0;
}
static int page(EldenNvtDevice *d,uint32_t address){
    if(!valid(d)||address>0x7fffff)return EldenNvtArgument;
    d->tx[0]=0xff;d->tx[1]=address>>15;d->tx[2]=address>>7;return transfer(d,3,false);
}
static int write(EldenNvtDevice *d,uint32_t address,const uint8_t *data,size_t bytes){
    if(!data||!bytes||bytes>ELDEN_NVT_FW_CHUNK||address>0x7fffff||bytes-1>0x7fffff-address)return EldenNvtArgument;
    int error=page(d,address);if(error)return error;
    d->tx[0]=(address&127)|128;memcpy(d->tx+1,data,bytes);return transfer(d,bytes+1,false);
}
static int byte(EldenNvtDevice *d,uint32_t address,uint8_t value){return write(d,address,&value,1);}
static int number(EldenNvtDevice *d,uint32_t address,uint32_t value,unsigned size){
    uint8_t data[4];for(unsigned i=0;i<size;i++)data[i]=value>>(8*i);return write(d,address,data,size);
}
int elden_nvt_read(EldenNvtDevice *d,uint32_t address,uint8_t *out,size_t bytes){
    if(!out||!bytes||bytes>108||address>0x7fffff||bytes-1>0x7fffff-address)return EldenNvtArgument;
    int error=page(d,address);if(error)return error;
    memset(d->tx,0,bytes+2);d->tx[0]=address&127;error=transfer(d,bytes+2,true);
    if(!error)memcpy(out,d->rx+2,bytes);
    return error;
}
static int bootloader(EldenNvtDevice *d){
    int error=byte(d,SW_RESET,0x69);if(!error)d->delay_us(d->context,5000);return error;
}
int elden_nvt_identify(EldenNvtDevice *d,bool reset){
    if(!valid(d))return EldenNvtArgument;
    int error=0;
    if(reset){
        error=byte(d,ENG_RESET,0x5a);if(error)return error;
        d->delay_us(d->context,11000);error=bootloader(d);if(error)return error;
        uint8_t zeros[6]={0};error=write(d,TRIM,zeros,6);if(error)return error;
    }
    uint8_t id[6];error=elden_nvt_read(d,TRIM,id,6);if(error)return error;
    if(id[0]!=0x15||id[3]!=0x23||id[4]!=0x65||id[5]!=3)return EldenNvtChip;
    uint8_t strap;error=elden_nvt_read(d,CASCADE,&strap,1);if(error)return error;
    memcpy(d->trim,id,6);d->cascade=(strap&1)==0;d->identified=true;return 0;
}
int elden_nvt_firmware_info(EldenNvtDevice *d){
    uint8_t info[38];int error=elden_nvt_read(d,ELDEN_NVT_EVENT+0x78,info,sizeof(info));if(error)return error;
    if((unsigned)info[0]+info[1]!=255)return EldenNvtFirmware;
    uint16_t pid=info[34]|(uint16_t)info[35]<<8;if(pid!=0x6097)return EldenNvtChip;
    d->pid=pid;d->firmware_version=info[0];return 0;
}
int elden_nvt_upload(EldenNvtDevice *d,const EldenNvtImage *supplied){
    if(!valid(d)||!supplied)return EldenNvtArgument;
    EldenNvtImage image;int error=elden_nvt_image(&image,supplied->data,supplied->bytes);if(error)return error;
    d->ready=false;d->stage=1;
    if(!d->identified&&(error=elden_nvt_identify(d,true)))return error;
    if((error=bootloader(d)))return error;
    d->stage=2;
    const uint32_t addresses[6]={ILM_DES,ILM_LEN,ILM_CRC,DLM_DES,DLM_LEN,DLM_CRC};
    const uint32_t values[6]={image.part[0].ram,image.part[0].size,image.part[0].crc,image.part[1].ram,image.part[1].size,image.part[1].crc};
    for(unsigned i=0;i<6;i++)if((error=number(d,addresses[i],values[i],i%3==2?4:3)))return error;
    d->stage=3;if(image.cascade&&(error=byte(d,AUTO_COPY,0x56)))return error;
    for(unsigned i=0;i<image.count;i++){
        EldenNvtPartition p=image.part[i];uint32_t left=p.size+1;
        while(left){unsigned bytes=left>ELDEN_NVT_FW_CHUNK?ELDEN_NVT_FW_CHUNK:left;
            if((error=write(d,p.ram,image.data+p.file,bytes)))return error;
            p.ram+=bytes;p.file+=bytes;left-=bytes;
        }
    }
    d->stage=4;
    if(image.cascade){
        uint8_t value=0x56;for(d->polls=0;d->polls<200;d->polls++){
            if((error=elden_nvt_read(d,AUTO_COPY,&value,1)))return error;
            if(!value)break;
            d->delay_us(d->context,1000);
        }
        if(value)return EldenNvtTimeout;
    }
    d->stage=5;uint8_t zeros[6]={0},command[2]={0xae,0};
    if((error=write(d,ELDEN_NVT_EVENT+0x60,zeros,6))||
       (error=write(d,ELDEN_NVT_EVENT+0x50,command,2))||(error=byte(d,BOOT_READY,1)))return error;
    d->stage=6;uint8_t state[5]={0};
    for(d->polls=0;d->polls<100;d->polls++){
        d->delay_us(d->context,10000);if((error=elden_nvt_read(d,ELDEN_NVT_EVENT+0x60,state,5)))return error;
        d->reset_state=state[0];if(state[0]>=0xa0&&state[0]<=0xaf)break;
    }
    if(state[0]<0xa0||state[0]>0xaf)return EldenNvtTimeout;
    d->stage=7;if((error=elden_nvt_firmware_info(d)))return error;
    d->ready=true;d->stage=8;return 0;
}
int elden_nvt_points(EldenNvtDevice *d,uint8_t out[109]){
    if(!valid(d)||!d->ready||!out)return EldenNvtArgument;
    uint8_t points[108];int error=elden_nvt_read(d,ELDEN_NVT_EVENT,points,sizeof(points));
    if(!error){out[0]=0;memcpy(out+1,points,sizeof(points));}return error;
}
