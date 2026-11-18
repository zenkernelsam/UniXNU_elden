/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint64_t epoch;unsigned starts[2],stops[2],live[2],polls[2],results,failed_start,failed_stop,deny;
    uint8_t last_gain[2];EldenHapticsResult result[256];} Peer;
static int admit(void *p,uint64_t owner,uint64_t epoch){Peer *v=p;return owner!=7||epoch!=v->epoch||v->deny?-13:0;}
static int start(void *p,unsigned m,uint8_t wave,uint8_t gain,uint32_t duration){
    Peer *v=p;CHECK(m<2&&wave==1&&gain<=32&&duration<=20000);CHECK(!v->live[m]);
    ++v->starts[m];v->live[m]=1;v->last_gain[m]=gain;return v->failed_start==m+1?-71:0;
}
static int poll(void *p,unsigned m){Peer *v=p;v->polls[m]++;return v->live[m]?0:1;}
static int stop(void *p,unsigned m){Peer *v=p;v->stops[m]++;if(v->failed_stop==m+1)return -71;v->live[m]=0;return 0;}
static void done(void *p,const EldenHapticsResult *r){Peer *v=p;CHECK(v->results<256);v->result[v->results++]=*r;}
static EldenHapticsQueueLimits limits(EldenHapticsClickPoint *points){
    EldenHapticsQueueLimits l={0};l.points=points;l.count=3;l.max_duration_us=20000;l.max_late_us=5000;
    l.window_us=100000;l.max_on_us[0]=l.max_on_us[1]=20000;l.max_dual_on_us=20000;
    l.max_gain[0]=32;l.max_gain[1]=24;l.max_sum_gain=56;l.available_mask=3;
    l.square_budget[0]=UINT64_C(32)*32*20000;l.square_budget[1]=UINT64_C(24)*24*20000;return l;
}
static void init(EldenHapticsQueue *q,Peer *v){
    memset(q,0,sizeof(*q));memset(v,0,sizeof(*v));v->epoch=1;
    EldenHapticsClickPoint points[]={{0,{0,0}},{16384,{16,12}},{32767,{32,24}}};EldenHapticsQueueLimits l=limits(points);
    EldenHapticsQueueIo io={v,admit,start,poll,stop,done,NULL};CHECK(!elden_haptics_queue_init(q,&l,&io));
    points[2].gain[0]=128;CHECK(q->points[2].gain[0]==32); /* immutable copied LUT */
}
int main(void){
    EldenHapticsQueue q;Peer v;init(&q,&v);
    CHECK(!elden_haptics_queue_submit(&q,7,1,1,0,10000,3,1,32767));CHECK(!v.starts[0]);
    CHECK(!elden_haptics_queue_step(&q,0)&&v.live[0]&&v.live[1]&&v.last_gain[0]==32&&v.last_gain[1]==24);
    CHECK(elden_haptics_queue_submit(&q,7,1,1,0,10000,1,1,1)==EldenHapticsRequestStale);
    CHECK(elden_haptics_queue_cancel(&q,8,1,1)==EldenHapticsRequestStale);
    CHECK(!elden_haptics_queue_cancel(&q,7,1,1)&&!elden_haptics_queue_step(&q,1));
    CHECK(!v.live[0]&&!v.live[1]&&v.result[0].state==EldenHapticsCancelled);
    CHECK(!elden_haptics_queue_submit(&q,7,1,2,1,10000,1,1,16384));
    CHECK(!elden_haptics_queue_step(&q,1)&&v.live[0]);
    CHECK(!elden_haptics_queue_disconnect(&q,7,1)&&!elden_haptics_queue_step(&q,2));CHECK(!v.live[0]);
    CHECK(!elden_haptics_queue_submit(&q,7,1,3,110000,10000,1,1,32767));CHECK(!elden_haptics_queue_step(&q,110000));
    CHECK(v.live[0]);CHECK(!elden_haptics_queue_step(&q,120000)&&!v.live[0]&&v.result[2].state==EldenHapticsComplete);
    /* Fixed-window bug oracle: a request started99ms ago still consumes the
     * actual prior rolling window at101ms. No doubled boundary burst. */
    init(&q,&v);CHECK(!elden_haptics_queue_submit(&q,7,1,1,99000,20000,3,1,32767));CHECK(!elden_haptics_queue_step(&q,99000));
    CHECK(!elden_haptics_queue_step(&q,119000));CHECK(!v.live[0]);
    CHECK(!elden_haptics_queue_submit(&q,7,1,2,120000,10000,3,1,32767));
    CHECK(elden_haptics_queue_step(&q,120000)==EldenHapticsRequestDenied&&v.starts[0]==1);
    CHECK(!elden_haptics_queue_submit(&q,7,1,3,219000,10000,3,1,32767));CHECK(!elden_haptics_queue_step(&q,219000));CHECK(v.starts[0]==2);
    /* Half-start failure and failed stop retain live owner, deny new work,
     * report failure only after the stop callback actually succeeds. */
    init(&q,&v);v.failed_start=2;v.failed_stop=1;
    CHECK(!elden_haptics_queue_submit(&q,7,1,1,0,10000,3,1,32767));CHECK(elden_haptics_queue_step(&q,0)==-71);
    CHECK(q.held&&q.stopping&&q.active_index>=0&&v.live[0]&&!v.live[1]&&!v.results);
    CHECK(elden_haptics_queue_submit(&q,7,1,2,1,10000,1,1,1)==EldenHapticsRequestDenied);
    CHECK(!elden_haptics_queue_terminal(&q));v.failed_stop=0;CHECK(!elden_haptics_queue_step(&q,1));
    CHECK(!v.live[0]&&elden_haptics_queue_terminal(&q)&&v.result[0].state==EldenHapticsFailed);
    init(&q,&v);CHECK(!elden_haptics_queue_submit(&q,7,1,1,0,10000,1,1,32767));v.epoch=2;
    CHECK(elden_haptics_queue_step(&q,0)==-13&&v.starts[0]==0&&v.result[0].state==EldenHapticsFailed);
    init(&q,&v);for(unsigned i=0;i<32;++i)CHECK(!elden_haptics_queue_submit(&q,7,1,i+1,1000000,10000,1,1,16384));
    CHECK(elden_haptics_queue_submit(&q,7,1,33,1000000,10000,1,1,16384)==EldenHapticsRequestBusy);
    CHECK(!elden_haptics_queue_stop(&q,0)&&elden_haptics_queue_terminal(&q)&&v.results==32&&v.starts[0]==0);
    init(&q,&v);CHECK(!elden_haptics_queue_submit(&q,7,1,1,100,10000,1,1,32767));CHECK(!elden_haptics_queue_step(&q,100));
    CHECK(elden_haptics_queue_step(&q,99)==EldenHapticsRequestClock&&!v.live[0]&&elden_haptics_queue_terminal(&q));
    init(&q,&v);CHECK(!elden_haptics_queue_submit(&q,7,1,1,0,10000,1,1,0));CHECK(!elden_haptics_queue_step(&q,0)&&v.results==1&&!v.starts[0]);
    init(&q,&v);CHECK(!elden_haptics_queue_submit(&q,7,1,1,0,10000,1,1,32767));
    CHECK(elden_haptics_queue_step(&q,5001)==EldenHapticsRequestStale&&v.starts[0]==0);
    printf("PASS: %u actual haptic queue checks: copied gains, rolling budgets, session/ID/clock, both stop outcomes, deadlines; mechanical callbacks modeled\n",checks);
}
