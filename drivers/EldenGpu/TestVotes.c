/* SPDX-License-Identifier: MIT */
/* Synthetic command-db rails/BCM data, not physical voltage measurements. */
#include "EldenGpuVotes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t rail[40],aux[8]={0xe8,3,0,0,16,0,0,0};
int main(void)
{
    const uint16_t corners[]={0,32,48,50,52,56,60,64,80,128,144,192,224,256,320,416,448,452};
    for(unsigned i=0;i<sizeof(corners)/sizeof(*corners);++i){rail[2*i]=(uint8_t)corners[i];rail[2*i+1]=(uint8_t)(corners[i]>>8);}
    Sm8750GmuVoteInputs in={0};in.gfx=in.mx=in.cx=in.gmxc=in.gbx=(AdrenoHfiRail){rail,sizeof(rail)};
    in.sh0=(Sm8750GmuBcmInput){0x50000,{aux,8}};in.mc0=in.sh0;in.mc0.address+=16;in.acv=in.sh0;in.acv.address+=32;in.cn0_address=0x50030;
    const uint32_t fuses[]={190,221,232,242};const uint64_t maxima[]={900000000,1050000000,1100000000,1100000000};
    const uint64_t common[]={160000000,222000000,342000000,389000000,443000000,525000000,607000000,660000000,734000000,832000000,900000000};
    unsigned cases=0;
    for(unsigned bin=0;bin<4;++bin)for(unsigned i=0;i<sizeof(common)/sizeof(*common);++i) {
        AdrenoHfiStartupConfig c={0};unsigned level=0,bw=0;
        assert(!elden_gpu_build_startup(&in,fuses[bin],common[i],&c,&level,&bw));
        assert(c.gpu[level].hz==common[i]&&c.gpu[c.gpu_count-1].hz==maxima[bin]);
        assert(c.gmu_count==4&&c.gmu[3].hz==687500000&&c.gmu[3].vote);
        assert(bw>0&&bw<c.bandwidth_count&&!c.enable_ifpc&&!c.acd_enable_mask);
        uint32_t wire[159],id;size_t words;assert(!AdrenoHfiStartupEncode(&c,AdrenoHfiStagePerformance,&id,wire,159,&words));
        assert(id==15&&words==6+3*c.gpu_count+2*c.gmu_count&&wire[5]==UINT32_MAX);++cases;
    }
    for(unsigned mode=0;mode<5;++mode) {
        Sm8750GmuVoteInputs bad=in;AdrenoHfiStartupConfig before,c;memset(&c,0xa5,sizeof(c));before=c;unsigned level=99,bw=98;
        uint32_t fuse=190;uint64_t hz=222000000;
        if(mode==0)fuse=252;if(mode==1)hz=1200000000;if(mode==2)hz=0;if(mode==3)bad.gfx.data=0;if(mode==4)bad.acv.aux.bytes=1;
        assert(elden_gpu_build_startup(&bad,fuse,hz,&c,&level,&bw)==AdrenoHfiStartupInvalid);
        assert(!memcmp(&c,&before,sizeof(c))&&level==99&&bw==98);++cases;
    }
    in.gmxc=(AdrenoHfiRail){0};in.gbx=(AdrenoHfiRail){0};AdrenoHfiStartupConfig c;unsigned level,bw;
    assert(!elden_gpu_build_startup(&in,190,525000000,&c,&level,&bw));++cases;
    printf("PASS: %u Elden OPP/HFI cases; actual functions with synthetic rails, no IO\n",cases);
}
