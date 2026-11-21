/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Source-side continuous waveform/RTP compiler: carries a parsed Apple
 * AHAP/recipe to the two AW86937 channels as bounded timestamped chunks.
 * Output requires an explicit measured dual-motor calibration profile; the
 * disabled default produces nothing. No voltage/boost/resonance/duty/thermal
 * limits are invented; intensity/sharpness stay perceptual conversion inputs.
 * The original worker/FIFO owner still feeds chips — this only compiles. */
#include "WaveCompiler.h"
static uint32_t multiply(uint32_t a,uint32_t b){return (a*b+16383u)/32767u;}
static int fail(EldenWaveCompiler *s,int error){s->state=EldenWaveFailed;s->error=error;return error;}
/* Qualified carrier select: explicit loop/tail layout, one-shot otherwise.
 * Samples are signed; an out-of-contract layout sets *ok=0 instead of
 * colliding with a legitimate negative sample value. */
static int32_t carrier_at(const EldenWaveCarrier *t,uint32_t index,uint32_t total,int *ok){
    *ok=0;
    if(!t||!t->samples||!t->count||t->count>ELDEN_WAVE_RATE_HZ)return 0;
    if(t->loop_frames){
        if(t->loop_frames+t->tail_frames!=t->count)return 0;
        if(t->tail_frames&&total>=t->tail_frames&&index>=total-t->tail_frames){
            uint32_t i=t->loop_frames+index-(total-t->tail_frames);
            if(i>=t->count)return 0;
            *ok=1;return t->samples[i];
        }
        *ok=1;return t->samples[index%t->loop_frames];
    }
    if(total!=t->count||index>=t->count)return 0;
    *ok=1;return t->samples[index];
}
/* Sharpness morph between the qualified sharpness0 and sharpness1 carriers.
 * A single bound carrier (morph NULL) serves every qualified sharpness. */
static int32_t sharp_value(const EldenWaveEvent *e,uint32_t sharp,uint32_t index,uint32_t count,int *ok){
    int32_t a=carrier_at(e->carrier,index,count,ok);if(!*ok)return 0;
    if(!e->morph){*ok=1;return a;}
    int ob;int32_t b=carrier_at(e->morph,index,count,&ob);if(!ob){*ok=0;return 0;}
    *ok=1;return a+(int32_t)(((int64_t)(b-a)*(int64_t)sharp)/32767);
}
/* One curve item's interpolated value at a relative frame. Ordered points with
 * linear interpolation; the last point's level holds past its frame. */
static uint32_t curve_at(const EldenWaveAutomation *it,uint32_t at){
    const EldenWaveControlPoint *p=it->points;
    if(at<=p[0].frame)return p[0].value_q15;
    for(uint32_t i=1;i<it->count;i++){
        if(at<=p[i].frame){
            int32_t delta=(int32_t)p[i].value_q15-(int32_t)p[i-1].value_q15;
            int32_t v=(int32_t)p[i-1].value_q15+(int32_t)(((int64_t)delta*(at-p[i-1].frame))/(p[i].frame-p[i-1].frame));
            return v<0?0:v>32767?32767:(uint32_t)v;
        }
    }
    return p[it->count-1].value_q15;
}
/* Latest applicable automation at an absolute source frame, over ordered
 * items; steps overwrite, curves interpolate. fallback is the qualified
 * default (intensity full, sharpness 0). */
static uint32_t automate(const EldenWaveAutomationList *list,uint32_t frame,uint32_t fallback){
    if(!list||!list->count||!list->items)return fallback;
    uint32_t value=fallback;
    for(uint32_t i=0;i<list->count;i++){
        const EldenWaveAutomation *it=&list->items[i];
        if(frame<it->frame)continue;
        value=it->kind==EldenWaveStep?it->value_q15:curve_at(it,frame-it->frame);
    }
    return value;
}
/* Render the signed source mixture for [first,first+count). Every event's
 * sharpness/intensity is applied through the calibrated carrier/LUT path;
 * a mixture that clips is a refusal, never an automatic limiter/amplifier. */
static int render_source(const EldenWaveCompiler *s,int16_t *dst,uint32_t first,uint32_t count){
    int32_t mix[ELDEN_WAVE_CHUNK];for(uint32_t i=0;i<count;i++)mix[i]=0;
    for(uint32_t i=0;i<s->recipe.event_count;i++){
        const EldenWaveEvent *e=&s->recipe.events[i];
        if(e->start_frame>=first+count||e->start_frame+e->frames<=first)continue;
        for(uint32_t f=first;f<first+count;f++){
            if(f<e->start_frame||f>=e->start_frame+e->frames)continue;
            uint32_t sharp=(uint32_t)e->sharpness_q15+automate(s->recipe.sharpness,f,0);if(sharp>32767)sharp=32767;
            int ok;int32_t v=sharp_value(e,sharp,f-e->start_frame,e->frames,&ok);if(!ok)return -13;
            uint32_t level=multiply(e->intensity_q15,automate(s->recipe.intensity,f,32767));
            int32_t scaled=(int32_t)(((int64_t)(v<0?-v:v)*level+16383)/32767);
            mix[f-first]+=v<0?-scaled:scaled;
        }
    }
    for(uint32_t i=0;i<count;i++){
        if(mix[i]>32767||mix[i]<-32768)return -13;
        dst[i]=(int16_t)mix[i];
    }
    return 0;
}
/* Moving-window dual-motor accounting over emitted frames. Both masked motors
 * stream (zero PCM is not motor-off). Preflight every new frame's squares,
 * motor-on and simultaneous dual-on before committing, mirroring the existing
 * live/broker rolling-window contract; no coil/thermal model is implied. */
