/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host test for the source-side continuous waveform/RTP compiler. Carriers
 * and the calibrated linear LUT make every emitted drive value deterministic:
 * signed16000->int8 62, signed8000->31 through the qualified conversion path.
 * Every refusal class from the task is exercised; nothing claims hardware. */
#include "WaveCompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t carHi[480],carMorph[480],carBig[480],carNeg[480],carTail[480];
static EldenWaveCarrier cHi={carHi,480,480,0};
static EldenWaveCarrier cMo={carMorph,480,480,0};
static EldenWaveCarrier cBg={carBig,480,0,0};
static EldenWaveCarrier cTl={carTail,480,400,80};           /* loop400+tail80 */
static const EldenHapticsGainPoint lutP[]={{0,0},{32767,32767}};
static const EldenHapticsCalibration calib={lutP,2,32767,24000,480,1ull<<60};
static const EldenHapticsCalibration calibBadRate={lutP,2,32767,12000,480,1ull<<60};
/* Malformed measured LUTs: each isolates one admission violation. */
static const EldenHapticsGainPoint lutEndLast[]={{0,0},{20000,20000}};                 /* last mag!=32767 */
static const EldenHapticsGainPoint lutEndFirst[]={{100,0},{32767,32767}};              /* first mag!=0 */
static const EldenHapticsGainPoint lutDecDrv[]={{0,0},{16384,20000},{32767,10000}};    /* decreasing drive */
static const EldenHapticsGainPoint lutNonMono[]={{0,0},{20000,10000},{15000,20000},{32767,32767}};/* nonmono mag */
static const EldenHapticsCalibration calEnd={lutEndLast,2,32767,24000,480,1ull<<60};
static const EldenHapticsCalibration calEnd0={lutEndFirst,2,32767,24000,480,1ull<<60};
static const EldenHapticsCalibration calDec={lutDecDrv,3,32767,24000,480,1ull<<60};
static const EldenHapticsCalibration calMono={lutNonMono,4,32767,24000,480,1ull<<60};
static EldenWaveCell hist[480];
static EldenWaveProfile profile(void){
    EldenWaveProfile p;memset(&p,0,sizeof(p));
    p.motor[0]=&calib;p.motor[1]=&calib;p.gain[0]=p.gain[1]=1;
    p.max_window_squares[0]=p.max_window_squares[1]=~0ull;
    p.max_window_on_frames[0]=p.max_window_on_frames[1]=480;
    p.max_dual_on_frames=480;p.window_frames=480;
    p.measured_min_payload_bps=60000;p.revision=1;p.enabled=1;
    return p;
}
static EldenWaveRecipe recipe2(EldenWaveEvent *ev){
    EldenWaveRecipe r;memset(&r,0,sizeof(r));r.events=ev;r.event_count=2;r.total_frames=1440;return r;
}
int main(void){
    for(int i=0;i<480;i++){carHi[i]=16000;carMorph[i]=8000;carBig[i]=24000;carNeg[i]=-16000;carTail[i]=i<400?16000:8000;}
    EldenWaveCompiler s;EldenWaveChunk ck;uint64_t g,g2;
    EldenWaveProfile prof=profile();
    /* ---- valid calibrated dual-motor conversion across three chunks ---- */
    EldenWaveEvent ev[2]={
        {&cHi,&cMo,0,960,32767,0},          /* sharp0 ->16000 ->62 */
        {&cHi,&cMo,960,480,32767,32767},    /* sharp1 morph ->8000 ->31 */
    };
    EldenWaveRecipe rc=recipe2(ev);
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(g==1&&s.state==EldenWaveReady&&s.enabled);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0);
    CHECK(ck.generation==g&&ck.first_frame==0&&ck.frames==480&&ck.timestamp_us==0&&ck.mask==3);
    for(int i=0;i<480;i++){CHECK(ck.pcm[0][i]==62);CHECK(ck.pcm[1][i]==62);}
    CHECK(s.pending==480&&s.produced==480);
    CHECK(elden_wave_compiler_fed(&s,g,480)==0&&s.pending==0&&s.drained==480);
    CHECK(elden_wave_compiler_next(&s,g,480,&ck)==0&&ck.first_frame==480&&ck.timestamp_us==20000);
    CHECK(elden_wave_compiler_fed(&s,g,480)==0);
    CHECK(elden_wave_compiler_next(&s,g,960,&ck)==1&&ck.first_frame==960&&ck.timestamp_us==40000&&ck.frames==480);
    for(int i=0;i<480;i++)CHECK(ck.pcm[0][i]==31);
    CHECK(s.state==EldenWaveComplete);
    CHECK(elden_wave_compiler_fed(&s,g,480)==0&&s.drained==1440&&s.pending==0);
    CHECK(elden_wave_compiler_next(&s,g,1440,&ck)==-116);
    /* ---- malformed recipe refuses at arm ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[1]={{&cHi,NULL,0,0,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[2]={{&cHi,NULL,480,480,32767,0},{&cHi,NULL,0,480,32767,0}};EldenWaveRecipe r={b,2,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* unordered */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[1]={{&cHi,NULL,480,600,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* beyond total */
    memset(&s,0,sizeof(s));
    {EldenWaveCarrier bad={carHi,0,0,0};EldenWaveEvent b[1]={{&bad,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* empty carrier */
    memset(&s,0,sizeof(s));
    {EldenWaveCarrier bad={carMorph,240,240,0};EldenWaveEvent b[1]={{&cHi,&bad,0,480,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* morph mismatch */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[1]={{&cHi,NULL,0,480,40000,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* intensity>q15 */
    memset(&s,0,sizeof(s));
    {int16_t *neg=malloc(480*2);memcpy(neg,carHi,960);neg[10]=-32768;
     EldenWaveCarrier bad={neg,480,480,0};EldenWaveEvent b[1]={{&bad,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);free(neg);} /* -32768 sample */
    memset(&s,0,sizeof(s));
    {EldenWaveControlPoint pt[2]={{480,100},{120,200}};EldenWaveAutomation it={0,EldenWaveCurve,0,pt,2};
     EldenWaveAutomationList al={&it,1};EldenWaveEvent b[1]={{&cHi,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,&al,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* unordered curve */
    memset(&s,0,sizeof(s));
    {EldenWaveControlPoint pt[2]={{120,100},{120,200}};EldenWaveAutomation it={0,EldenWaveCurve,0,pt,2};
     EldenWaveAutomationList al={&it,1};EldenWaveEvent b[1]={{&cHi,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,&al,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* collapsed equal point frames */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[1]={{&cHi,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,0};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* zero duration */
    /* ---- tail underflow: an event shorter than its explicit tail refuses ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent b[1]={{&cTl,NULL,0,40,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* 40<tail80 */
    memset(&s,0,sizeof(s));
    {EldenWaveCarrier bad={carTail,480,0,80};EldenWaveEvent b[1]={{&bad,NULL,0,480,32767,0}};EldenWaveRecipe r={b,1,NULL,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==-22);}       /* tail without loop */
    /* ---- tail selection: a looped carrier plays its tail in the last frames ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent a[1]={{&cTl,NULL,0,480,32767,0}};EldenWaveRecipe r={a,1,NULL,NULL,480};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==1);
     for(int i=0;i<400;i++)CHECK(ck.pcm[0][i]==62);
     for(int i=400;i<480;i++)CHECK(ck.pcm[0][i]==31);}
    /* ---- malformed measured LUTs refuse at admission, on either motor ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[0]=&calEnd;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* last-mag endpoint */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[0]=&calEnd0;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* first-mag endpoint */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[0]=&calDec;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* decreasing drive */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[0]=&calMono;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* nonmonotonic mag */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[1]=&calDec;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* bad LUT on second motor */
    /* ---- missing / disabled / asymmetric / stale calibration ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,NULL,1,hist,480,&g)==-22);          /* absent */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile off=profile();off.enabled=0;
     CHECK(elden_wave_compiler_begin(&s,&rc,&off,1,hist,480,&g)==-125);}      /* conservative disabled */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[1]=NULL;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* single-motor coverage */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.gain[1]=0;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* asymmetric gain */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.max_window_on_frames[1]=0;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* asymmetric limit */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.motor[1]=&calibBadRate;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* out-of-range LUT */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();a.gain[0]=200;
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,1,hist,480,&g)==-22);}         /* gain out of range */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile a=profile();
     CHECK(elden_wave_compiler_begin(&s,&rc,&a,2,hist,480,&g)==-116);}        /* stale revision */
    /* ---- stale generation ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_next(&s,g+9,0,&ck)==-116);
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g2)==0&&g2==2);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-116);
    CHECK(elden_wave_compiler_stop(&s,g2)==0);
    /* ---- timing gap / order ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_next(&s,g,480,&ck)==-116);                     /* skipped first chunk */
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    /* ---- underrun: re-request an already emitted position ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-61);
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    /* ---- dual-load budget: simultaneous both-motor on over the window ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile b=profile();b.max_dual_on_frames=300;
     CHECK(elden_wave_compiler_begin(&s,&rc,&b,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-13&&s.state==EldenWaveFailed);}
    /* ---- per-motor on budget ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile b=profile();b.max_window_on_frames[0]=300;b.max_window_on_frames[1]=300;
     CHECK(elden_wave_compiler_begin(&s,&rc,&b,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-13&&s.state==EldenWaveFailed);}
    /* ---- rolling square budget ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveProfile b=profile();b.max_window_squares[0]=384400;b.max_window_squares[1]=384400;
     CHECK(elden_wave_compiler_begin(&s,&rc,&b,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-13);}
    /* ---- stop during pending feed: partial abort, never Complete ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0&&s.pending==480);
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    CHECK(s.state==EldenWaveStopped&&s.dropped==480&&s.pending==0&&s.state!=EldenWaveComplete);
    CHECK(elden_wave_compiler_next(&s,g,480,&ck)==-125);
    /* ---- mixture clip refuses, never a limiter/amplifier ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveEvent ov[2]={{&cBg,NULL,0,480,32767,0},{&cBg,NULL,0,480,32767,0}};
     EldenWaveRecipe r={ov,2,NULL,NULL,480};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==-13);}
    /* ---- conservative disabled default: never armed produces nothing ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_next(&s,1,0,&ck)==-125);
    CHECK(elden_wave_compiler_fed(&s,1,1)==-125);
    CHECK(elden_wave_compiler_stop(&s,1)==-125);
    /* ---- live re-arm refuses ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g2)==-16);
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    /* ---- fed accounting ---- */
    memset(&s,0,sizeof(s));
    CHECK(elden_wave_compiler_begin(&s,&rc,&prof,1,hist,480,&g)==0);
    CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0);
    CHECK(elden_wave_compiler_fed(&s,g+1,480)==-116);                        /* wrong gen */
    CHECK(elden_wave_compiler_fed(&s,g,481)==-22);                           /* more than pending */
    CHECK(elden_wave_compiler_fed(&s,g,0)==-22);                             /* zero */
    CHECK(elden_wave_compiler_stop(&s,g)==0);
    /* ---- signed carrier: negative samples produce signed drive ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveCarrier cN={carNeg,480,0,0};EldenWaveEvent a[1]={{&cN,NULL,0,480,32767,0}};
     EldenWaveRecipe r={a,1,NULL,NULL,480};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==1);
     for(int i=0;i<480;i++){CHECK(ck.pcm[0][i]==-62);CHECK(ck.pcm[1][i]==-62);}}
    /* ---- intensity automation: step to 0 at frame480 ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveAutomation st={480,EldenWaveStep,0,NULL,0};EldenWaveAutomationList al={&st,1};
     EldenWaveEvent a[1]={{&cHi,NULL,0,960,32767,0}};EldenWaveRecipe r={a,1,&al,NULL,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0);
     for(int i=0;i<480;i++)CHECK(ck.pcm[0][i]==62);
     CHECK(elden_wave_compiler_next(&s,g,480,&ck)==1);
     for(int i=0;i<480;i++)CHECK(ck.pcm[0][i]==0);}
    /* ---- sharpness automation morph: step to 32767 at frame480 ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveAutomation st={480,EldenWaveStep,32767,NULL,0};EldenWaveAutomationList al={&st,1};
     EldenWaveEvent a[1]={{&cHi,&cMo,0,960,32767,0}};EldenWaveRecipe r={a,1,NULL,&al,960};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==0);
     for(int i=0;i<480;i++)CHECK(ck.pcm[0][i]==62);
     CHECK(elden_wave_compiler_next(&s,g,480,&ck)==1);
     for(int i=0;i<480;i++)CHECK(ck.pcm[0][i]==31);}
    /* ---- intensity curve: linear 32767->0 over first 480 ---- */
    memset(&s,0,sizeof(s));
    {EldenWaveControlPoint pt[2]={{0,32767},{480,0}};EldenWaveAutomation it={0,EldenWaveCurve,0,pt,2};
     EldenWaveAutomationList al={&it,1};EldenWaveEvent a[1]={{&cHi,NULL,0,480,32767,0}};EldenWaveRecipe r={a,1,&al,NULL,480};
     CHECK(elden_wave_compiler_begin(&s,&r,&prof,1,hist,480,&g)==0);
     CHECK(elden_wave_compiler_next(&s,g,0,&ck)==1);
     CHECK(ck.pcm[0][0]==62&&ck.pcm[0][240]==31);}
    printf("PASS wave-compiler: %u checks\n",checks);
    return 0;
}
