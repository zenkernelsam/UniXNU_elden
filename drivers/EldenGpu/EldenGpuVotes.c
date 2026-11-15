/* SPDX-License-Identifier: MIT */
/* Adapts the PaganiGpuVotes command-db/HFI flow at a84204ec, using generated
 * TB322FC Linux OPPs and speed-bin mapping, never the phone's frequency tables. */
#include "EldenGpuVotes.h"
#include "EldenGpuOpps.h"

AdrenoHfiStartupStatus elden_gpu_build_startup(const Sm8750GmuVoteInputs *in,
    uint32_t fuse,uint64_t requested,AdrenoHfiStartupConfig *out,unsigned *initial,unsigned *bandwidth)
{
    if(!in||!out||!initial||!bandwidth||!requested)return AdrenoHfiStartupInvalid;
    unsigned bin=32;
    for(unsigned i=0;i<sizeof(eldenGpuSpeedBins)/sizeof(*eldenGpuSpeedBins);++i)
        if(eldenGpuSpeedBins[i].fuse==fuse)bin=eldenGpuSpeedBins[i].index;
    if(bin>=4)return AdrenoHfiStartupInvalid;
    AdrenoHfiStartupConfig c={0};uint32_t levels[16]={0},votes[16]={0},dep[16]={0},bw[16]={0};
    unsigned selected=0,bw_count=1;uint32_t selected_bw=0;
    c.performance_table=AdrenoHfiPerfA8xx;c.gpu_count=1;
    for(unsigned i=0;i<sizeof(eldenGpuOpps)/sizeof(*eldenGpuOpps);++i) {
        const EldenGpuOpp *p=&eldenGpuOpps[i];
        if(!(p->supported_mask&(1u<<bin)))continue;
        if(c.gpu_count>=16||!p->bandwidth_kbps)return AdrenoHfiStartupInvalid;
        unsigned n=c.gpu_count++;levels[n]=p->level;c.gpu[n].hz=p->hz;
        if(p->hz==requested){selected=n;selected_bw=p->bandwidth_kbps;}
        if(p->bandwidth_kbps<bw[bw_count-1])return AdrenoHfiStartupInvalid;
        if(p->bandwidth_kbps>bw[bw_count-1]) {
            if(bw_count>=16)return AdrenoHfiStartupInvalid;
            bw[bw_count++]=p->bandwidth_kbps;
        }
    }
    if(!selected)return AdrenoHfiStartupInvalid;
    const AdrenoHfiRail *secondary=in->gmxc.data?&in->gmxc:&in->mx;
    if(AdrenoHfiGen8Votes(levels,c.gpu_count,&in->gfx,secondary,&in->mx,&in->gbx,votes,dep,16))
        return AdrenoHfiStartupInvalid;
    for(unsigned i=0;i<c.gpu_count;++i){c.gpu[i].vote=votes[i];c.gpu_dependency_votes[i]=dep[i];}
    c.gmu_count=1;levels[0]=0;
    for(unsigned i=0;i<sizeof(eldenGmuOpps)/sizeof(*eldenGmuOpps);++i) {
        if(c.gmu_count>=4)return AdrenoHfiStartupInvalid;
        unsigned n=c.gmu_count++;levels[n]=eldenGmuOpps[i].level;c.gmu[n].hz=eldenGmuOpps[i].hz;
    }
    const AdrenoHfiRail absent={0};
    if(AdrenoHfiGen8Votes(levels,c.gmu_count,&in->cx,&in->mx,&in->mx,&absent,votes,dep,16))
        return AdrenoHfiStartupInvalid;
    for(unsigned i=0;i<c.gmu_count;++i)c.gmu[i].vote=votes[i];
    /* BCM policy is the exact A830 catalog: SH0 width16, MC0 width4,
     * ACV fixed/perfmode bit2 above10687500kB/s. Addresses/aux remain live inputs. */
    const Sm8750GmuBcmInput *inputs[]={&in->sh0,&in->mc0,&in->acv};
    AdrenoHfiBcm bcms[3]={{.buswidth=16},{.buswidth=4},{.fixed=1,.perfmode=4,.perfmode_kbps=10687500}};
    for(unsigned i=0;i<3;++i) {
        bcms[i].address=inputs[i]->address;
        if(AdrenoHfiBcmAux(&bcms[i],inputs[i]->aux.data,inputs[i]->aux.bytes))return AdrenoHfiStartupInvalid;
    }
    if(AdrenoHfiBuildBandwidth(bw,bw_count,bcms,3,in->cn0_address,&c)||AdrenoHfiStartupValidate(&c))
        return AdrenoHfiStartupInvalid;
    unsigned bus=0;
    for(unsigned i=1;i<bw_count;++i)if(bw[i]==selected_bw)bus=i;
    if(!bus)return AdrenoHfiStartupInvalid;
    *out=c;*initial=selected;*bandwidth=bus;return AdrenoHfiStartupSuccess;
}
