/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Protocol facts: h0cheung/tb322fc-linux-kernel novatek-nvt36xxx-spi.c,
 * 6f97250ead155fc3b980856323e99a4623dae73d. Board register access is separate. */
#include "PointReport.h"
int elden_nvt_decode(const uint8_t *p,size_t bytes,uint16_t max_x,uint16_t max_y,struct elden_nvt_frame *out){
    if(!p || !out || bytes!=ELDEN_NVT_POINT_BYTES || !max_x || !max_y)return ELDEN_NVT_LENGTH;
    uint8_t sum=0;for(unsigned i=1;i<=65;++i)sum=(uint8_t)(sum+p[i]);
    if(sum)return ELDEN_NVT_CHECKSUM;
    struct elden_nvt_frame result={0};unsigned seen=0;
    for(unsigned slot=0;slot<ELDEN_NVT_CONTACTS;++slot){
        unsigned at=1+slot*6,status=p[at]&7,id=p[at]>>3;
        if((status!=1 && status!=2) || id<1 || id>10)continue;
        if(seen&(1U<<id))return ELDEN_NVT_DUPLICATE;
        seen|=1U<<id;
        unsigned x=((unsigned)p[at+1]<<8)|p[at+2],y=((unsigned)p[at+3]<<8)|p[at+4];
        if(x>max_x || y>max_y)return ELDEN_NVT_RANGE;
        struct elden_nvt_contact *c=&result.contacts[result.count++];
        c->id=(uint8_t)(id-1);c->width=p[at+5]?p[at+5]:1;c->raw_x=(uint16_t)x;c->raw_y=(uint16_t)y;
        c->x=(uint16_t)(((uint32_t)x*32767U+max_x/2U)/max_x);
        c->y=(uint16_t)(((uint32_t)y*32767U+max_y/2U)/max_y);
    }
    *out=result;return ELDEN_NVT_OK;
}
