/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "Protocol.h"
#include "PointReport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct model {uint8_t *ram;uint32_t page;unsigned calls,writes,fail_at;bool stuck_auto;};
static int transfer(void *context,const uint8_t *tx,uint8_t *rx,size_t size){
    struct model *m=context;CHECK(size>=2&&size<=15361);++m->calls;
    if(m->calls==m->fail_at)return -110;
    if(tx[0]==0xff&&size==3&&!rx){m->page=(uint32_t)tx[1]<<15|(uint32_t)tx[2]<<7;return 0;}
    uint32_t address=m->page+(tx[0]&127);CHECK(address+size-1<=0x800000);
    if(rx){
        CHECK(!(tx[0]&128));for(size_t i=1;i<size;i++)CHECK(tx[i]==0);
        memset(rx,0,size);
        if(address==0x1fc825&&!m->stuck_auto)m->ram[address]=0;
        memcpy(rx+2,m->ram+address,size-2);
    }else{
        CHECK(tx[0]&128);m->writes++;
        if(address!=0x1fb104)memcpy(m->ram+address,tx+1,size-1); /* silicon trim is ROM */
        if(address==0x1fb50d&&tx[1]==1){
            m->ram[ELDEN_NVT_EVENT+0x60]=0xa0;
            m->ram[ELDEN_NVT_EVENT+0x78]=0x1a;m->ram[ELDEN_NVT_EVENT+0x79]=0xe5;
            m->ram[ELDEN_NVT_EVENT+0x78+34]=0x97;m->ram[ELDEN_NVT_EVENT+0x78+35]=0x60;
        }
    }
    return 0;
}
static void delay(void *context,unsigned usec){(void)context;CHECK(usec>=1000&&usec<=11000);}
static EldenNvtDevice device(struct model *m,uint8_t *tx,uint8_t *rx){
    EldenNvtDevice d={0};d.context=m;d.transfer=transfer;d.delay_us=delay;d.tx=tx;d.rx=rx;d.tx_bytes=15361;d.rx_bytes=110;
    memcpy(m->ram+0x1fb104,(uint8_t[6]){0x15,0,0,0x23,0x65,3},6);m->ram[0x1fb12c]=1;return d;
}
static void rejected(uint8_t *data,size_t bytes){
    EldenNvtImage a,b;memset(&a,0xa5,sizeof(a));memcpy(&b,&a,sizeof(b));
    CHECK(elden_nvt_image(&a,data,bytes)!=0);CHECK(!memcmp(&a,&b,sizeof(a)));
}
int main(int argc,char **argv){
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);CHECK(!fseek(f,0,SEEK_SET));
    size_t bytes=(size_t)n;uint8_t *data=malloc(bytes),*original=malloc(bytes),*bad=malloc(bytes);CHECK(data&&original&&bad);
    CHECK(fread(data,1,bytes,f)==bytes);CHECK(!fclose(f));memcpy(original,data,bytes);
    EldenNvtImage image;CHECK(!elden_nvt_image(&image,data,bytes));CHECK(image.count>=2&&image.cascade);
    CHECK(image.part[0].crc==0xc9bbe411&&image.part[1].crc==0x47059e2f);
    rejected(data,bytes-1);memcpy(bad,data,bytes);bad[24]^=1;rejected(bad,bytes);
    memcpy(bad,data,bytes);bad[0x28]|=16;rejected(bad,bytes);memcpy(bad,data,bytes);bad[0x29]|=1;rejected(bad,bytes);
    uint8_t *tx=malloc(15361),*rx=malloc(110);struct model m={0};m.ram=calloc(0x800000,1);CHECK(tx&&rx&&m.ram);
    EldenNvtDevice d=device(&m,tx,rx);CHECK(!elden_nvt_upload(&d,&image));CHECK(d.ready&&d.pid==0x6097&&d.stage==8);
    uint8_t *expected=calloc(0x800000,1);CHECK(expected!=NULL);
    for(unsigned i=0;i<image.count;i++)memcpy(expected+image.part[i].ram,data+image.part[i].file,image.part[i].size+1);
    for(unsigned i=0;i<image.count;i++)CHECK(!memcmp(expected+image.part[i].ram,m.ram+image.part[i].ram,image.part[i].size+1));
    CHECK(!memcmp(original,data,bytes));
    memset(m.ram+ELDEN_NVT_EVENT,0,108);uint8_t *point=m.ram+ELDEN_NVT_EVENT;
    point[0]=9;point[1]=0x25;point[2]=0x30;point[3]=0x3b;point[4]=0x60;point[5]=4;
    uint8_t checksum=0;for(unsigned i=0;i<64;i++)checksum+=point[i];point[64]=(uint8_t)(0-checksum);
    uint8_t report[109];CHECK(!elden_nvt_points(&d,report));struct elden_nvt_frame frame;
    CHECK(!elden_nvt_decode(report,sizeof(report),19040,30400,&frame));CHECK(frame.count==1&&frame.contacts[0].raw_x==9520&&frame.contacts[0].raw_y==15200);
    CHECK(frame.contacts[0].x==16384&&frame.contacts[0].y==16384);
    memset(m.ram,0,0x800000);m.calls=m.writes=0;m.fail_at=3;d=device(&m,tx,rx);
    CHECK(elden_nvt_upload(&d,&image)==-110&&d.poisoned&&!d.ready);unsigned before=m.calls;
    CHECK(elden_nvt_identify(&d,true)!=0&&m.calls==before);
    m.fail_at=0;m.stuck_auto=true;m.calls=0;d=device(&m,tx,rx);CHECK(elden_nvt_upload(&d,&image)==-110&&!d.ready&&d.stage==4);
    free(expected);free(m.ram);free(rx);free(tx);free(bad);free(original);free(data);
    printf("PASS: %u checks; actual NT36536 image CRC/framing/SRAM/dummy byte/point decode; no hardware\n",checks);return 0;
}
