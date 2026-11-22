/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Deterministic pthread race fences for the guarded J317 orientation
 * consumer. Mutex/cond gates pin provider and sink callbacks at exact
 * instruction boundaries while a second thread calls begin/stop/status —
 * no timing assumptions. All provider data is host-only synthetic fixture
 * output; nothing here qualifies a real J317/SPU/UI path. */
#include "Orientation.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {pthread_mutex_t m;pthread_cond_t c;int active,armed,released;} Gate;
static void gate_init(Gate*g){pthread_mutex_init(&g->m,0);pthread_cond_init(&g->c,0);g->active=0;g->armed=0;g->released=0;}
/* A gate blocks its callback only while active; an inactive gate is a plain
 * pass-through so sibling callbacks on the same operation never deadlock. */
static void gate_hold(Gate*g){pthread_mutex_lock(&g->m);if(!g->active){pthread_mutex_unlock(&g->m);return;}g->armed=1;pthread_cond_broadcast(&g->c);while(!g->released)pthread_cond_wait(&g->c,&g->m);g->active=0;pthread_mutex_unlock(&g->m);}
static void gate_wait_armed(Gate*g){pthread_mutex_lock(&g->m);while(!g->armed)pthread_cond_wait(&g->c,&g->m);pthread_mutex_unlock(&g->m);}
static void gate_release(Gate*g){pthread_mutex_lock(&g->m);g->released=1;pthread_cond_broadcast(&g->c);pthread_mutex_unlock(&g->m);}

typedef struct {
    EldenMotionConsumer *cons;
    const EldenMotionSample *q;unsigned n,at;
    uint64_t owner,epoch;
    Gate hold_next,hold_oe,hold_map,hold_sink,hold_desc;
    unsigned desc_calls,oe_calls,next_calls,map_calls,sink_calls;
    int reentry,begin_in_sink_rc,sample_in_sink_rc,stop_in_sink_rc;
    int32_t published[8];unsigned pub_count;
    /* thread results */
    int op_rc;uint64_t out_gen;
} Peer;
static int r_desc(void*c,uint32_t kind,EldenMotionSpec*s){
    Peer*p=c;p->desc_calls++;gate_hold(&p->hold_desc);
    static const EldenMotionSpec acc={{0xa1,0xb2},EldenMotionAccel,EldenMotionG,16*65536,{1,2,3}};
    static const EldenMotionSpec gyr={{0xc3,0xd4},EldenMotionGyro,EldenMotionRadiansPerSecond,64*65536,{1,2,3}};
    *s=kind==EldenMotionAccel?acc:gyr;return 0;
}
static int r_oe(void*c,uint64_t*o,uint64_t*e){Peer*p=c;p->oe_calls++;gate_hold(&p->hold_oe);*o=p->owner;*e=p->epoch;return 0;}
static int r_map(void*c,uint64_t t,uint64_t*m){Peer*p=c;p->map_calls++;gate_hold(&p->hold_map);*m=t+1000;return 0;}
static int r_next(void*c,EldenMotionSample*s){Peer*p=c;p->next_calls++;gate_hold(&p->hold_next);if(p->at>=p->n)return 0;*s=p->q[p->at++];return 1;}
static int r_sink(void*c,int32_t axis){
    Peer*p=c;p->sink_calls++;gate_hold(&p->hold_sink);
    if(p->reentry){
        p->begin_in_sink_rc=elden_motion_consumer_begin(p->cons,&(EldenMotionProvider){p,r_desc,r_oe,r_map,r_next},r_sink,p,150000,&p->out_gen);
        p->sample_in_sink_rc=elden_motion_consumer_sample(p->cons,p->cons->generation);
        p->stop_in_sink_rc=elden_motion_consumer_stop(p->cons,p->cons->generation);
    }
    if(p->pub_count<8)p->published[p->pub_count++]=axis;return 0;
}
static void peer_init(Peer*p,EldenMotionConsumer*c){
    memset(p,0,sizeof(*p));p->cons=c;p->owner=7;p->epoch=11;
    gate_init(&p->hold_next);gate_init(&p->hold_oe);gate_init(&p->hold_map);gate_init(&p->hold_sink);gate_init(&p->hold_desc);
}
static EldenMotionProvider rprov(Peer*p){return (EldenMotionProvider){p,r_desc,r_oe,r_map,r_next};}
#define ACC(x,y,z,t) (EldenMotionSample){{0xa1,0xb2},(t),{(x),(y),(z)},EldenMotionAccel,EldenMotionG,3}
#define HOLD 150000
#define STEP 2880000u
static uint64_t arm(EldenMotionConsumer*c,Peer*p){
    EldenMotionProvider pr=rprov(p);uint64_t gen=0;
    CHECK(!elden_motion_consumer_begin(c,&pr,r_sink,p,HOLD,&gen)&&gen);return gen;
}
typedef struct{EldenMotionConsumer*c;Peer*p;uint64_t gen;int rc;uint64_t out_gen;} Job;
static void*t_sample(void*a){Job*j=a;j->rc=elden_motion_consumer_sample(j->c,j->gen);return 0;}
static void*t_stop(void*a){Job*j=a;j->rc=elden_motion_consumer_stop(j->c,j->gen);return 0;}
static void*t_begin(void*a){Job*j=a;EldenMotionProvider pr=rprov(j->p);j->rc=elden_motion_consumer_begin(j->c,&pr,r_sink,j->p,HOLD,&j->out_gen);return 0;}
static void*t_status(void*a){Job*j=a;EldenMotionConsumerStatus st;j->rc=elden_motion_consumer_status(j->c,&st);return 0;}
static void spawn(pthread_t*t,void*(*f)(void*),Job*j){CHECK(!pthread_create(t,0,f,j));}
static void join(pthread_t t){CHECK(!pthread_join(t,0));}

