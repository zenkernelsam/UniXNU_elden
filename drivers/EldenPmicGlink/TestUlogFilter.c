/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UlogFilter.h"
#include "UlogRead.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void begin(EldenLogFilter *s,uint8_t *req){elden_log_filter_init(s);elden_log_filter_link(s,true,1);CHECK(elden_log_filter_next(s,true,false,req)==32);}
int main(void){
 EldenLogFilter s;uint8_t req[32],ack[64]={0};put(ack,32778);put(ack+4,1);put(ack+8,0x19);
 begin(&s,req);CHECK(get(req)==32778&&get(req+4)==1&&get(req+8)==0x19);
 CHECK(!get(req+12)&&get(req+16)==0x02000020&&get(req+20)==4&&get(req+24)==3&&!get(req+28));
 CHECK(!elden_log_filter_next(&s,true,false,req));CHECK(elden_log_filter_receive(&s,1,ack,16)==1&&s.phase==EldenLogFilterActive);
 CHECK(!elden_log_filter_next(&s,true,false,req));CHECK(elden_log_filter_next(&s,false,true,req)==32);
 CHECK(!get(req+12)&&!get(req+16)&&!get(req+20)&&get(req+24)==4&&!get(req+28));
 CHECK(elden_log_filter_receive(&s,1,ack,16)==1&&s.restored&&s.requests==2&&s.acks==2);
 for(unsigned i=0;i<4;i++)CHECK(!elden_log_filter_next(&s,true,i&1,req));
 begin(&s,req);elden_log_filter_unsent(&s);CHECK(!s.requests&&!s.touched&&!s.pending);
 CHECK(!elden_log_filter_next(&s,false,true,req)&&s.restored&&s.phase==EldenLogFilterDone);
 begin(&s,req);CHECK(!elden_log_filter_receive(&s,2,ack,16)&&s.pending);
 put(ack+8,0x18);CHECK(!elden_log_filter_receive(&s,1,ack,16)&&s.pending);put(ack+8,0x19);
 for(unsigned n=0;n<33;n++)if(n!=16){begin(&s,req);CHECK(elden_log_filter_receive(&s,1,ack,n)==-1&&s.phase==EldenLogFilterFailed);CHECK(!elden_log_filter_next(&s,true,true,req));}
 begin(&s,req);put(ack+12,1);CHECK(elden_log_filter_receive(&s,1,ack,16)==-1);put(ack+12,0);
 begin(&s,req);elden_log_filter_fail(&s,4);CHECK(!elden_log_filter_next(&s,true,true,req)&&!s.restored);
 begin(&s,req);elden_log_filter_link(&s,false,2);elden_log_filter_link(&s,true,3);CHECK(s.phase==EldenLogFilterFailed&&!elden_log_filter_next(&s,true,true,req));
 static EldenUlogRead u;static uint8_t reply[ELDEN_ULOG_REPLY];uint8_t request[16];
 elden_ulog_init(&u);elden_ulog_enable_policy_trace(&u);elden_ulog_link(&u,true,1);
 put(reply,ELDEN_ULOG_OWNER);put(reply+4,1);put(reply+8,0x18);
 for(unsigned i=0;i<12;i++){
  CHECK(elden_ulog_next(&u,request)==16&&get(request+8)==0x18&&get(request+12)==8192);
  CHECK(elden_ulog_receive(&u,1,reply,sizeof(reply))==1);CHECK(!u.text_bytes&&u.current_empty);
  elden_ulog_release(&u);
 }
 CHECK(!elden_ulog_next(&u,request)&&u.requests==12&&u.completed==12&&!u.init_pages);
 elden_ulog_link(&u,false,2);elden_ulog_link(&u,true,3);CHECK(!elden_ulog_next(&u,request));
 elden_ulog_init(&u);elden_ulog_enable_policy_trace(&u);elden_ulog_link(&u,true,1);reply[12]='x';reply[13]=0;
 CHECK(elden_ulog_next(&u,request)==16);CHECK(elden_ulog_receive(&u,1,reply,sizeof(reply))==1);elden_ulog_release(&u);
 CHECK(elden_ulog_next(&u,request)==16);CHECK(elden_ulog_receive(&u,1,reply,sizeof(reply))==-1&&u.error==EldenUlogDuplicate);
 printf("UlogFilter: %u checks; fixed categories/default restore, faults/epochs,12 bounded current pages\n",checks);return 0;
}
