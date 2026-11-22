/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Orientation.h"
/* Nonblocking whole-operation claim: exactly one sample/lifecycle/status
 * operation owns the consumer at a time. Never waits — a lost claim is the
 * Busy refusal both for foreign threads and for same-thread reentry from
 * inside a provider or sink callback. */
static int claim(EldenMotionConsumer *s){return atomic_exchange(&s->busy,1);}
static void unwind(EldenMotionConsumer *s){
    /* Swap out any deferred stop posted while this operation held the claim:
     * whoever takes the tag owns applying it — state plus pending-candidate
     * clear, identical to a direct stop. A tag for this generation always
     * applies; the tag is generation-scoped so a stale tag left by a retired
     * generation can never poison a re-armed successor (it is inert, not
     * lost — a later claimant on that generation still takes and runs it). */
    uint64_t dg=atomic_exchange(&s->stop_deferred,0);
    if(dg&&dg==s->generation&&s->state!=EldenConsStopped){
        atomic_store(&s->state,EldenConsStopped);s->rotation.candidate=0;
    }
    atomic_store(&s->busy,0);
}
/* Deferred-stop fence: true when a stop is pending for the live generation. */
static int stop_pending(const EldenMotionConsumer *s){
    return s->stop_deferred==s->generation&&s->stop_deferred!=0;
}
/* Real mount-matrix algebra, mirroring the existing signed-permutation/
 * determinant+1 admission in CorePublic.inc — a valid signed permutation of
 * each axis exactly once, product of signs and permutation parity even. */
static int mount_ok(const int8_t m[3]){
    unsigned mask=0,neg=0,inv=0;
    for(unsigned i=0;i<3;i++){
        int v=m[i];if(v<0){neg++;v=-v;}if(v<1||v>3||mask&(1u<<v))return 0;mask|=1u<<v;
        for(unsigned j=0;j<i;j++){int b=m[j];if(b<0)b=-b;if(b>v)inv++;}
    }
    return ((neg+inv)&1)==0;
}
static int spec_ok(const EldenMotionSpec *s,uint32_t kind){
    if(!s||s->kind!=kind||!(s->suid.low||s->suid.high)||s->max_abs_q16<=0||s->max_abs_q16>1000*65536)return 0;
    if(kind==EldenMotionAccel){if(s->unit!=EldenMotionG&&s->unit!=EldenMotionMetersPerSecond2)return 0;}
    else{if(s->unit!=EldenMotionRadiansPerSecond)return 0;}
    return mount_ok(s->mount);
}
int elden_motion_consumer_begin(EldenMotionConsumer *s,const EldenMotionProvider *p,
    EldenMotionRotate rotate,void *rotate_context,uint32_t hold_us,uint64_t *generation){
    if(!s||!p||!rotate||!generation||!hold_us||hold_us>1000000)return EldenMotionArgument;
    if(!p->describe||!p->owner_epoch||!p->map_time||!p->next)return EldenMotionArgument;
    if(s->enabled&&(s->state==EldenConsReady||s->state==EldenConsRunning))return EldenOrientationBusy;
    if(claim(s))return EldenOrientationBusy;
    /* Under the claim: a live run that appeared between the pre-check and the
     * claim still refuses, and an exhausted generation refuses before any
     * provider probe — an identity is never reused. */
    if(s->enabled&&(s->state==EldenConsReady||s->state==EldenConsRunning)){unwind(s);return EldenOrientationBusy;}
    if(s->generation==UINT64_MAX){unwind(s);return EldenMotionArgument;}
    EldenMotionSpec accel={0},gyro={0};
    if(p->describe(p->context,EldenMotionAccel,&accel)||!spec_ok(&accel,EldenMotionAccel)){unwind(s);return EldenMotionArgument;}
    if(p->describe(p->context,EldenMotionGyro,&gyro)||!spec_ok(&gyro,EldenMotionGyro)){unwind(s);return EldenMotionArgument;}
    uint64_t owner=0,epoch=0;
    if(p->owner_epoch(p->context,&owner,&epoch)||!owner){unwind(s);return EldenMotionArgument;}
    uint64_t first=0,second=0;
    if(p->map_time(p->context,1,&first)||p->map_time(p->context,2,&second)||!first||second<=first){unwind(s);return EldenMotionClock;}
    uint64_t next_generation=s->generation+1;
    /* Reinitialize only the run state — the claim flag and the (already
     * generation-scoped) deferred tag are lifecycle fields, not run state. */
    s->rotation=(EldenMotionRotation){0};s->last_mach=0;s->last_ticks=0;s->last_ticks19=0;
    s->samples=s->published=s->rejected=s->dropped=0;s->published_axis=0;s->clock_failed=0;
    s->provider=*p;s->rotate=rotate;s->rotate_context=rotate_context;
    s->accel=accel;s->gyro=gyro;s->owner=owner;s->epoch=epoch;
    s->hold_us=hold_us;
    atomic_store(&s->generation,next_generation);
    atomic_store(&s->state,EldenConsReady);atomic_store(&s->enabled,1);
    *generation=next_generation;
    unwind(s);return 0;
}
/* The same gravity plausibility the existing classifier applies — a dominant-
 * axis candidate requires an in-bounds, plausible-magnitude, quality sample. */
