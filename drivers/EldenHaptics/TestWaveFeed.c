/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host test for connecting compiled calibrated waveform chunks to the native
 * serialized Broker/RTP worker. A source-bound recipe id arms the measured
 * dual-motor profile, then the SAME rtp stream plays bounded pull-source
 * chunks through ONE live session onto the model GENI/AW FIFOs. Carriers and
 * the linear LUT are explicitly SYNTHETIC host fixtures — they prove the
 * chunk/ordering/refusal contract, never measured motor equivalence or hardware. */
#define main previous_bus_main
#include "TestBus.c"
#undef main
#include "Broker.h"
#include "WaveFeed.h"
typedef struct {
    Model hardware;EldenAwChip chips[2];EldenHapticsBroker broker;
    unsigned lock,allocations,releases,fail_alloc,denied;
    unsigned feed_busy[2];        /* force almost_full once a burst is staged */
    EldenWaveCell history[480];
    EldenWaveRecipe recipe;EldenWaveEvent events[2];
    EldenWaveSource manifest[1];
} FeedPeer;
/* SYNTHETIC measured fixtures: linear identity LUT, constant and ramp carriers. */
static const EldenHapticsGainPoint lutP[]={{0,0},{32767,32767}};
static const EldenHapticsCalibration calib={lutP,2,32767,24000,480,1ull<<50};
static const EldenHapticsCalibration calibBadRate={lutP,2,32767,12000,480,1ull<<50};
static int16_t carHi[480],carRamp[960];
static EldenWaveCarrier cHi={carHi,480,480,0};
static EldenWaveCarrier cRamp={carRamp,960,0,0};
static EldenWaveProfile wave_profile(void){
    EldenWaveProfile p;memset(&p,0,sizeof(p));
    p.motor[0]=&calib;p.motor[1]=&calib;p.gain[0]=p.gain[1]=1;
    p.max_window_squares[0]=p.max_window_squares[1]=1ull<<40;
    p.max_window_on_frames[0]=p.max_window_on_frames[1]=480;
    p.max_dual_on_frames=480;p.window_frames=480;
    p.measured_min_payload_bps=60000;p.revision=1;p.enabled=1;return p;
}
static int b_admit(void *v,int cleanup){FeedPeer *p=v;CHECK(!p->lock);return p->denied&&!cleanup?-13:0;}
static void b_lock(void *v,int acquire){FeedPeer *p=v;CHECK(p->lock==(unsigned)!acquire);p->lock=(unsigned)acquire;}
static uint64_t b_now(void *v){return ((FeedPeer *)v)->hardware.now;}
static void *b_allocate(void *v,size_t n){FeedPeer *p=v;CHECK(!p->lock&&n<=1440000);p->allocations++;return p->allocations==p->fail_alloc?NULL:malloc(n);}
static void b_release(void *v,void *memory,size_t n){FeedPeer *p=v;CHECK(!p->lock&&memory);(void)n;p->releases++;free(memory);}
/* Chip io.read for the feed tests: delegates to the same Model bus, then models
 * the AW FIFO going almost-full exactly at a chunk's tail burst. It injects only
 * once the feed staged the WHOLE resident chunk but has not confirmed the last
 * burst (pulled==frames && confirmed<frames) — so the status poll passes while
 * frames remain to pull, the final pull stages the tail into feed[], and the
 * feed's own status read then reports the FIFO full. That reproduces the real
 * async full-race the fix addresses: a chunk pulled entirely into staging is
 * still NOT consumed until its tail FIFO write confirms. hardware is member 0,
 * so io.context can be the FeedPeer (== &p->hardware). */
static int wf_chip_read(void *v,uint8_t address,uint8_t reg,uint8_t *out,size_t bytes){
    FeedPeer *p=v;
    int rc=elden_haptics_bus_read(&p->hardware.bus,address,reg,out,bytes);if(rc)return rc;
    if(reg==1&&bytes==1){
        unsigned m=(unsigned)(address-0x5a);
        const EldenWaveFeed *w=&p->broker.wave;
        if(m<2&&p->feed_busy[m]&&w->have_chunk&&w->pulled[m]==w->chunk.frames&&w->confirmed[m]<w->chunk.frames)out[0]|=8;
    }
    return rc;
}
/* Builds a 3-chunk ramp recipe (1440 frames): event0 frames0..960 uses cRamp
 * (one-shot), event1 frames960..1440 uses cHi -> distinguishable ordered pcm. */