int main(void){
    /* 1. Sample-vs-stop: a sample inside provider next() holds the claim; a
     * foreign stop posts the deferred tag and returns immediately; the holder
     * applies it at the post-callback fence before any publication. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};p.q=run;p.n=2;
     uint64_t gen=arm(&c,&p);
     p.hold_next.active=1;
     Job sj={&c,&p,gen,0,0},tj={&c,&p,gen,0,0};pthread_t s,t;
     spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_next);
     CHECK(c.busy==1);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);            /* deferred post */
     CHECK(c.state!=EldenConsStopped);                        /* not applied yet */
     gate_release(&p.hold_next);join(s);
     CHECK(sj.rc==EldenOrientationDisabled);                  /* -125 at the fence */
     CHECK(c.state==EldenConsStopped&&c.busy==0&&c.stop_deferred==0); /* tag consumed */
     CHECK(elden_motion_consumer_sample(&c,gen)==EldenOrientationDisabled);}

    /* 2. Stop inside owner_epoch aborts before next() runs. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1)};p.q=run;p.n=1;
     uint64_t gen=arm(&c,&p);
     p.hold_oe.active=1;
     Job sj={&c,&p,gen,0,0},tj={&c,&p,gen,0,0};pthread_t s,t;
     spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_oe);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);
     gate_release(&p.hold_oe);join(s);
     CHECK(sj.rc==EldenOrientationDisabled&&p.at==0);         /* next never ran */
     CHECK(c.state==EldenConsStopped);}

    /* 3. Stop inside map_time aborts before classifier/sink; the pending
     * candidate is cleared. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1)};p.q=run;p.n=1;
     uint64_t gen=arm(&c,&p);
     p.hold_map.active=1;
     Job sj={&c,&p,gen,0,0},tj={&c,&p,gen,0,0};pthread_t s,t;
     spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_map);
     spawn(&t,t_stop,&tj);join(t);CHECK(tj.rc==0);
     gate_release(&p.hold_map);join(s);
     CHECK(sj.rc==EldenOrientationDisabled&&c.state==EldenConsStopped&&c.rotation.candidate==0);}

    /* 4. Sample-vs-begin: a concurrent begin inside a held next() refuses
     * before reaching its own provider's describe(). */
    {EldenMotionConsumer c={0};Peer a,b;peer_init(&a,&c);peer_init(&b,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1)};a.q=run;a.n=1;
     uint64_t gen=arm(&c,&a);
     a.hold_next.active=1;
     Job sj={&c,&a,gen,0,0},bj={&c,&b,0,0,0};pthread_t s,t;
     spawn(&s,t_sample,&sj);gate_wait_armed(&a.hold_next);
     spawn(&t,t_begin,&bj);join(t);
     CHECK(bj.rc==EldenOrientationBusy&&b.desc_calls==0);     /* claim refused */
     gate_release(&a.hold_next);join(s);CHECK(sj.rc==0);
     /* After the run completes the live generation still refuses re-arm. */
     CHECK(elden_motion_consumer_begin(&c,&(EldenMotionProvider){&b,r_desc,r_oe,r_map,r_next},r_sink,&b,HOLD,&bj.out_gen)==EldenOrientationBusy);}

    /* 5. Status inside a held provider callback and inside the sink returns
     * Busy — never a torn snapshot. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};p.q=run;p.n=2;
     uint64_t gen=arm(&c,&p);
     p.hold_next.active=1;
     Job sj={&c,&p,gen,0,0},st={&c,&p,0,0,0};pthread_t s,t;
     spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_next);
     spawn(&t,t_status,&st);join(t);CHECK(st.rc==EldenOrientationBusy);
     gate_release(&p.hold_next);join(s);CHECK(sj.rc==0);
     /* Now inside the sink callback of the publishing sample. */
     p.hold_sink.active=1;sj.rc=0;spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_sink);
     st.rc=0;spawn(&t,t_status,&st);join(t);CHECK(st.rc==EldenOrientationBusy);
     gate_release(&p.hold_sink);join(s);CHECK(sj.rc==1);      /* published */
     CHECK(elden_motion_consumer_status(&c,&(EldenMotionConsumerStatus){0})==0);}

    /* 6. Concurrent begins on a fresh consumer: exactly one winner and the
     * loser refuses before any provider describe. */
    {EldenMotionConsumer c={0};Peer a,b;peer_init(&a,&c);peer_init(&b,&c);
     a.hold_desc.active=1;
     Job ja={&c,&a,0,0,0},jb={&c,&b,0,0,0};pthread_t t1,t2;
     spawn(&t1,t_begin,&ja);gate_wait_armed(&a.hold_desc);    /* A inside describe */
     spawn(&t2,t_begin,&jb);join(t2);
     CHECK(jb.rc==EldenOrientationBusy&&b.desc_calls==0);
     gate_release(&a.hold_desc);join(t1);
     CHECK(ja.rc==0&&ja.out_gen!=0&&c.state==EldenConsReady);
     /* A second begin while the winner's generation is live refuses. */
     uint64_t g2=0;CHECK(elden_motion_consumer_begin(&c,&(EldenMotionProvider){&b,r_desc,r_oe,r_map,r_next},r_sink,&b,HOLD,&g2)==EldenOrientationBusy);}

    /* 7. Owner drift while owner_epoch is held is terminal: the pending
     * candidate clears and the generation fences forever. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,10),ACC(0,65536,0,10+STEP),ACC(0,65536,0,20)};p.q=run;p.n=3;
     uint64_t gen=arm(&c,&p);
     CHECK(elden_motion_consumer_sample(&c,gen)==0);          /* candidate armed */
     CHECK(c.rotation.candidate==2&&p.at==1);
     p.hold_oe.active=1;
     Job sj={&c,&p,gen,0,0};pthread_t s;
     spawn(&s,t_sample,&sj);gate_wait_armed(&p.hold_oe);
     p.owner=8;                                                /* drift while held */
     gate_release(&p.hold_oe);join(s);
     CHECK(sj.rc==EldenMotionStale&&p.at==1);                  /* refused before next() */
     CHECK(c.state==EldenConsStopped&&c.rotation.candidate==0);
     CHECK(elden_motion_consumer_sample(&c,gen)==EldenOrientationDisabled);}

    /* 8. Reentry inside the sink: begin and sample are fenced Busy, stop
     * posts the deferred tag and the in-flight publication is aborted —
     * the axis is rolled back, dropped counts, published does not. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);p.reentry=1;
     EldenMotionSample run[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};p.q=run;p.n=2;
     uint64_t gen=arm(&c,&p);
     CHECK(elden_motion_consumer_sample(&c,gen)==0);
     CHECK(elden_motion_consumer_sample(&c,gen)==EldenOrientationDisabled);
     CHECK(p.begin_in_sink_rc==EldenOrientationBusy&&p.sample_in_sink_rc==EldenOrientationBusy&&p.stop_in_sink_rc==0);
     CHECK(p.pub_count==1&&c.published==0&&c.dropped==1&&c.rotation.axis==0);
     CHECK(c.state==EldenConsStopped);}

    /* 9. Generation exhaustion refuses before any provider probe and leaves
     * the out-parameter untouched — an identity is never reused. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     uint64_t gen=arm(&c,&p);CHECK(!elden_motion_consumer_stop(&c,gen));
     c.generation=UINT64_MAX;                                  /* exhausted slot */
     p.desc_calls=p.oe_calls=p.map_calls=0;uint64_t out=0xdead;
     CHECK(elden_motion_consumer_begin(&c,&(EldenMotionProvider){&p,r_desc,r_oe,r_map,r_next},r_sink,&p,HOLD,&out)==EldenMotionArgument);
     CHECK(out==0xdead&&p.desc_calls==0&&p.oe_calls==0&&p.map_calls==0);}

    /* 10. Terminal rebind: after a drift-terminal generation a fresh begin
     * arms a new generation; the old one stays fenced and no hold credit
     * carries across the discontinuity. */
    {EldenMotionConsumer c={0};Peer p;peer_init(&p,&c);
     EldenMotionSample run[]={ACC(0,65536,0,10)};p.q=run;p.n=1;
     uint64_t gen=arm(&c,&p);
     p.owner=9;CHECK(elden_motion_consumer_sample(&c,gen)==EldenMotionStale);
     CHECK(c.state==EldenConsStopped);
     p.owner=9;uint64_t gen2=arm(&c,&p);CHECK(gen2!=gen);
     CHECK(elden_motion_consumer_sample(&c,gen)==EldenMotionStale);   /* old gen fenced */
     EldenMotionSample run2[]={ACC(0,65536,0,30),ACC(0,65536,0,30+STEP)};
     p.q=run2;p.n=2;p.at=0;
     CHECK(elden_motion_consumer_sample(&c,gen2)==0);                 /* fresh hold */
     CHECK(c.rotation.candidate==2&&c.published==0);
     CHECK(elden_motion_consumer_sample(&c,gen2)==1&&c.published_axis==2);
     CHECK(elden_motion_consumer_sample(&c,gen)==EldenMotionStale);}

    /* 11. Concurrent samples serialize the whole operation: the second one
     * refuses Busy inside the held provider callback — it never reaches the
     * provider — and completes cleanly once the claim is free. */
    {EldenMotionConsumer c={0};Peer a,b;peer_init(&a,&c);peer_init(&b,&c);
     EldenMotionSample run[]={ACC(0,65536,0,1),ACC(0,65536,0,1+STEP)};a.q=run;a.n=2;
     uint64_t gen=arm(&c,&a);
     a.hold_next.active=1;
     Job s1={&c,&a,gen,0,0},s2={&c,&a,gen,0,0};pthread_t t1,t2;
     spawn(&t1,t_sample,&s1);gate_wait_armed(&a.hold_next);
     spawn(&t2,t_sample,&s2);join(t2);
     CHECK(s2.rc==EldenOrientationBusy&&a.next_calls==1);       /* no second next() */
     gate_release(&a.hold_next);join(t1);CHECK(s1.rc==0);
     /* After the first sample unwinds the same generation still runs. */
     CHECK(elden_motion_consumer_sample(&c,gen)==1&&c.published==1);
     CHECK(c.busy==0);}

    printf("PASS %u\n",checks);return 0;
}
