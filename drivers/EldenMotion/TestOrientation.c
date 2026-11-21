/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY synthetic provider/sink fixtures for the guarded J317 orientation
 * consumer boundary. Every gravity vector, timestamp, mount, SUID and rotation
 * published here is a deterministic host fixture — none of it can enter a
 * native default profile and none of it is hardware/provider qualification.
 * The real classifier (elden_motion_rotation) is linked from the prepared
 * Core.c; this test only exercises the consumer fences around it. */
#include "Orientation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    EldenMotionSpec accel,gyro;uint64_t owner,epoch;
    const EldenMotionSample *q;unsigned n,at;
    const uint64_t *mach_seq;unsigned mach_n,mach_i;
    int fail_desc,fail_owner,fail_map,fail_next,next_ret,map_back;
    EldenMotionConsumer *cons;int stop_in_sink,sample_in_sink,fail_sink,inner_rc;
    int32_t published[32];unsigned pub_count;
} Peer;
static int p_desc(void *c,uint32_t kind,EldenMotionSpec *s){Peer*p=c;if(p->fail_desc)return -13;*s=kind==EldenMotionAccel?p->accel:p->gyro;return 0;}
static int p_own(void *c,uint64_t *o,uint64_t *e){Peer*p=c;if(p->fail_owner)return -13;*o=p->owner;*e=p->epoch;return 0;}
static int p_map(void *c,uint64_t t,uint64_t *m){
    Peer*p=c;if(p->fail_map)return -13;
    if(p->mach_seq){*m=p->mach_i<p->mach_n?p->mach_seq[p->mach_i++]:0;return 0;}
    *m=p->map_back?1000-t:t+1000;return 0;
}
static int p_next(void *c,EldenMotionSample *s){
    Peer*p=c;if(p->fail_next)return -13;if(p->next_ret)return p->next_ret;
    if(p->at>=p->n)return 0;*s=p->q[p->at++];return 1;
}
static int p_sink(void *c,int32_t axis){
    Peer*p=c;
    if(p->stop_in_sink)p->inner_rc=elden_motion_consumer_stop(p->cons,p->cons->generation);
    else if(p->sample_in_sink)p->inner_rc=elden_motion_consumer_sample(p->cons,p->cons->generation);
    if(p->fail_sink)return -13;
    if(p->pub_count<32)p->published[p->pub_count++]=axis;return 0;
}
static void setup(Peer*p){
    memset(p,0,sizeof(*p));
    p->accel=(EldenMotionSpec){{0xa1,0xb2},EldenMotionAccel,EldenMotionG,16*65536,{1,2,3}};
    p->gyro=(EldenMotionSpec){{0xc3,0xd4},EldenMotionGyro,EldenMotionRadiansPerSecond,64*65536,{1,2,3}};
    p->owner=7;p->epoch=11;
}
static EldenMotionProvider prov(Peer*p){return (EldenMotionProvider){p,p_desc,p_own,p_map,p_next};}
#define ACC(x,y,z,t) (EldenMotionSample){{0xa1,0xb2},(t),{(x),(y),(z)},EldenMotionAccel,EldenMotionG,3}
#define GYR(x,y,z,t) (EldenMotionSample){{0xc3,0xd4},(t),{(x),(y),(z)},EldenMotionGyro,EldenMotionRadiansPerSecond,3}
#define HOLD 150000
#define STEP 2880000u /* 150000us hold at 19.2MHz SSC ticks */
static uint64_t arm(EldenMotionConsumer *c,Peer *p){
    if(c->enabled&&(c->state==EldenConsReady||c->state==EldenConsRunning))CHECK(!elden_motion_consumer_stop(c,c->generation));
    EldenMotionProvider pr=prov(p);uint64_t gen=0;p->cons=c;
    int rc=elden_motion_consumer_begin(c,&pr,p_sink,p,HOLD,&gen);CHECK(!rc&&gen);
    return gen;
}
static EldenMotionConsumerStatus peek(EldenMotionConsumer *c){
    EldenMotionConsumerStatus s;CHECK(!elden_motion_consumer_status(c,&s));return s;
}
int main(void){
    EldenMotionConsumer c;Peer p;uint64_t gen;memset(&c,0,sizeof(c));

    /* Disabled default: an unarmed consumer refuses everything. */
    CHECK(elden_motion_consumer_sample(&c,1)==-125&&elden_motion_consumer_stop(&c,1)==-125);

    /* Missing provider callbacks refuse before arming. */
    setup(&p);
    CHECK(elden_motion_consumer_begin(&c,0,p_sink,&p,HOLD,&gen)==-22);
    EldenMotionProvider pr=prov(&p);
    EldenMotionProvider no_desc=pr,no_own=pr,no_map=pr,no_next=pr;
    no_desc.describe=0;no_own.owner_epoch=0;no_map.map_time=0;no_next.next=0;
    CHECK(elden_motion_consumer_begin(&c,&no_desc,p_sink,&p,HOLD,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&no_own,p_sink,&p,HOLD,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&no_map,p_sink,&p,HOLD,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&no_next,p_sink,&p,HOLD,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&pr,0,&p,HOLD,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,0,&gen)==-22);
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,1000001,&gen)==-22);

    /* Bad provider outputs refuse: failed describe, zero owner, failed owner,
     * bad accel mount (det -1), wrong accel unit, zero SUID, bad gyro spec,
     * nonmonotonic time-map probe, failed map probe. */
    setup(&p);pr=prov(&p);p.fail_desc=1;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.owner=0;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.fail_owner=1;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.accel.mount[0]=-1; /* {-1,2,3}: neg1 inv0 -> det -1 */
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.accel.unit=EldenMotionRadiansPerSecond;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.accel.suid=(EldenMotionSuid){0,0};
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.gyro.unit=EldenMotionG;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-22);
    setup(&p);pr=prov(&p);p.map_back=1; /* map(1)=999,map(2)=998 nonmonotonic */
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-110);
    setup(&p);pr=prov(&p);p.fail_map=1;
    CHECK(elden_motion_consumer_begin(&c,&pr,p_sink,&p,HOLD,&gen)==-110);

    /* Valid begin + a held +Y publish through the real classifier. */
    setup(&p);EldenMotionSample run1[]={ACC(0,65536,0,100),ACC(0,65536,0,100+STEP)};
    p.q=run1;p.n=2;gen=arm(&c,&p);
    CHECK(peek(&c).state==EldenConsReady);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(peek(&c).pending_candidate==2&&peek(&c).published_axis==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1);
    CHECK(p.pub_count==1&&p.published[0]==2&&peek(&c).published_axis==2&&peek(&c).published==1);
    CHECK(elden_motion_consumer_sample(&c,gen)==2); /* provider drained */

    /* A second held candidate transitions the published axis -X. */
    setup(&p);EldenMotionSample run2[]={ACC(0,65536,0,10),ACC(0,65536,0,10+STEP),
        ACC(-65536,0,0,10+STEP+50),ACC(-65536,0,0,10+2*STEP+50)};
    p.q=run2;p.n=4;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==2);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==-1);
    CHECK(p.pub_count==2&&p.published[0]==2&&p.published[1]==-1);

    /* The real mount matrix is applied: accel mount {-2,1,3} maps a sensor +Y
     * onto device -X, so the published axis is -1, not +2. */
    setup(&p);p.accel.mount[0]=-2;p.accel.mount[1]=1;EldenMotionSample run3[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};
    p.q=run3;p.n=2;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==-1);

    /* m/s2 accel input is normalized to G before the classifier. */
    setup(&p);p.accel.unit=EldenMotionMetersPerSecond2;p.accel.max_abs_q16=200*65536;
    EldenMotionSample run4[]={ACC(0,643310,0,1),ACC(0,643310,0,1+STEP)}; /* 9.81 m/s2 ~ 1g */
    run4[0].unit=run4[1].unit=EldenMotionMetersPerSecond2;
    p.q=run4;p.n=2;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==2);

    /* The hold runs on mapped mach time, not source ticks: source ticks spaced
     * 1M apart but only ~100us apart in mach must not publish (their mapped
     * span stays far under the 150000us hold). */
    setup(&p);uint64_t seqA[]={1001,1002,100,200,300,400}; /* probe, then +100us/sample */
    EldenMotionSample runA[]={ACC(0,65536,0,1000000),ACC(0,65536,0,2000000),ACC(0,65536,0,3000000),ACC(0,65536,0,4000000)};
    p.mach_seq=seqA;p.mach_n=6;p.q=runA;p.n=4;gen=arm(&c,&p);
    for(unsigned i=0;i<4;i++)CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(peek(&c).published==0);

    /* Reaching the hold in mapped time publishes even when the source ticks
     * are only 1 apart (the source rate is irrelevant to the hold). */
    setup(&p);uint64_t seqB[]={1001,1002,100,100+150000};
    EldenMotionSample runB[]={ACC(0,65536,0,50),ACC(0,65536,0,51)};
    p.mach_seq=seqB;p.mach_n=4;p.q=runB;p.n=2;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==2);

    /* Just under the mapped hold boundary (149999us) still refuses to publish,
     * then a further mapped sample crosses it and publishes. */
    setup(&p);uint64_t seqC[]={1001,1002,100,100+149999,100+151999};
    EldenMotionSample runC[]={ACC(0,65536,0,60),ACC(0,65536,0,61),ACC(0,65536,0,62)};
    p.mach_seq=seqC;p.mach_n=5;p.q=runC;p.n=3;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==0&&peek(&c).published==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1&&peek(&c).published_axis==2);

    /* A mach mapping that overflows the 19.2MHz conversion latches the clock. */
    setup(&p);uint64_t seqD[]={1001,1002,100,~0ull};
    EldenMotionSample runD[]={ACC(0,65536,0,70),ACC(0,65536,0,71)};
    p.mach_seq=seqD;p.mach_n=4;p.q=runD;p.n=2;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-110);
    CHECK(elden_motion_consumer_sample(&c,gen)==-110);

    /* Generation fences on sample and stop. */
    setup(&p);gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen+1)==-116);
    CHECK(elden_motion_consumer_stop(&c,gen+1)==-116);

    /* A stale/foreign provider is fenced: owner and epoch are re-checked on
     * every sample against the armed values. */
    setup(&p);EldenMotionSample run5[]={ACC(0,65536,0,5)};p.q=run5;p.n=1;gen=arm(&c,&p);
    p.owner=8;CHECK(elden_motion_consumer_sample(&c,gen)==-116);
    p.owner=7;p.epoch=12;CHECK(elden_motion_consumer_sample(&c,gen)==-116);

    /* Wrong SUID, wrong unit, unknown kind, zero ticks and out-of-range refuse. */
    setup(&p);EldenMotionSample bad_suid=ACC(0,65536,0,1);bad_suid.suid=(EldenMotionSuid){9,9};
    EldenMotionSample bad_unit=ACC(0,65536,0,2);bad_unit.unit=EldenMotionMetersPerSecond2;
    EldenMotionSample bad_kind=ACC(0,65536,0,3);bad_kind.kind=9;
    EldenMotionSample no_ticks=ACC(0,65536,0,0);
    EldenMotionSample oob=ACC(17*65536,0,0,4); /* >16g declared accel range */
    EldenMotionSample run6[]={bad_suid,bad_unit,bad_kind,no_ticks,oob};
    p.q=run6;p.n=5;gen=arm(&c,&p);
    for(unsigned i=0;i<5;i++)CHECK(elden_motion_consumer_sample(&c,gen)==-84);
    CHECK(peek(&c).rejected==5);

    /* Duplicate and reversed source timestamps refuse; reversed latches clock. */
    setup(&p);EldenMotionSample run7[]={ACC(0,65536,0,50),ACC(0,65536,0,50)};
    p.q=run7;p.n=2;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-116); /* duplicate ticks */
    setup(&p);EldenMotionSample run8[]={ACC(0,65536,0,200),ACC(0,65536,0,150),ACC(0,65536,0,500)};
    p.q=run8;p.n=3;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-110); /* reversed time */
    CHECK(elden_motion_consumer_sample(&c,gen)==-110); /* latched */

    /* A nonmonotonic source->mach mapping refuses and latches the clock. */
    setup(&p);EldenMotionSample run9[]={ACC(0,65536,0,10),ACC(0,65536,0,20),ACC(0,65536,0,30)};
    p.q=run9;p.n=3;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    p.map_back=1; /* later samples map to a decreasing mach */
    CHECK(elden_motion_consumer_sample(&c,gen)==-110);
    CHECK(elden_motion_consumer_sample(&c,gen)==-110);

    /* Flat and poor-quality input resets the pending candidate and refuses. */
    setup(&p);EldenMotionSample run10[]={ACC(0,65536,0,1),ACC(0,0,0,2),ACC(0,65536,0,3)};
    p.q=run10;p.n=3;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(peek(&c).pending_candidate==2);
    CHECK(elden_motion_consumer_sample(&c,gen)==-84); /* flat: candidate reset */
    CHECK(peek(&c).pending_candidate==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==0); /* recovers on good input */

    setup(&p);EldenMotionSample poor=ACC(0,65536,0,1);poor.accuracy=0;
    EldenMotionSample run11[]={poor};p.q=run11;p.n=1;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==-84); /* poor-quality refused */

    /* A valid gyro sample is consumed without touching the rotation; a
     * poor-quality or out-of-range gyro still refuses. */
    setup(&p);EldenMotionSample gyr_bad=GYR(65*65536,0,0,3); /* >64g declared gyro range */
    EldenMotionSample gyr_poor=GYR(0,0,0,2);gyr_poor.accuracy=0;
    EldenMotionSample run12[]={GYR(100,200,300,1),gyr_poor,gyr_bad};
    p.q=run12;p.n=3;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0); /* gyro consumed, no rotation */
    CHECK(peek(&c).published==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-84); /* poor gyro */
    CHECK(elden_motion_consumer_sample(&c,gen)==-84); /* out-of-range gyro */

    /* Stop during the pending sink callback aborts the publication and fences
     * the run; nothing publishes after stop. */
    setup(&p);EldenMotionSample run13[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP),ACC(0,65536,0,1+2*STEP)};
    p.q=run13;p.n=3;p.stop_in_sink=1;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-125); /* stop landed in sink */
    CHECK(p.inner_rc==0&&peek(&c).state==EldenConsStopped);
    CHECK(peek(&c).published==0&&peek(&c).dropped==1); /* publication aborted */
    CHECK(elden_motion_consumer_sample(&c,gen)==-125); /* no publication after stop */

    /* Sink callback failure rolls back: the candidate stays pending and a later
     * good sample retries the publication instead of losing it. */
    setup(&p);EldenMotionSample run14[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP),ACC(0,65536,0,1+2*STEP)};
    p.q=run14;p.n=3;p.fail_sink=1;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==-13); /* callback failure */
    CHECK(peek(&c).published==0&&peek(&c).published_axis==0);
    p.fail_sink=0;CHECK(elden_motion_consumer_sample(&c,gen)==1); /* retry publishes */
    CHECK(peek(&c).published_axis==2);

    /* A re-entrant sample inside the sink is fenced busy; the outer publish
     * still completes. */
    setup(&p);EldenMotionSample run15[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};
    p.q=run15;p.n=2;p.sample_in_sink=1;gen=arm(&c,&p);
    CHECK(elden_motion_consumer_sample(&c,gen)==0);
    CHECK(elden_motion_consumer_sample(&c,gen)==1);
    CHECK(p.inner_rc==-16&&p.pub_count==1&&p.published[0]==2);

    /* Provider next() returning an out-of-contract code or an error refuses. */
    setup(&p);gen=arm(&c,&p);p.next_ret=5;
    CHECK(elden_motion_consumer_sample(&c,gen)==-13);
    setup(&p);gen=arm(&c,&p);p.fail_next=1;
    CHECK(elden_motion_consumer_sample(&c,gen)==-13);

    /* Re-arming a live consumer refuses busy; stop then re-arm yields a new
     * generation, and the old generation is stale. */
    setup(&p);gen=arm(&c,&p);
    CHECK(elden_motion_consumer_begin(&c,&(EldenMotionProvider){&p,p_desc,p_own,p_map,p_next},p_sink,&p,HOLD,&gen)==-16);
    CHECK(elden_motion_consumer_stop(&c,gen)==0);
    uint64_t gen2=0;CHECK(!elden_motion_consumer_begin(&c,&(EldenMotionProvider){&p,p_desc,p_own,p_map,p_next},p_sink,&p,HOLD,&gen2));
    CHECK(gen2!=gen&&elden_motion_consumer_sample(&c,gen)==-116&&elden_motion_consumer_sample(&c,gen2)==2);

    /* Status is read-only and reflects the run. */
    setup(&p);gen=arm(&c,&p);EldenMotionConsumerStatus s=peek(&c);
    CHECK(s.state==EldenConsReady&&s.generation==gen&&s.owner==7&&s.epoch==11&&s.published_axis==0);

    printf("PASS: %u orientation-consumer checks (host-only synthetic fixtures; "
        "no provider/gravity/timestamp/calibration synthesized, no orientation "
        "node or 0x20/0x8a HID device, hardware+J317 absent)\n",checks);
}