static FeedPeer *feed_setup(const uint8_t *fw,size_t n,const EldenWaveProfile *prof,int bind,int bad_carrier){
    FeedPeer *p=calloc(1,sizeof(*p));CHECK(p);Model *m=&p->hardware;initial(m);acquire(m);m->firmware=fw;m->firmware_bytes=n;
    EldenAwIo io={p,wf_chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned i=0;i<2;i++){CHECK(!elden_aw_bind(&p->chips[i],&io,(uint8_t)(0x5a+i)));CHECK(!elden_aw_initialize(&p->chips[i],fw,n,64));m->motor[i].reg[1]=16;}
    p->events[0]=(EldenWaveEvent){&cRamp,NULL,0,960,32767,0};
    p->events[1]=(EldenWaveEvent){bad_carrier?0:&cHi,NULL,960,480,32767,0};
    p->recipe=(EldenWaveRecipe){p->events,2,NULL,NULL,1440};
    p->manifest[0]=(EldenWaveSource){7,&p->recipe};
    EldenSharedBudgetLimits shared={1000000,{1000000,1000000},1000000,{1ull<<40,1ull<<40},3};
    EldenRtpLimits rtp={60000000,10000,60000,24000,{1ull<<40,1ull<<40},{24000,24000},32,{64,64},{127,127},3};
    EldenHapticsBrokerIo bio={p,b_admit,b_lock,b_now,b_allocate,b_release};EldenAwChip *chips[]={&p->chips[0],&p->chips[1]};
    CHECK(!elden_broker_init(&p->broker,&bio,chips,NULL,&shared,&rtp,NULL,0));
    if(bind)CHECK(!elden_broker_bind_recipes(&p->broker,p->manifest,1,prof,1,p->history,480));
    return p;
}
static void open(FeedPeer *p,uint64_t *owner,uint64_t *epoch){CHECK(!elden_broker_open(&p->broker,owner,epoch));}
/* Expected compiled pcm for one motor at an absolute frame: re-runs the same
 * deterministic compiler over a scratch chunk bank. */
