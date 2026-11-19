/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual NVT Protocol with original independent SRAM/ROM/command peer. */
#define main previous_protocol_main
#include "../EldenNvt36536/TestProtocol.c"
#undef main
void *elden_sleep_protocol_create(EldenNvtDevice *d,uint8_t *tx,uint8_t *rx,const uint8_t *firmware,size_t bytes){
    struct model *m=calloc(1,sizeof(*m));CHECK(m);m->ram=calloc(0x800000,1);CHECK(m->ram);
    *d=device(m,tx,rx);EldenNvtImage image;CHECK(!elden_nvt_image(&image,firmware,bytes));CHECK(!elden_nvt_upload(d,&image));return m;
}
void elden_sleep_protocol_gesture(void *v,unsigned id){struct model *m=v;uint8_t *p=m->ram+ELDEN_NVT_EVENT;
    memset(p,0,108);p[0]=(uint8_t)(id<<3);uint8_t sum=0;for(unsigned i=0;i<64;i++)sum=(uint8_t)(sum+p[i]);p[64]=(uint8_t)(0-sum);p[65]=255;
}
unsigned elden_sleep_protocol_calls(void *v){return ((struct model *)v)->calls;}
void elden_sleep_protocol_fault(void *v){struct model *m=v;m->fail_at=m->calls+1;}
void elden_sleep_protocol_destroy(void *v){struct model *m=v;free(m->ram);free(m);}
