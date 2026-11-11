/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "EldenScmiPerf.h"
#include "CpuPerfAccess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
struct Fake {uint32_t words[128];unsigned reads,writes,rings,sets;uint64_t tick;bool silent;};
static uint32_t read(void *p,uint32_t offset){Fake *f=(Fake*)p;++f->reads;CHECK(offset<512&&!(offset&3));return f->words[offset/4];}
static void write(void *p,uint32_t offset,uint32_t value){Fake *f=(Fake*)p;++f->writes;CHECK(offset<512&&!(offset&3));f->words[offset/4]=value;}
static uint64_t now(void *p){return ++((Fake*)p)->tick;}
static bool allowed(void *,uint32_t protocol,uint32_t message){return elden_cpu_perf_query(protocol,message);}
static void ring(void *p,uint32_t){
    Fake *f=(Fake*)p;++f->rings;if(f->silent)return;
    const uint32_t h=f->words[scmi::kHeader/4],protocol=scmi::headerProtocol(h),message=scmi::headerMessage(h);
    CHECK(allowed(nullptr,protocol,message));
    uint32_t domain=f->words[scmi::kPayload/4];uint32_t *r=f->words+scmi::kPayload/4+1;unsigned count=0;
    const uint32_t levels[2][3]={{384000,1996800,3532800},{1017600,2841600,4320000}};
    memset(r,0,64*4);
    if(message==scmi::kVersion){r[0]=protocol==scmi::kBase?0x20000:0x30000;count=1;}
    else if(message==scmi::kAttributes){r[0]=2;count=4;}
    else if(message==scmi::kDomainAttributes){CHECK(domain<2);r[0]=scmi::kCanSetLevel;r[2]=levels[domain][1];r[3]=levels[domain][1];memcpy(r+4,"test-domain",12);count=8;}
    else if(message==scmi::kDescribeLevels){CHECK(domain<2);r[0]=3;for(unsigned i=0;i<3;++i){r[1+3*i]=levels[domain][i];r[3+3*i]=10;}count=10;}
    else if(message==scmi::kLevelGet){CHECK(domain<2);r[0]=levels[domain][1];count=1;}
    else if(message==scmi::kLimitsGet){CHECK(domain<2);r[0]=levels[domain][2];r[1]=levels[domain][0];count=2;}
    else ++f->sets;
    f->words[scmi::kPayload/4]=0;f->words[scmi::kLength/4]=8+4*count;f->words[scmi::kChannelStatus/4]=1;
}
int main(){
    Fake f={};f.words[scmi::kChannelStatus/4]=1;scmi::Perf p={};
    p.channel.io={&f,read,write,ring,now,nullptr,nullptr,1000000,1,allowed};
    CHECK(scmi::ok(scmi::probe(&p)));CHECK(p.domains==2&&p.domain[0].count==3&&p.domain[1].count==3);
    uint32_t index=0,raw=0;CHECK(scmi::ok(scmi::levelGet(&p,0,&index,&raw))&&index==1&&raw==1996800);
    uint32_t hi=0,lo=0;CHECK(scmi::ok(scmi::limitsGet(&p,1,&hi,&lo))&&hi==2&&lo==0);
    unsigned reads=f.reads,writes=f.writes,rings=f.rings;scmi::Result denied=scmi::levelSet(&p,0,0);
    CHECK(denied.error==scmi::kStatus&&denied.status==scmi::kDenied);CHECK(f.reads==reads&&f.writes==writes&&f.rings==rings&&f.sets==0);
    uint32_t tx[3]={0,0,0};denied=scmi::transfer(&p.channel,scmi::kPerf,scmi::kLimitsSet,tx,3,nullptr,0,nullptr);
    CHECK(denied.status==scmi::kDenied&&f.writes==writes&&f.reads==reads);
    f.silent=true;scmi::Result timeout=scmi::levelGet(&p,0,&index,&raw);
    CHECK(timeout.error==scmi::kTimeout&&f.words[scmi::kChannelStatus/4]==0);
    writes=f.writes;scmi::Result busy=scmi::baseVersion(&p);CHECK(busy.error==scmi::kBusy&&f.writes==writes);
    printf("CPU PERF survey: %u checks, no performance SET or forced mailbox release\n",checks);return 0;
}
