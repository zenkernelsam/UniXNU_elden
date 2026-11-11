/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcTrial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
typedef struct {uint8_t value,history[2];unsigned writes;int open_error,check_error,write_error;bool fence;} Io;
static int open_io(void *p){return ((Io *)p)->open_error;}
static int check_io(void *p,uint8_t v){Io *i=p;if(i->check_error)return -1;return i->value==v?0:(v==9&&i->value==10?1:-1);}
static int write_io(void *p,uint8_t v){Io *i=p;CHECK(v==9||v==10);if(i->fence)return 1;CHECK(i->writes<2);i->history[i->writes++]=v;if(i->write_error)return -1;i->value=v;return 0;}
static void snapshot(EldenDcTrial *s,int corrupt){
 static const uint8_t values[]={0xdb,1,0,10,0x10,0x12,1,1,0,0x80,0x80,0x10,0x12,1,1,0,0x80,0x80,0xdb};
 for(unsigned i=0;i<19;i++){
  uint8_t req[28],r[284]={0};CHECK(elden_dc_trial_next_read(s,req)==28);memcpy(r,req,28);put(r+8,0x37);r[28]=values[i]^(i==(unsigned)corrupt?1:0);
  int rc=elden_dc_trial_receive(s,r,sizeof(r));CHECK(rc==(i==18?(corrupt<0?2:-1):1));
 }
}
static void begin(EldenDcTrial *s,Io *i){elden_dc_trial_init(s);memset(i,0,sizeof(*i));i->value=10;elden_dc_trial_link(s,true,1);CHECK(elden_dc_trial_begin(s,100));snapshot(s,-1);}
static void step(EldenDcTrial *s,Io *i,uint64_t t,bool safe,bool stop,bool abort){EldenDcTrialIo io={i,open_io,check_io,write_io};elden_dc_trial_step(s,t,safe,stop,abort,&io);}
static void low(EldenDcTrial *s,Io *i){begin(s,i);step(s,i,110,true,false,false);step(s,i,120,true,false,false);step(s,i,130,true,false,false);CHECK(s->phase==EldenDcTrialHold&&s->lower_seen&&i->value==9);}
int main(void){
 EldenDcTrial s;Io i;
 low(&s,&i);step(&s,&i,5130,true,false,false);step(&s,&i,5140,true,false,false);CHECK(s.phase==EldenDcTrialFinal&&i.value==10);snapshot(&s,-1);
 CHECK(s.phase==EldenDcTrialDone&&s.restored&&s.held_full&&!s.overridden&&s.write_completions==2&&i.writes==2&&i.history[0]==9&&i.history[1]==10);
 CHECK(!elden_dc_trial_begin(&s,6000));step(&s,&i,99999,true,true,true);CHECK(i.writes==2);
 for(int field=0;field<19;field++){elden_dc_trial_init(&s);elden_dc_trial_link(&s,true,1);CHECK(elden_dc_trial_begin(&s,100));snapshot(&s,field);CHECK(s.phase==EldenDcTrialFailed&&!s.write_attempts);}
 begin(&s,&i);step(&s,&i,110,false,false,false);CHECK(s.phase==EldenDcTrialOpen&&!i.writes);step(&s,&i,120,true,true,false);CHECK(s.phase==EldenDcTrialFailed&&!i.writes);
 begin(&s,&i);i.open_error=1;step(&s,&i,110,true,false,false);CHECK(s.phase==EldenDcTrialFailed&&!i.writes);
 begin(&s,&i);step(&s,&i,110,true,false,false);i.fence=true;step(&s,&i,120,true,false,false);CHECK(!i.writes&&!s.write_attempts&&s.phase==EldenDcTrialLower);i.fence=false;step(&s,&i,130,true,false,false);CHECK(i.writes==1);
 begin(&s,&i);step(&s,&i,110,true,false,false);i.write_error=1;step(&s,&i,120,true,false,false);step(&s,&i,130,true,false,false);CHECK(s.phase==EldenDcTrialFailed&&i.writes==1&&!s.write_completions);
 low(&s,&i);step(&s,&i,140,true,true,false);step(&s,&i,150,true,true,false);snapshot(&s,-1);CHECK(s.restored&&!s.held_full&&i.writes==2);
 low(&s,&i);step(&s,&i,140,true,true,false);step(&s,&i,150,false,true,false);step(&s,&i,6140,false,true,false);CHECK(s.phase==EldenDcTrialFailed&&i.value==9&&i.writes==1);
 low(&s,&i);i.value=10;step(&s,&i,140,true,false,false);snapshot(&s,-1);CHECK(s.restored&&s.overridden&&!s.held_full&&i.writes==1);
 low(&s,&i);i.value=8;step(&s,&i,140,true,false,false);CHECK(s.phase==EldenDcTrialFailed&&i.writes==1&&i.value==8);
 low(&s,&i);step(&s,&i,140,true,false,true);step(&s,&i,150,true,false,false);CHECK(s.phase==EldenDcTrialFailed&&i.writes==1);
 low(&s,&i);elden_dc_trial_link(&s,false,2);elden_dc_trial_link(&s,true,3);CHECK(s.phase==EldenDcTrialFailed&&!elden_dc_trial_begin(&s,200)&&i.writes==1);
 low(&s,&i);step(&s,&i,5130,true,false,false);step(&s,&i,5140,true,false,false);snapshot(&s,3);CHECK(!s.restored&&s.phase==EldenDcTrialFailed);
 printf("DcTrial: %u checks; real I/O results,19-byte guards,450/500 bounds,stop/override/fence/epoch paths\n",checks);return 0;
}
