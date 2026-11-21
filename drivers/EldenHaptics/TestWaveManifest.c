/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host validation for a generated EldenWaveManifest: sorted/unique/nonzero
 * untagged ids, deduplicated carriers, morph pairing and automation contract,
 * then drains every source recipe through WaveCompiler and WaveFeed with a
 * synthetic measured profile. The profile is host-only: no hardware claim. */
#include <stdio.h>
#include <string.h>
#include "WaveFeed.h"
#include "AwRtp.h"
#include "EldenWaveManifest.h"
#ifndef EXPECTED_SOURCES
#define EXPECTED_SOURCES 0u
#endif
#ifndef EXPECTED_CARRIERS
#define EXPECTED_CARRIERS 0u
#endif
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;printf("FAIL %u %s\n",checks,#x);}}while(0)
static const EldenHapticsGainPoint lutP[]={{0,0},{32767,32767}};
static const EldenHapticsCalibration calib={lutP,2,32767,24000,480,1ull<<50};
static EldenWaveProfile profile(void){
    EldenWaveProfile p={0};p.motor[0]=p.motor[1]=&calib;p.gain[0]=p.gain[1]=64;
    p.max_window_squares[0]=p.max_window_squares[1]=1ull<<50;
    p.max_window_on_frames[0]=p.max_window_on_frames[1]=480;
    p.max_dual_on_frames=480;p.window_frames=480;
    p.measured_min_payload_bps=64000;p.revision=1;p.enabled=1;return p;
}
static int drain_compiler(const EldenWaveRecipe *r){
    static EldenWaveCell hist[480];EldenWaveCompiler c={0};
    uint64_t gen;EldenWaveProfile p=profile();
    if(elden_wave_compiler_begin(&c,r,&p,1,hist,480,&gen))return -1;
    EldenWaveChunk ch;uint32_t pos=0;
    for(unsigned g=0;g<4096;g++){
        int done=elden_wave_compiler_next(&c,gen,pos,&ch);if(done<0)return -2;
        pos+=ch.frames;
        if(elden_wave_compiler_fed(&c,gen,ch.frames))return -3;
        if(done)return c.drained==r->total_frames&&c.state==EldenWaveComplete?0:-4;
    }
    return -5;
}
static int drain_feed(const EldenWaveRecipe *r){
    static EldenWaveCell hist[480];EldenWaveFeed f={0};
    uint64_t gen;EldenWaveProfile p=profile();
    if(elden_wave_feed_begin(&f,r,&p,1,hist,480,&gen))return -1;
    int8_t buf[64];uint32_t pos[2]={0,0};
    for(unsigned g=0;g<200000&&f.compiler.drained<r->total_frames&&!f.error;g++){
        for(unsigned m=0;m<2;m++){
            int n=elden_wave_feed_pull(&f,m,pos[m],buf,64);
            if(n>0){pos[m]+=n;if(elden_wave_feed_consumed(&f,m,pos[m]-n,(size_t)n))return -2;}
            else if(n!=EldenAwBusy)return -3;
        }
    }
    return f.compiler.drained==r->total_frames&&!f.error?0:-4;
}
int main(void){
    CHECK(elden_wave_manifest_count==EXPECTED_SOURCES);
    CHECK(elden_wave_manifest_count>0);
    uint32_t prior=0;
    const EldenWaveCarrier *uniq[64];unsigned nu=0,auto_seen=0,morph_seen=0;
    for(unsigned i=0;i<elden_wave_manifest_count;i++){
        const EldenWaveSource *s=&elden_wave_manifest[i];
        CHECK(s->id&&!(s->id&EldenWaveRecipeTag));
        CHECK(i==0||s->id>prior);prior=s->id;
        const EldenWaveRecipe *r=s->recipe;CHECK(r&&r->events&&r->event_count&&r->total_frames);
        for(int which=0;which<2;which++){
            const EldenWaveAutomationList *l=which?r->sharpness:r->intensity;
            if(!l)continue;auto_seen++;CHECK(l->items&&l->count);
            uint32_t pf=0;
            for(uint32_t k=0;k<l->count;k++){
                const EldenWaveAutomation *it=&l->items[k];
                CHECK(it->frame>=pf&&(it->kind==EldenWaveStep||it->kind==EldenWaveCurve));pf=it->frame;
                if(it->kind==EldenWaveStep){CHECK(it->value_q15<=32767);continue;}
                CHECK(it->points&&it->count);
                uint32_t pp=0;
                for(uint32_t j=0;j<it->count;j++){CHECK((j==0||it->points[j].frame>pp)&&it->points[j].value_q15<=32767);pp=it->points[j].frame;}
            }
        }
        uint32_t total=0;
        for(uint32_t k=0;k<r->event_count;k++){
            const EldenWaveEvent *e=&r->events[k];
            CHECK(e->frames&&e->start_frame+e->frames<=r->total_frames);
            for(const EldenWaveCarrier *t=e->carrier;;t=e->morph){
                CHECK(t&&t->samples&&t->count&&t->count<=24000);
                if(t->loop_frames)CHECK(t->loop_frames+t->tail_frames==t->count);
                if(t->tail_frames)CHECK(t->loop_frames&&e->frames>=t->tail_frames);
                for(uint32_t j=0;j<t->count;j++)CHECK(t->samples[j]!=-32768);
                unsigned u=0;while(u<nu&&uniq[u]!=t)u++;if(u==nu&&nu<64)uniq[nu++]=t;
                if(t==e->morph||!e->morph)break;
            }
            if(e->morph){morph_seen++;CHECK(e->morph->count==e->carrier->count&&
                e->morph->loop_frames==e->carrier->loop_frames&&e->morph->tail_frames==e->carrier->tail_frames);}
            total=e->start_frame+e->frames;
        }
        CHECK(total==r->total_frames);
        int rc=drain_compiler(r);CHECK(!rc);if(rc)printf("  compiler rc=%d source=%u\n",rc,i);
        rc=drain_feed(r);CHECK(!rc);if(rc)printf("  feed rc=%d source=%u\n",rc,i);
    }
    CHECK(nu==EXPECTED_CARRIERS);
    CHECK(auto_seen>=2);          /* fixture AHAP supplies intensity+sharpness */
    CHECK(morph_seen>0);          /* fixture AHAP continuous uses a morph pair */
    printf("TestWaveManifest %s %u checks\n",failed?"FAIL":"PASS",checks);
    return failed?1:0;
}
