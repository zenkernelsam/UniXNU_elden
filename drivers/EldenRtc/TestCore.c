/* SPDX-License-Identifier: GPL-2.0-only */
#include "Core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t raw[3];uint8_t low,control;unsigned full,reads,fail_at;} Model;
static int read_rtc(void *p,uint16_t address,uint8_t *out,size_t bytes){
    Model *m=p;if(++m->reads==m->fail_at)return -71;
    if(address==0x6146){assert(bytes==1);*out=m->control;}
    else if(bytes==1){assert(address==0x6148);*out=m->low;}
    else{assert(address==0x6148&&bytes==4&&m->full<3);uint32_t value=m->raw[m->full++];for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(value>>(i*8));}
    return 0;
}
static void setup(Model *m,EldenRtc *r,uint32_t raw){
    memset(m,0,sizeof(*m));memset(r,0,sizeof(*r));m->raw[0]=m->raw[1]=m->raw[2]=raw;m->low=(uint8_t)raw;m->control=0x80;
    EldenRtcIo io={m,read_rtc};assert(!elden_rtc_core_init(r,&io));
}
int main(void){
    Model m;EldenRtc rtc;uint32_t raw;uint64_t time=123;
    setup(&m,&rtc,1234);assert(elden_rtc_core_read(&rtc,&time)==EldenRtcNoOffset&&time==123);
    m.full=0;m.reads=0;assert(!elden_rtc_core_learn(&rtc,1800000000));
    m.full=0;m.low=211;m.raw[0]=1235;assert(!elden_rtc_core_read(&rtc,&time)&&time==1800000001);
    setup(&m,&rtc,255);m.raw[0]=0x1200ff;m.raw[1]=0x120100;m.low=0;
    assert(!elden_rtc_core_raw(&rtc,&raw)&&raw==0x120100&&m.full==2);
    for(unsigned fail=1;fail<=4;++fail){
        setup(&m,&rtc,255);m.raw[0]=255;m.raw[1]=256;m.low=0;m.fail_at=fail;raw=0xabcdef;
        assert(elden_rtc_core_raw(&rtc,&raw)==-71&&raw==0xabcdef);
    }
    setup(&m,&rtc,10);m.control=0;assert(elden_rtc_core_raw(&rtc,&raw)==EldenRtcStopped&&m.full==0);
    setup(&m,&rtc,10);assert(!elden_rtc_core_learn(&rtc,1800000000));m.full=0;m.raw[0]=9;m.low=9;time=123;
    assert(elden_rtc_core_read(&rtc,&time)==EldenRtcCounterReset&&time==123&&!rtc.has_offset);
    m.full=0;m.raw[0]=10;m.low=10;assert(elden_rtc_core_learn(&rtc,1800000000)==EldenRtcCounterReset);
    setup(&m,&rtc,1);assert(elden_rtc_core_learn(&rtc,0)==EldenRtcRange);
    assert(elden_rtc_core_learn(&rtc,UINT64_MAX)==EldenRtcRange);
    elden_rtc_core_stop(&rtc);assert(elden_rtc_core_raw(&rtc,&raw)==EldenRtcStopped);
    puts("PASS: actual Gen4 RTC read/carry, output preservation, volatile offset, clock reset and stop; PMIC reads modeled");
}