static void expected(FeedPeer *p,const EldenWaveProfile *prof,int8_t out[2][1440]){
    EldenWaveCompiler s;memset(&s,0,sizeof(s));uint64_t g;EldenWaveChunk ck;
    EldenWaveCell scratch[480];
    CHECK(!elden_wave_compiler_begin(&s,&p->recipe,prof,1,scratch,480,&g));
    uint32_t at=0;
    while(at<p->recipe.total_frames){
        int rc=elden_wave_compiler_next(&s,g,at,&ck);CHECK(rc>=0);
        for(unsigned m=0;m<2;m++)memcpy(out[m]+ck.first_frame,ck.pcm[m],ck.frames);
        at+=ck.frames;(void)elden_wave_compiler_fed(&s,g,ck.frames);if(rc==1)break;
    }
}
/* Drives the worker until both FIFOs received 'total' bytes or a bound. */
static int run(FeedPeer *p,uint32_t total){
    for(unsigned i=0;i<200000;i++){
        if(p->broker.rtp.sent[0]>=total&&p->broker.rtp.sent[1]>=total)return 0;
        int rc=elden_broker_step(&p->broker);if(rc<0)return rc;
    }
    return -1;
}
static void finish_rtp(FeedPeer *p){
    for(unsigned i=0;i<2;i++){p->hardware.motor[i].reg[0x3f]=0;p->hardware.motor[i].reg[2]=1;}
}
/* Steps the broker until cond(p) holds or a bound elapses. */
static int step_until(FeedPeer *p,int(*cond)(const FeedPeer *),unsigned bound){
    for(unsigned i=0;i<bound;i++){if(cond(p))return 0;if(elden_broker_step(&p->broker)<0)return -1;}
    return -1;
}
/* Both motors hold a staged-but-unconfirmed burst of the current chunk. */
static int both_staged(const FeedPeer *p){
    return p->broker.wave.pulled[0]>p->broker.wave.confirmed[0]&&
           p->broker.wave.pulled[1]>p->broker.wave.confirmed[1];
}
/* Motor0 confirmed every frame of the resident chunk while the sibling lags. */
static int m0_chunk_full(const FeedPeer *p){
    return p->broker.wave.have_chunk&&p->broker.wave.confirmed[0]==p->broker.wave.chunk.frames;
}
static int m0_staged(const FeedPeer *p){
    return p->broker.wave.pulled[0]>p->broker.wave.confirmed[0];
}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t fw[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);size_t n=fread(fw,1,sizeof(fw),f);CHECK(n==1436&&!ferror(f));CHECK(!fclose(f));
    for(int i=0;i<960;i++){if(i<480)carHi[i]=16000;carRamp[i]=(int16_t)((i*97)%32000-16000);}
    EldenWaveProfile prof=wave_profile();uint64_t owner,epoch;EldenHapticsResult result;
    /* ---- unbound recipe path refuses ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,0,0);open(p,&owner,&epoch);
     CHECK(elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767)==-13);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- bind validation: second bind, bad manifest, small ring ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(elden_broker_bind_recipes(&p->broker,p->manifest,1,&prof,1,p->history,480)==-13);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    {FeedPeer *p=feed_setup(fw,n,&prof,0,0);open(p,&owner,&epoch);
     EldenWaveSource bad[2]={{3,&p->recipe},{2,&p->recipe}};           /* unsorted */
     CHECK(elden_broker_bind_recipes(&p->broker,bad,2,&prof,1,p->history,480)==-22);
     EldenWaveSource tagged[1]={{EldenWaveRecipeTag|7,&p->recipe}};    /* tagged id */
     CHECK(elden_broker_bind_recipes(&p->broker,tagged,1,&prof,1,p->history,480)==-22);
     CHECK(elden_broker_bind_recipes(&p->broker,p->manifest,1,&prof,1,p->history,479)==-22); /* short ring */
     CHECK(elden_broker_bind_recipes(&p->broker,p->manifest,1,NULL,1,p->history,480)==-22);   /* no profile */
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- happy path: 3 bounded chunks streamed through ONE session ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(!elden_broker_step(&p->broker)&&p->broker.current_kind==2);
     CHECK(p->hardware.motor[0].starts==1&&p->hardware.motor[1].starts==1);    /* simultaneous GO */
     CHECK(!run(p,1440));
     /* no dropped/replayed frames: FIFO holds exactly the compiled stream */
     int8_t exp[2][1440];expected(p,&prof,exp);
     for(unsigned m=0;m<2;m++){CHECK(p->hardware.motor[m].rtp_count==1440);
         CHECK(!memcmp(p->hardware.motor[m].rtp,exp[m],1440));}
     CHECK(p->broker.wave.compiler.drained==1440&&p->broker.wave.compiler.pending==0);
     finish_rtp(p);CHECK(!elden_broker_step(&p->broker));
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsComplete);
     CHECK(!elden_shared_budget_active(&p->broker.budget));
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- missing source carrier refuses at arm, never feeds ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,1);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(elden_broker_step(&p->broker)<0);
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);
     CHECK(p->hardware.motor[0].rtp_count==0&&p->hardware.motor[0].starts==0);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- partial motor coverage refuses ---- */
    {EldenWaveProfile one=prof;one.motor[1]=NULL;
     FeedPeer *p=feed_setup(fw,n,&one,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(elden_broker_step(&p->broker)<0);
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- disabled / stale-revision profile refuses ---- */
    {EldenWaveProfile off=prof;off.enabled=0;
     FeedPeer *p=feed_setup(fw,n,&off,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(elden_broker_step(&p->broker)<0);
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    {EldenWaveProfile stale=prof;stale.revision=1;   /* manifest accepts rev2 -> stale */
     FeedPeer *p=feed_setup(fw,n,&stale,0,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_bind_recipes(&p->broker,p->manifest,1,&stale,2,p->history,480));
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(elden_broker_step(&p->broker)<0);
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- bad LUT rate refuses at admission ---- */
    {EldenWaveProfile badlut=prof;badlut.motor[1]=&calibBadRate;
     FeedPeer *p=feed_setup(fw,n,&badlut,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(elden_broker_step(&p->broker)<0);
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsFailed);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- unknown recipe id / stale request id refuse ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(elden_broker_submit_recipe(&p->broker,owner,epoch,1,99,0,32767)==-116);   /* unknown recipe */
     CHECK(elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767)==0);
     CHECK(elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767)==-116);    /* duplicate/stale id */
     CHECK(elden_broker_submit_recipe(&p->broker,owner+9,epoch,2,7,0,32767)==-13);   /* wrong owner */
     CHECK(!elden_broker_cancel(&p->broker,owner,epoch,1)&&!elden_broker_step(&p->broker));
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsCancelled);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- cancellation mid-run settles through the broker, drops unfed ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(!elden_broker_step(&p->broker)&&p->broker.current_kind==2);
     CHECK(!elden_broker_cancel(&p->broker,owner,epoch,1));
     CHECK(!elden_broker_step(&p->broker));
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsCancelled);
     CHECK(!elden_shared_budget_active(&p->broker.budget));
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- stop mid-run: cancelled, never false-complete ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(!elden_broker_step(&p->broker)&&p->broker.current_kind==2);
     /* elden_broker_stop is terminal teardown: the result slot is Cancelled in
      * the mail (readable before stop only), so assert the settled state —
      * stream cancelled, worker released, no held chip/budget, never Complete. */
     CHECK(!elden_broker_stop(&p->broker));
     CHECK(p->broker.rtp.state==EldenRtpCancelled&&p->broker.current_kind==0);
     CHECK(!elden_shared_budget_active(&p->broker.budget));
     free(p);}
    /* ---- almost-full pauses the feed and resumes, never a duplicate ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     CHECK(!elden_broker_step(&p->broker));
     p->hardware.motor[0].reg[1]=24;p->hardware.motor[1].reg[1]=24;   /* almost_full+empty */
     unsigned m0=p->hardware.motor[0].rtp_count,m1=p->hardware.motor[1].rtp_count;
     CHECK(!elden_broker_step(&p->broker));
     CHECK(p->hardware.motor[0].rtp_count==m0&&p->hardware.motor[1].rtp_count==m1); /* paused */
     p->hardware.motor[0].reg[1]=16;p->hardware.motor[1].reg[1]=16;
     CHECK(!run(p,1440));finish_rtp(p);CHECK(!elden_broker_step(&p->broker));
     int8_t exp[2][1440];expected(p,&prof,exp);
     CHECK(!memcmp(p->hardware.motor[0].rtp,exp[0],1440));
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- staged-but-unwritten tail: chunk stays resident, never fed, cancel ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     p->feed_busy[0]=p->feed_busy[1]=1;                  /* both tail writes go full */
     CHECK(!step_until(p,both_staged,200000));           /* both staged chunk0's tail */
     /* Both pulled ALL of chunk0 into staging, but the tail FIFO write stayed
      * busy: the chunk is staged, not consumed. A pull-consumed model would have
      * retired and overwritten it with chunk1 here — it stays resident, unfed. */
     CHECK(p->broker.wave.pulled[0]==480&&p->broker.wave.pulled[1]==480);
     CHECK(p->broker.wave.confirmed[0]<480&&p->broker.wave.confirmed[1]<480);
     CHECK(p->broker.wave.chunk.first_frame==0);          /* chunk0 still resident */
     CHECK(p->broker.wave.compiler.drained==0);           /* nothing fed */
     CHECK(p->broker.wave.compiler.produced==480&&p->broker.wave.compiler.pending==480);
     CHECK(!elden_broker_cancel(&p->broker,owner,epoch,1));
     CHECK(!elden_broker_step(&p->broker));
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsCancelled);
     CHECK(!elden_shared_budget_active(&p->broker.budget));
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    /* ---- one motor confirms the chunk while the sibling stays FIFO-busy ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     p->feed_busy[1]=1;                                  /* only motor1 tail goes full */
     CHECK(!step_until(p,m0_chunk_full,200000));          /* motor0 drains chunk0 */
     /* Motor0 wrote every chunk0 frame to its FIFO; motor1's tail stayed staged-
      * busy. A chunk feeds only when BOTH confirm every frame — so even motor0's
      * fully-confirmed share is not counted drained yet. */
     CHECK(p->broker.wave.confirmed[0]==480);
     CHECK(p->broker.wave.pulled[1]==480&&p->broker.wave.confirmed[1]<480);
     CHECK(p->broker.wave.compiler.drained==0);           /* chunk0 not fed */
     /* Stop mid-window: cancelled settle; confirmed+staged frames drop, no false
      * completion. */
     CHECK(!elden_broker_stop(&p->broker));
     CHECK(p->broker.rtp.state==EldenRtpCancelled&&p->broker.current_kind==0);
     CHECK(!elden_shared_budget_active(&p->broker.budget));free(p);}
    /* ---- a FIFO-busy retry writes the staged tail once, never duplicated ---- */
    {FeedPeer *p=feed_setup(fw,n,&prof,1,0);open(p,&owner,&epoch);
     CHECK(!elden_broker_submit_recipe(&p->broker,owner,epoch,1,7,0,32767));
     p->feed_busy[0]=1;CHECK(!step_until(p,m0_staged,200000));   /* motor0 tail staged+busy */
     CHECK(p->broker.wave.pulled[0]==480&&p->broker.wave.confirmed[0]<480);
     CHECK(p->broker.wave.compiler.drained==0);           /* staged tail not fed */
     p->feed_busy[0]=0;                                   /* FIFO drains -> retry */
     CHECK(!run(p,1440));
     int8_t exp[2][1440];expected(p,&prof,exp);
     /* The held staged bytes were written on retry — not re-pulled or dropped:
      * each FIFO holds the compiled stream exactly once, drained==1440. */
     for(unsigned m=0;m<2;m++){CHECK(p->hardware.motor[m].rtp_count==1440);
         CHECK(!memcmp(p->hardware.motor[m].rtp,exp[m],1440));}
     CHECK(p->broker.wave.compiler.drained==1440&&p->broker.wave.compiler.pending==0);
     finish_rtp(p);CHECK(!elden_broker_step(&p->broker));
     CHECK(!elden_broker_result(&p->broker,owner,epoch,1,&result)&&result.state==EldenHapticsComplete);
     CHECK(!elden_broker_close(&p->broker,owner,epoch)&&!elden_broker_stop(&p->broker));free(p);}
    printf("PASS wave-feed recipe->chunk->RTP: %u checks; one session, bounded chunks, ordered no-dup feed, refusals/cancel/stop; synthetic measured peers\n",checks);return 0;
}