static int flat_or_poor(const EldenMotionSample *v){
    int64_t x=v->q16[0],y=v->q16[1],z=v->q16[2],ax=x<0?-x:x,ay=y<0?-y:y;
    if(ax>2*65536||ay>2*65536||z>2*65536||z<-2*65536)return 1;
    int64_t magnitude=x*x+y*y+z*z;
    return !v->accuracy||magnitude<(int64_t)49152*49152||magnitude>(int64_t)81920*81920;
}
/* Qualified mach_us -> the classifier's documented 19.2 MHz tick domain, with
 * the same ceiling rounding as the existing SSC hold conversion
 * ((us*96+4)/5). A zero input or a multiplication that would overflow
 * refuses; the caller treats either as a clock failure. */
static int to_ticks19(uint64_t mach_us,uint64_t *out){
    if(!mach_us||mach_us>(UINT64_MAX-4)/96)return 0;
    *out=(mach_us*96+4)/5;return 1;
}
int elden_motion_consumer_sample(EldenMotionConsumer *s,uint64_t generation){
    if(!s||!s->enabled||s->state==EldenConsDisabled||s->state==EldenConsStopped)return EldenOrientationDisabled;
    if(generation!=s->generation)return EldenMotionStale;
    if(s->clock_failed)return EldenMotionClock;
    if(claim(s))return EldenOrientationBusy;
    /* Re-fence under the claim: a stop applied or a re-arm completed between
     * the pre-checks and the claim is caught here, before any provider I/O. */
    if(s->state==EldenConsStopped){unwind(s);return EldenOrientationDisabled;}
    if(generation!=s->generation){unwind(s);return EldenMotionStale;}
    if(s->clock_failed){unwind(s);return EldenMotionClock;}
    uint64_t owner=0,epoch=0;
    int rc=s->provider.owner_epoch(s->provider.context,&owner,&epoch);
    if(stop_pending(s)){unwind(s);return EldenOrientationDisabled;}
    if(rc||owner!=s->owner||epoch!=s->epoch){
        /* An ownership discontinuity is terminal for this generation: clear
         * the pending candidate so hold time can never accumulate across it,
         * keep the last published axis as the already accepted UI state and
         * fence every later sample until a fresh begin re-arms. A provider
         * re-reporting the armed identity must not resume the session. */
        s->rotation.candidate=0;s->rotation.axis=s->published_axis;
        atomic_store(&s->state,EldenConsStopped);unwind(s);return EldenMotionStale;
    }
    EldenMotionSample v={0};
    rc=s->provider.next(s->provider.context,&v);
    if(stop_pending(s)){unwind(s);return EldenOrientationDisabled;}
    if(rc<0){unwind(s);return rc;}if(rc==0){unwind(s);return 2;}
    if(rc!=1){s->rejected++;unwind(s);return EldenMotionPeer;}
    atomic_store(&s->state,EldenConsRunning);
    const EldenMotionSpec *spec=v.kind==EldenMotionAccel?&s->accel:v.kind==EldenMotionGyro?&s->gyro:(void *)0;
    if(!spec||v.suid.low!=spec->suid.low||v.suid.high!=spec->suid.high||v.unit!=spec->unit||!v.timestamp_ticks){s->rejected++;unwind(s);return EldenMotionMalformed;}
    for(unsigned i=0;i<3;i++){
        int64_t a=v.q16[i];if(a<0)a=-a;
        if(a>spec->max_abs_q16){s->rejected++;unwind(s);return EldenMotionMalformed;}
    }
    if(v.timestamp_ticks==s->last_ticks){s->rejected++;unwind(s);return EldenMotionStale;}
    if(v.timestamp_ticks<s->last_ticks){s->clock_failed=1;s->rejected++;unwind(s);return EldenMotionClock;}
    uint64_t mach=0;
    rc=s->provider.map_time(s->provider.context,v.timestamp_ticks,&mach);
    if(stop_pending(s)){unwind(s);return EldenOrientationDisabled;}
    if(rc||!mach||mach<=s->last_mach){s->clock_failed=1;s->rejected++;unwind(s);return EldenMotionClock;}
    /* The hold runs on real elapsed time: feed the classifier the qualified
     * mach_us converted into its 19.2 MHz tick domain, not the raw source
     * ticks (which may run at a different rate). Conversion is
     * overflow-checked and must stay monotonic. */
    uint64_t ticks19=0;
    if(!to_ticks19(mach,&ticks19)||ticks19<=s->last_ticks19){s->clock_failed=1;s->rejected++;unwind(s);return EldenMotionClock;}
    EldenMotionSample device=v;device.timestamp_ticks=ticks19;
    for(unsigned i=0;i<3;i++){int axis=spec->mount[i];device.q16[i]=axis<0?-v.q16[-axis-1]:v.q16[axis-1];}
    if(v.unit==EldenMotionMetersPerSecond2){for(unsigned i=0;i<3;i++)device.q16[i]=(int32_t)((int64_t)device.q16[i]*100000/980665);device.unit=EldenMotionG;}
    s->last_ticks=v.timestamp_ticks;s->last_mach=mach;s->last_ticks19=ticks19;s->samples++;
    if(v.kind==EldenMotionGyro){if(!v.accuracy){s->rejected++;unwind(s);return EldenMotionMalformed;}unwind(s);return 0;}
    int32_t axis=0;rc=elden_motion_rotation(&s->rotation,&device,s->hold_us,&axis);
    if(rc<0){s->rejected++;unwind(s);return rc;}
    if(rc==0){if(flat_or_poor(&device)){s->rejected++;unwind(s);return EldenMotionMalformed;}unwind(s);return 0;}
    int sink=s->rotate(s->rotate_context,axis);
    if(stop_pending(s)){s->rotation.axis=s->published_axis;s->dropped++;unwind(s);return EldenOrientationDisabled;}
    if(sink){s->rotation.axis=s->published_axis;s->rejected++;unwind(s);return sink<0?sink:EldenMotionPeer;}
    s->published_axis=axis;s->published++;unwind(s);return 1;
}
int elden_motion_consumer_stop(EldenMotionConsumer *s,uint64_t generation){
    if(!s||!s->enabled||s->state==EldenConsDisabled)return EldenOrientationDisabled;
    if(generation!=s->generation)return EldenMotionStale;
    if(s->state==EldenConsStopped)return EldenOrientationDisabled;
    int posted=0;
    if(claim(s)){
        /* Another operation holds the claim: post the deferred stop for this
         * generation (the holder applies it at its next fence or unwind),
         * then retry the claim once — that closes the window where the
         * holder already unwound between our exchange and our store, so a
         * deferred stop is never lost. */
        atomic_store(&s->stop_deferred,generation);posted=1;
        if(claim(s))return 0;
    }
    /* Under the claim: refuse if a re-arm replaced the generation since the
     * pre-check. An already-stopped run is the pre-check's Disabled answer
     * unless our own deferred post was the one applied — then it applied. */
    if(generation!=s->generation){atomic_store(&s->busy,0);return EldenMotionStale;}
    if(s->state==EldenConsStopped){
        if(s->stop_deferred==generation)atomic_store(&s->stop_deferred,0);
        atomic_store(&s->busy,0);return posted?0:EldenOrientationDisabled;
    }
    atomic_store(&s->state,EldenConsStopped);s->rotation.candidate=0;
    if(s->stop_deferred==generation)atomic_store(&s->stop_deferred,0);
    atomic_store(&s->busy,0);return 0;
}
int elden_motion_consumer_status(EldenMotionConsumer *s,EldenMotionConsumerStatus *out){
    if(!s||!out)return EldenMotionArgument;
    if(claim(s))return EldenOrientationBusy;
    *out=(EldenMotionConsumerStatus){s->state,s->samples,s->published,s->rejected,s->dropped,
        s->generation,s->owner,s->epoch,s->last_mach,s->last_ticks19,s->published_axis,s->rotation.candidate};
    unwind(s);return 0;
}
