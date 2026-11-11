#include "PointReport.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void seal(uint8_t *p){unsigned sum=0;for(unsigned i=1;i<65;++i)sum+=p[i];p[65]=(uint8_t)(0U-sum);}
int main(void){
    uint8_t p[109]={0};struct elden_nvt_frame out={0};
    /* Independent wire vector: finger 10, enter, X=19040 (4a60), Y=30400 (76c0). */
    const uint8_t record[]={0x51,0x4a,0x60,0x76,0xc0,0};memcpy(p+1,record,6);seal(p);
    assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==0);
    assert(out.count==1 && out.contacts[0].id==9 && out.contacts[0].width==1);
    assert(out.contacts[0].x==32767 && out.contacts[0].y==32767);
    struct elden_nvt_frame previous=out;p[3]^=1;
    assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==ELDEN_NVT_CHECKSUM && !memcmp(&out,&previous,sizeof(out)));
    p[3]^=1;memcpy(p+7,record,6);seal(p);
    assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==ELDEN_NVT_DUPLICATE && !memcmp(&out,&previous,sizeof(out)));
    memset(p+7,0,6);p[3]=0x61;seal(p);
    assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==ELDEN_NVT_RANGE && !memcmp(&out,&previous,sizeof(out)));
    assert(elden_nvt_decode(p,65,19040,30400,&out)==ELDEN_NVT_LENGTH);
    assert(elden_nvt_decode(p,sizeof(p),0,30400,&out)==ELDEN_NVT_LENGTH);
    memset(p,0,sizeof(p));seal(p);assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==0 && out.count==0);
    p[1]=0x0a;p[2]=0x25;p[3]=0x30;p[4]=0x3b;p[5]=0x60;p[6]=4;seal(p);
    assert(elden_nvt_decode(p,sizeof(p),19040,30400,&out)==0 && out.count==1);
    assert(out.contacts[0].x==16384 && out.contacts[0].y==16384 && out.contacts[0].width==4);
    puts("PASS: native NT36536 point layout, inclusive endpoints, checksum, duplicate/range/length rejection and empty-frame release");
}
