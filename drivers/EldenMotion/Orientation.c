/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Orientation.h"
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
    EldenMotionSpec accel={0},gyro={0};
    if(p->describe(p->context,EldenMotionAccel,&accel)||!spec_ok(&accel,EldenMotionAccel))return EldenMotionArgument;
    if(p->describe(p->context,EldenMotionGyro,&gyro)||!spec_ok(&gyro,EldenMotionGyro))return EldenMotionArgument;
    uint64_t owner=0,epoch=0;
    if(p->owner_epoch(p->context,&owner,&epoch)||!owner)return EldenMotionArgument;
    uint64_t first=0,second=0;
    if(p->map_time(p->context,1,&first)||p->map_time(p->context,2,&second)||!first||second<=first)return EldenMotionClock;
    uint64_t next_generation=s->generation+1;*s=(EldenMotionConsumer){0};
    s->provider=*p;s->rotate=rotate;s->rotate_context=rotate_context;
    s->accel=accel;s->gyro=gyro;s->owner=owner;s->epoch=epoch;
    s->hold_us=hold_us;s->generation=next_generation;s->state=EldenConsReady;s->enabled=1;
    *generation=next_generation;return 0;
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
    if(s->busy)return EldenOrientationBusy;
    uint64_t owner=0,epoch=0;
    s->busy=1;int rc=s->provider.owner_epoch(s->provider.context,&owner,&epoch);s->busy=0;
    if(s->state==EldenConsStopped)return EldenOrientationDisabled;
    if(rc||owner!=s->owner||epoch!=s->epoch){
        /* An ownership discontinuity is terminal for this generation: clear
         * the pending candidate so hold time can never accumulate across it,
         * keep the last published axis as the already accepted UI state and
         * fence every later sample until a fresh begin re-arms. A provider
         * re-reporting the armed identity must not resume the session. */
        s->rotation.candidate=0;s->rotation.axis=s->published_axis;
        s->state=EldenConsStopped;return EldenMotionStale;
    }
    EldenMotionSample v={0};
    s->busy=1;rc=s->provider.next(s->provider.context,&v);s->busy=0;
    if(s->state==EldenConsStopped)return EldenOrientationDisabled;
    if(rc<0)return rc;if(rc==0)return 2;if(rc!=1){s->rejected++;return EldenMotionPeer;}
    s->state=EldenConsRunning;
    const EldenMotionSpec *spec=v.kind==EldenMotionAccel?&s->accel:v.kind==EldenMotionGyro?&s->gyro:(void *)0;
    if(!spec||v.suid.low!=spec->suid.low||v.suid.high!=spec->suid.high||v.unit!=spec->unit||!v.timestamp_ticks){s->rejected++;return EldenMotionMalformed;}
    for(unsigned i=0;i<3;i++){
        int64_t a=v.q16[i];if(a<0)a=-a;
        if(a>spec->max_abs_q16){s->rejected++;return EldenMotionMalformed;}
    }
    if(v.timestamp_ticks==s->last_ticks){s->rejected++;return EldenMotionStale;}
    if(v.timestamp_ticks<s->last_ticks){s->clock_failed=1;s->rejected++;return EldenMotionClock;}
    uint64_t mach=0;
    s->busy=1;rc=s->provider.map_time(s->provider.context,v.timestamp_ticks,&mach);s->busy=0;
    if(s->state==EldenConsStopped)return EldenOrientationDisabled;
    if(rc||!mach||mach<=s->last_mach){s->clock_failed=1;s->rejected++;return EldenMotionClock;}
    /* The hold runs on real elapsed time: feed the classifier the qualified
     * mach_us converted into its 19.2 MHz tick domain, not the raw source
     * ticks (which may run at a different rate). Conversion is
     * overflow-checked and must stay monotonic. */
    uint64_t ticks19=0;
    if(!to_ticks19(mach,&ticks19)||ticks19<=s->last_ticks19){s->clock_failed=1;s->rejected++;return EldenMotionClock;}
    EldenMotionSample device=v;device.timestamp_ticks=ticks19;
    for(unsigned i=0;i<3;i++){int axis=spec->mount[i];device.q16[i]=axis<0?-v.q16[-axis-1]:v.q16[axis-1];}
    if(v.unit==EldenMotionMetersPerSecond2){for(unsigned i=0;i<3;i++)device.q16[i]=(int32_t)((int64_t)device.q16[i]*100000/980665);device.unit=EldenMotionG;}
    s->last_ticks=v.timestamp_ticks;s->last_mach=mach;s->last_ticks19=ticks19;s->samples++;
    if(v.kind==EldenMotionGyro){if(!v.accuracy){s->rejected++;return EldenMotionMalformed;}return 0;}
    int32_t axis=0;rc=elden_motion_rotation(&s->rotation,&device,s->hold_us,&axis);
    if(rc<0){s->rejected++;return rc;}
    if(rc==0){if(flat_or_poor(&device)){s->rejected++;return EldenMotionMalformed;}return 0;}
    s->busy=1;int sink=s->rotate(s->rotate_context,axis);s->busy=0;
    if(s->state==EldenConsStopped){s->rotation.axis=s->published_axis;s->dropped++;return EldenOrientationDisabled;}
    if(sink){s->rotation.axis=s->published_axis;s->rejected++;return sink<0?sink:EldenMotionPeer;}
    s->published_axis=axis;s->published++;return 1;
}
int elden_motion_consumer_stop(EldenMotionConsumer *s,uint64_t generation){
    if(!s||!s->enabled||s->state==EldenConsDisabled)return EldenOrientationDisabled;
    if(generation!=s->generation)return EldenMotionStale;
    if(s->state==EldenConsStopped)return EldenOrientationDisabled;
    s->state=EldenConsStopped;s->rotation.candidate=0;return 0;
}
int elden_motion_consumer_status(const EldenMotionConsumer *s,EldenMotionConsumerStatus *out){
    if(!s||!out)return EldenMotionArgument;
    *out=(EldenMotionConsumerStatus){s->state,s->samples,s->published,s->rejected,s->dropped,
        s->generation,s->owner,s->epoch,s->last_mach,s->last_ticks19,s->published_axis,s->rotation.candidate};
    return 0;
}