static int record(EldenWaveCompiler *s,const int8_t (*pcm)[ELDEN_WAVE_CHUNK],uint32_t count,uint8_t on_mask){
    const EldenWaveProfile *c=&s->calibration;
    uint64_t sq[2]={s->rolling_squares[0],s->rolling_squares[1]};
    uint32_t on[2]={s->rolling_on[0],s->rolling_on[1]},dual=s->rolling_dual;
    uint32_t size=c->window_frames,at=s->history_at,used=s->history_used;
    for(uint32_t i=0;i<count;i++){
        const EldenWaveCell *old=&s->history[at];
        for(unsigned m=0;m<2;m++){
            if(used==size){sq[m]-=old->squares[m];if(old->on_mask&(1u<<m))on[m]--;}
            if(on_mask&(1u<<m)){int32_t v=pcm[m][i];sq[m]+=(uint64_t)v*v*c->gain[m]*c->gain[m];on[m]++;}
            if(sq[m]>c->max_window_squares[m]||on[m]>c->max_window_on_frames[m])return -13;
        }
        if(used==size&&old->on_mask==3)dual--;
        if(on_mask==3)dual++;
        if(dual>c->max_dual_on_frames)return -13;
        if(used<size)used++;
        at=(at+1)%size;
    }
    for(uint32_t i=0;i<count;i++){
        EldenWaveCell *h=&s->history[s->history_at];h->on_mask=on_mask;
        for(unsigned m=0;m<2;m++){int32_t v=pcm[m][i];h->squares[m]=(on_mask&(1u<<m))?(uint64_t)v*v*c->gain[m]*c->gain[m]:0;}
        s->history_at=(s->history_at+1)%size;
    }
    for(unsigned m=0;m<2;m++){s->rolling_squares[m]=sq[m];s->rolling_on[m]=on[m];}
    s->rolling_dual=dual;s->history_used=used;return 0;
}
/* A measured dual-motor profile is mandatory: enabled, in range, covers BOTH
 * motors and is not stale relative to the caller's accepted revision. */
