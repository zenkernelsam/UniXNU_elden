/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Connects the source-side WaveCompiler's bounded chunks to the existing
 * serialized native RTP worker through a pull-source clip. The worker keeps ONE
 * live prepare+GO session across the whole recipe while payload residency stays
 * bounded to a single compiled chunk plus one staged burst — no unbounded
 * kernel allocation, no per-chunk STOP/GO restart, one up-front reservation.
 * This only feeds compiled chunks; it invents no carrier, gain, calibration or
 * voltage and binds nothing by itself. */
#include "WaveFeed.h"
const EldenWaveSource *elden_wave_source_find(const EldenWaveSource *e,size_t count,uint32_t id){
    if(!e||count>512||!id)return 0;
    size_t lo=0,hi=count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(e[mid].id<id)lo=mid+1;else hi=mid;}
    return lo<count&&e[lo].id==id?e+lo:0;
}
int elden_wave_feed_begin(EldenWaveFeed *f,const EldenWaveRecipe *r,const EldenWaveProfile *p,
    uint32_t accepted_revision,EldenWaveCell *history,uint32_t history_cells,uint64_t *generation){
    if(!f||!r||!generation||f->enabled)return -22;
    int rc=elden_wave_compiler_begin(&f->compiler,r,p,accepted_revision,history,history_cells,&f->generation);
    if(rc)return rc;                       /* absent/disabled/stale profile refuses */
    f->history=history;f->history_cells=history_cells;
    /* Prime the first chunk before any GO: a recipe that cannot compile its
     * opening chunk never reaches the FIFO owner. */
    rc=elden_wave_compiler_next(&f->compiler,f->generation,0,&f->chunk);
    if(rc<0){f->error=rc;return rc;}
    f->have_chunk=1;f->pulled[0]=f->pulled[1]=0;f->confirmed[0]=f->confirmed[1]=0;if(rc==1)f->exhausted=1;
    f->enabled=1;*generation=f->generation;return 0;
}
void elden_wave_feed_clip(EldenWaveFeed *f,EldenRtpClip *clip,uint64_t clip_id){
    EldenRtpClip v={0};
    if(!f||!clip||!f->enabled){if(clip)*clip=v;return;}
    v.id=clip_id;v.rate_hz=ELDEN_WAVE_RATE_HZ;v.mask=3;
    v.frames=f->compiler.recipe.total_frames;
    for(unsigned m=0;m<2;m++){
        v.gain[m]=f->compiler.calibration.gain[m];
        /* Reserve the measured worst moving-window cost once for the whole
         * streamed recipe; the actual per-chunk peak is not known up front. */
        v.peak_squares[m]=f->compiler.calibration.max_window_squares[m];
    }
    v.source=f;v.pull=elden_wave_feed_pull;v.consumed=elden_wave_feed_consumed;*clip=v;
}
/* Both motors confirmed every frame of the current chunk (pulled[] and
 * confirmed[] all reached chunk.frames): account it fed to the compiler and
 * compile the next bounded chunk. Confirmed frames already reached the FIFO,
 * so the resident chunk is safe to overwrite. Last chunk leaves have_chunk=0
 * so the worker's own sent/done accounting reports real completion. */
static int wave_advance(EldenWaveFeed *f){
    int rc=elden_wave_compiler_fed(&f->compiler,f->generation,f->chunk.frames);
    if(rc){f->error=rc;return rc;}
    if(f->exhausted){f->have_chunk=0;return 0;}
    rc=elden_wave_compiler_next(&f->compiler,f->generation,f->compiler.produced,&f->chunk);
    if(rc<0){f->error=rc;return rc;}
    f->pulled[0]=f->pulled[1]=0;f->confirmed[0]=f->confirmed[1]=0;if(rc==1)f->exhausted=1;
    return 0;
}
int elden_wave_feed_pull(void *source,unsigned motor,uint32_t first_frame,int8_t *out,size_t want){
    EldenWaveFeed *f=source;
    if(!f||!f->enabled||motor>1||!out||!want||want>64)return -22;
    if(f->error)return f->error;
    if(!f->have_chunk)return EldenAwBusy;
    /* This motor already staged the whole chunk: hold it until BOTH motors
     * confirm every frame (consumed retires it) so the FIFOs stay phase-locked
     * and no chunk is overwritten while a FIFO still owes or stages frames. */
    if(f->pulled[motor]>=f->chunk.frames)return EldenAwBusy;
    /* The worker's sent position must equal this chunk's staged cursor: a
     * skipped/gapped or replayed frame is a refusal, never a playable chunk. */
    if(first_frame!=f->chunk.first_frame+f->pulled[motor])return -116;
    uint32_t n=f->chunk.frames-f->pulled[motor];if(n>want)n=(uint32_t)want;
    for(uint32_t i=0;i<n;i++)out[i]=f->chunk.pcm[motor][f->pulled[motor]+i];
    f->pulled[motor]+=n;
    return (int)n;                 /* staged only — consumed() acks the write */
}
int elden_wave_feed_consumed(void *source,unsigned motor,uint32_t first_frame,size_t count){
    EldenWaveFeed *f=source;
    if(!f||!f->enabled||motor>1||!count)return -22;
    if(f->error)return f->error;
    if(!f->have_chunk)return -116;
    /* Confirm only staged frames, in order: first_frame must be this chunk's
     * confirmed cursor and count must not exceed staged-but-unconfirmed. */
    if(first_frame!=f->chunk.first_frame+f->confirmed[motor])return -116;
    if(count>f->pulled[motor]-f->confirmed[motor])return -116;
    f->confirmed[motor]+=(uint32_t)count;
    /* Retire the chunk only when BOTH motors confirmed every frame; the last
     * chunk retires here too (no later pull exists to notice it). */
    if(f->confirmed[0]==f->chunk.frames&&f->confirmed[1]==f->chunk.frames)
        return wave_advance(f);
    return 0;
}
int elden_wave_feed_stop(EldenWaveFeed *f){
    if(!f)return -22;
    if(!f->enabled)return -125;
    return elden_wave_compiler_stop(&f->compiler,f->generation);
}
