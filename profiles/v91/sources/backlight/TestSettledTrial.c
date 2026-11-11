/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "SettledTrial.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(c) do{++checks;assert(c);}while(0)
static EldenAw99706 captured(void){
    EldenAw99706 s={0};s.failed=true;s.error=EldenBlFault;s.observed_id=7;s.observed_mode=0x65;
    s.observed_hi=6;s.observed_lo=0x3d;s.observed_config=0xf8;s.observed_open=0x3c;s.observed_flags=8;s.observed_cfg7=5;return s;
}
static void sample(EldenBlSettledTrial *t,unsigned n,unsigned fault){
    const unsigned ms[]={100,1000,2000,4000,8000};
    elden_bl_trial_sample(t,ms[n],fault==1?-1:0,fault==2?1:0,fault==3?1:0,
        fault==4?1:0,fault==5?0x78:0xf8,fault==6?7:6,fault==7?0x3c:0x3d);
}
int main(void){
    EldenAw99706 s=captured();EldenBlSettledTrial t;elden_bl_trial_begin(&t,&s);
    for(unsigned n=0;n<5;n++){CHECK(!elden_bl_trial_admitted(&t));sample(&t,n,0);}
    CHECK(elden_bl_trial_admitted(&t));sample(&t,4,0);CHECK(!elden_bl_trial_admitted(&t));
    for(unsigned f=1;f<=7;f++)for(unsigned bad=0;bad<5;bad++){
        s=captured();elden_bl_trial_begin(&t,&s);
        for(unsigned n=0;n<5;n++){sample(&t,n,n==bad?f:0);CHECK(!elden_bl_trial_admitted(&t));}
        CHECK(!t.valid); /* a later clean sample cannot erase the failure */
    }
    for(unsigned f=0;f<13;f++){
        s=captured();switch(f){case 0:s.failed=false;break;case 1:s.ready=true;break;case 2:s.error=EldenBlIo;break;
        case 3:s.writes=1;break;case 4:s.observed_id=8;break;case 5:s.observed_mode^=1;break;
        case 6:s.observed_hi=5;break;case 7:s.observed_lo=0x3e;break;case 8:s.observed_config=0xfe;break;
        case 9:s.observed_open=0;break;case 10:s.observed_short=1;break;case 11:s.observed_flags=4;break;case 12:s.observed_cfg7=4;break;}
        elden_bl_trial_begin(&t,&s);for(unsigned n=0;n<5;n++)sample(&t,n,0);CHECK(!elden_bl_trial_admitted(&t));
    }
    s=captured();elden_bl_trial_begin(&t,&s);sample(&t,1,0);CHECK(!t.valid);
    elden_bl_trial_begin(&t,&s);sample(&t,0,0);sample(&t,0,0);CHECK(!t.valid);
    printf("Settled AW99706 trial: %u checks; no retry after any changed/faulted observation\n",checks);
}