static int valid_profile(const EldenWaveProfile *c,uint32_t accepted_revision){
    if(!c)return -22;
    if(!c->enabled)return -125;
    if(c->revision<accepted_revision)return -116;
    if(!c->window_frames||c->window_frames>240000||!c->max_dual_on_frames||
       c->max_dual_on_frames>c->window_frames||!c->measured_min_payload_bps)return -22;
    unsigned motors=0;
    for(unsigned m=0;m<2;m++){
        const EldenHapticsCalibration *sm=c->motor[m];
        if(!sm||!c->gain[m]||c->gain[m]>128||!c->max_window_squares[m]||
           !c->max_window_on_frames[m]||c->max_window_on_frames[m]>c->window_frames)return -22;
        /* Both motor LUTs are measured-qualified at admission — endpoints,
         * monotonicity, peak, rate and budget — not deferred to first produce. */
        if(elden_haptics_calibration_check(sm))return -22;
        if(sm->rate_hz!=ELDEN_WAVE_RATE_HZ||sm->max_frames<ELDEN_WAVE_CHUNK)return -22;
        motors++;
    }
    if(motors!=2)return -22;
    if(c->measured_min_payload_bps<motors*24000u*5u/4u)return -13;
    return 0;
}
static int valid_curve(const EldenWaveAutomationList *l){
    if(!l)return 0;
    if(!l->items||!l->count||l->count>1024)return -22;
    uint32_t prior=0;
    for(uint32_t i=0;i<l->count;i++){
        const EldenWaveAutomation *it=&l->items[i];
        if(it->frame<prior||(it->kind!=EldenWaveStep&&it->kind!=EldenWaveCurve))return -22;
        prior=it->frame;
        if(it->kind==EldenWaveCurve){
            if(!it->points||!it->count||it->count>1024)return -22;
            uint32_t pt=0;
            for(uint32_t j=0;j<it->count;j++){
                /* Control-point frames must strictly increase: equal frames
                 * are a collapsed pair that would divide by zero in curve_at. */
                if((j&&it->points[j].frame<=pt)||it->points[j].value_q15>32767)return -22;pt=it->points[j].frame;
            }
        }
    }
    return 0;
}
static int valid_recipe(const EldenWaveRecipe *r){
    if(!r||!r->total_frames||r->total_frames>ELDEN_WAVE_MAX_FRAMES)return -22;
    if(!r->events||!r->event_count||r->event_count>1024)return -22;
    uint32_t prior=0;
    for(uint32_t i=0;i<r->event_count;i++){
        const EldenWaveEvent *e=&r->events[i];
        if(!e->frames||e->start_frame<prior||(uint64_t)e->start_frame+e->frames>r->total_frames||
           e->intensity_q15>32767||e->sharpness_q15>32767)return -22;
        prior=e->start_frame;
        const EldenWaveCarrier *t=e->carrier;
        if(!t||!t->samples||!t->count||t->count>ELDEN_WAVE_RATE_HZ)return -22;
        if(t->loop_frames&&t->loop_frames+t->tail_frames!=t->count)return -22;
        /* An explicit tail belongs to a loop and must fit inside the event:
         * a tail with no loop, or an event shorter than the tail, refuses so the
         * tail region can never underflow into silent loop playback. */
        if(t->tail_frames&&(!t->loop_frames||e->frames<t->tail_frames))return -22;
        if(e->morph&&(!e->morph->samples||e->morph->count!=t->count||
           e->morph->loop_frames!=t->loop_frames||e->morph->tail_frames!=t->tail_frames))return -22;
        for(uint32_t k=0;k<t->count;k++)if(t->samples[k]==-32768)return -22;
        if(e->morph)for(uint32_t k=0;k<e->morph->count;k++)if(e->morph->samples[k]==-32768)return -22;
    }
    if(valid_curve(r->intensity)||valid_curve(r->sharpness))return -22;
    return 0;
}
int elden_wave_compiler_begin(EldenWaveCompiler *s,const EldenWaveRecipe *r,
    const EldenWaveProfile *c,uint32_t accepted_revision,EldenWaveCell *history,uint32_t history_cells,uint64_t *generation){
    if(!s||!r||!generation)return -22;
    if(s->state==EldenWaveReady||s->state==EldenWaveRunning)return -16;
    int rc=valid_profile(c,accepted_revision);if(rc)return rc;
    rc=valid_recipe(r);if(rc)return rc;
    if(!history||history_cells<c->window_frames)return -22;
    uint64_t next=s->generation+1;                       /* run stamps stay monotonic */
    EldenWaveCompiler zero={0};*s=zero;
    s->recipe=*r;s->calibration=*c;s->history=history;s->history_count=c->window_frames;
    for(uint32_t i=0;i<c->window_frames;i++){history[i].squares[0]=history[i].squares[1]=0;history[i].on_mask=0;}
    s->generation=next;s->state=EldenWaveReady;s->enabled=1;*generation=next;
    return 0;
}
int elden_wave_compiler_next(EldenWaveCompiler *s,uint64_t generation,
    uint32_t expected_first_frame,EldenWaveChunk *out){
    if(!s||!out)return -22;
    if(!s->enabled||s->state==EldenWaveDisabled)return -125;
    if(generation!=s->generation)return -116;
    if(s->state==EldenWaveComplete)return -116;
    if(s->state!=EldenWaveReady&&s->state!=EldenWaveRunning)return s->error?s->error:-125;
    if(expected_first_frame!=s->produced)return expected_first_frame<s->produced?-61:-116;
    uint32_t frames=s->recipe.total_frames-s->produced;if(frames>ELDEN_WAVE_CHUNK)frames=ELDEN_WAVE_CHUNK;
    int16_t src[ELDEN_WAVE_CHUNK];
    if(render_source(s,src,s->produced,frames))return fail(s,-13);
    EldenWaveChunk value={0};
    value.generation=s->generation;value.first_frame=s->produced;value.frames=frames;
    value.timestamp_us=((uint64_t)s->produced*1000000)/ELDEN_WAVE_RATE_HZ;value.mask=3;
    for(unsigned m=0;m<2;m++){
        uint64_t cost;
        int rc=elden_haptics_samples(src,frames,32767,s->calibration.motor[m],value.pcm[m],&cost);
        if(rc)return fail(s,rc);
    }
    if(record(s,value.pcm,frames,3))return fail(s,-13);
    *out=value;s->produced+=frames;s->pending+=frames;s->state=EldenWaveRunning;
    if(s->produced==s->recipe.total_frames){s->state=EldenWaveComplete;return 1;}
    return 0;
}
int elden_wave_compiler_fed(EldenWaveCompiler *s,uint64_t generation,uint32_t frames){
    if(!s)return -22;
    if(!s->enabled||s->state==EldenWaveDisabled)return -125;
    if(generation!=s->generation)return -116;
    if(s->state!=EldenWaveRunning&&s->state!=EldenWaveComplete)return s->error?s->error:-125;
    if(!frames||frames>s->pending)return -22;
    s->pending-=frames;s->drained+=frames;return 0;
}
int elden_wave_compiler_stop(EldenWaveCompiler *s,uint64_t generation){
    if(!s)return -22;
    if(!s->enabled||s->state==EldenWaveDisabled)return -125;
    if(generation!=s->generation)return -116;
    if(s->state==EldenWaveStopped)return -125;
    if(s->state==EldenWaveFailed)return s->error?s->error:-125;
    s->dropped=s->pending;s->pending=0;s->state=EldenWaveStopped;return 0;
}
