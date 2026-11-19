/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Live.h"
static uint64_t ticks_us(uint32_t us){return ((uint64_t)us*96+4)/5;}
static void page(EldenActuatorLive *l,uint32_t state,int error){
    __atomic_store_n(&l->ring->page->error,error,__ATOMIC_RELAXED);
    __atomic_store_n(&l->ring->page->state,state,__ATOMIC_RELEASE);
}
static int limits(const EldenActuatorLimits *c){
    if(!c||!c->mask||c->mask>3||c->window_frames<64||c->window_frames>240000||!c->max_run_us||c->max_run_us>60000000||
       !c->max_gap_us||c->max_gap_us>10000||!c->max_missing_frames||c->max_missing_frames>48000||
       !c->silence_frames||c->silence_frames>24000||!c->measured_min_payload_bps||
       !c->max_startup_lateness_us||c->max_startup_lateness_us>250000)return 0;
    unsigned motors=0;
    for(unsigned m=0;m<2;m++)if(c->mask&(1u<<m)){
        motors++;if(!c->gain[m]||c->gain[m]>128||!c->max_on_frames[m]||c->max_on_frames[m]>c->window_frames||!c->max_squares[m]||
            c->max_squares[m]>(uint64_t)c->gain[m]*c->gain[m]*16129*c->window_frames||
            c->sample_map[m].rate_hz!=24000||c->sample_map[m].max_frames<64)return 0;
        int16_t zero=0;int8_t mapped=1;uint64_t cost;
        if(elden_haptics_samples(&zero,1,32767,&c->sample_map[m],&mapped,&cost)||mapped||cost)return 0;
    }
    return c->measured_min_payload_bps>=motors*24000u*5u/4u;
}
int elden_actuator_live_init(EldenActuatorLive *l,EldenActuatorRing *ring,EldenAwChip *chips[2],const EldenActuatorLimits *c,
    EldenActuatorHistory *history,size_t bytes,const EldenActuatorLiveIo *io){
    if(!l||l->ring||!ring||!history||!io||!io->admit||!io->reserve||!io->finish||!io->host||!limits(c)||
       !chips||bytes!=(size_t)c->window_frames*sizeof(*history)||elden_actuator_ring_check(ring))return -22;
    for(unsigned m=0;m<2;m++)if(c->mask&(1u<<m))if(!chips[m]||!chips[m]->ready||chips[m]->held||chips[m]->busy||c->gain[m]>chips[m]->max_gain)return -13;
    int rc=io->admit(io->context,ring->owner,ring->epoch,0);if(rc)return rc;
    EldenActuatorLive value={0};value.ring=ring;value.chips[0]=chips[0];value.chips[1]=chips[1];value.limits=*c;value.io=*io;
    value.history=history;value.history_bytes=bytes;value.next_frame=ring->taken;value.state=EldenActuatorIdle;*l=value;
    for(size_t i=0;i<bytes;i++)((uint8_t *)history)[i]=0;page(l,EldenActuatorReady,0);return 0;
}
static int uncertain(EldenActuatorLive *l,int error){
    l->state=EldenActuatorUncertain;l->error=error;page(l,EldenActuatorHeld,error);
    if(l->reserved)(void)l->io.finish(l->io.context,l->ring->owner,l->ring->epoch,0,0,0);return error;
}
int elden_actuator_live_stop(EldenActuatorLive *l){
    if(!l||!l->ring)return -22;int error=0;
    uint32_t prior=l->state;
    for(unsigned m=0;m<2;m++)if(l->rtp[m].chip){int rc=elden_aw_rtp_stop(&l->rtp[m]);if(rc&&!error)error=rc;}
    if(error)return uncertain(l,error);
    if(l->state==EldenActuatorUncertain)return l->error?l->error:-125;
    if(l->reserved){
        uint64_t host=l->io.host(l->io.context);if(host<l->run_start_host)return uncertain(l,-110);
        uint64_t elapsed=(host-l->run_start_host)/800+((host-l->run_start_host)%800!=0);uint32_t on[2]={0};
        if(elapsed>l->limits.window_frames)elapsed=l->limits.window_frames;
        for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m)){
            on[m]=(uint32_t)elapsed;if(on[m]>l->limits.max_on_frames[m])return uncertain(l,-13);}
        int rc=l->io.finish(l->io.context,l->ring->owner,l->ring->epoch,1,l->run_peak_squares,on);
        if(rc)return uncertain(l,rc);l->reserved=0;
    }
    l->pending_frames=0;l->pending_mask=0;l->state=prior==EldenActuatorTerminal?EldenActuatorTerminal:EldenActuatorIdle;l->quiet=0;
    for(unsigned m=0;m<2;m++){EldenAwRtp zero={0};l->rtp[m]=zero;}
    page(l,(l->state==EldenActuatorTerminal?0u:EldenActuatorReady)|EldenActuatorStopped,l->error);return 0;
}
static int failed(EldenActuatorLive *l,int error){
    l->error=error;int rc=elden_actuator_live_stop(l);if(rc)return rc;
    l->state=EldenActuatorTerminal;page(l,EldenActuatorStopped,error);return error;
}
int elden_actuator_live_new_run(EldenActuatorLive *l,uint64_t host){
    if(!l||!l->ring||l->state!=EldenActuatorIdle||l->reserved||l->pending_frames)return -125;
    for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m))
        if(l->rtp[m].owned||l->rtp[m].held||l->chips[m]->busy||l->chips[m]->held)return -125;
    if(l->has_clock&&host<l->last_host)return -110;
    uint64_t first=elden_actuator_frame(l->ring,host)&~UINT64_C(1);
    // Previously prefetched frames may be a few samples ahead of host time.
    // Never move the cursor backwards or reuse that old generation.
    if(first<l->next_frame)first=l->next_frame;
    uint64_t skipped=(first-l->next_frame)/2;
    if(skipped>=l->limits.window_frames){
        for(size_t i=0;i<l->history_bytes;i++)((uint8_t *)l->history)[i]=0;
        l->history_count=0;l->history_at=0;l->rolling_squares[0]=l->rolling_squares[1]=0;l->rolling_on[0]=l->rolling_on[1]=0;
    }else for(uint64_t i=0;i<skipped;i++){
        EldenActuatorHistory *h=&l->history[l->history_at];
        if(l->history_count==l->limits.window_frames)for(unsigned m=0;m<2;m++){
            l->rolling_squares[m]-=h->squares[m];if(h->on_mask&(1u<<m))l->rolling_on[m]--;}
        else l->history_count++;
        h->squares[0]=h->squares[1]=0;h->on_mask=0;h->run=0;l->history_at=(l->history_at+1)%l->limits.window_frames;
    }
    int rc=elden_actuator_ring_new_run(l->ring,first);if(rc)return rc;
    l->next_frame=first;l->missing=l->quiet=0;l->last_host=host;l->has_clock=1;return 0;
}
static int record(EldenActuatorLive *l,const int8_t samples[2][64],unsigned count,uint8_t on_mask){
    uint64_t square[2]={l->rolling_squares[0],l->rolling_squares[1]};uint32_t on[2]={l->rolling_on[0],l->rolling_on[1]};
    uint64_t run[2]={l->run_squares[0],l->run_squares[1]},peak[2]={l->run_peak_squares[0],l->run_peak_squares[1]};
    unsigned size=l->limits.window_frames;size_t at=l->history_at;unsigned used=l->history_count;
    // Pure preflight before history/hardware changes. count<=64; each slot
    // occurs once in this block, even when the ring wraps.
    if(count>size)return -22;
    for(unsigned i=0;i<count;i++){
        const EldenActuatorHistory *old=&l->history[at];
        for(unsigned m=0;m<2;m++){
            if(used==size){square[m]-=old->squares[m];if(old->on_mask&(1u<<m))on[m]--;
                if(old->run==l->run_sequence&&old->run)run[m]-=old->squares[m];}
            if(on_mask&(1u<<m)){
                int32_t value=samples[m][i];uint64_t cost=(uint64_t)value*value*l->limits.gain[m]*l->limits.gain[m];
                square[m]+=cost;run[m]+=cost;on[m]++;
            }
            if(square[m]>l->limits.max_squares[m]||on[m]>l->limits.max_on_frames[m])return -13;
            if(run[m]>peak[m])peak[m]=run[m];
        }
        if(used<size)used++;at=(at+1)%size;
    }
    for(unsigned i=0;i<count;i++){
        EldenActuatorHistory *h=&l->history[l->history_at];h->on_mask=on_mask;h->run=on_mask?l->run_sequence:0;
        for(unsigned m=0;m<2;m++){int32_t value=samples[m][i];h->squares[m]=on_mask&(1u<<m)?(uint32_t)((uint64_t)value*value*l->limits.gain[m]*l->limits.gain[m]):0;}
        l->history_at=(l->history_at+1)%size;
    }
    for(unsigned m=0;m<2;m++){l->rolling_squares[m]=square[m];l->rolling_on[m]=on[m];l->run_squares[m]=run[m];l->run_peak_squares[m]=peak[m];}
    l->history_count=used;return 0;
}
static int begin(EldenActuatorLive *l){
    int rc=l->io.reserve(l->io.context,l->ring->owner,l->ring->epoch,l->limits.max_squares,l->limits.max_on_frames);if(rc)return rc;l->reserved=1;
    for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m))l->rtp[m].chip=l->chips[m];
    for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m)){
        rc=elden_aw_rtp_prepare(&l->rtp[m],l->chips[m],24000,l->limits.gain[m],l->limits.max_run_us+500000);if(rc)return rc;
    }
    l->run_start_host=l->io.host(l->io.context);l->state=EldenActuatorPreparing;
    for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m)){rc=elden_aw_rtp_go(&l->rtp[m]);if(rc)return rc;}
    l->last_host=l->io.host(l->io.context);
    if(l->first_frame>(UINT64_MAX-l->ring->anchor)/400)return -22;
    uint64_t expected=l->ring->anchor+l->first_frame*400;
    if(l->run_start_host>expected&&l->run_start_host-expected>ticks_us(l->limits.max_startup_lateness_us))return -110;
    return 0;
}
static int feed(EldenActuatorLive *l){
    if(!l->pending_frames)return 0;
    for(unsigned m=0;m<2;m++)if(l->pending_mask&(1u<<m)){
        EldenAwRtpStatus s;int rc=elden_aw_rtp_status(&l->rtp[m],&s);if(rc)return failed(l,rc);
        if(s.state!=8){
            if(l->state==EldenActuatorPlaying&&s.state==0)return failed(l,-61);
            uint64_t host=l->io.host(l->io.context);
            if(host<l->run_start_host||host-l->run_start_host>ticks_us(l->limits.max_startup_lateness_us))return failed(l,-110);
            return 0;
        }
        if(s.almost_full)return 0;
        rc=elden_aw_rtp_feed(&l->rtp[m],l->pending[m],l->pending_frames);if(rc==EldenAwBusy)return 0;if(rc)return failed(l,rc);
        l->pending_mask&=(uint8_t)~(1u<<m);
    }
    if(!l->pending_mask){__atomic_fetch_add(&l->ring->page->frames_sent,l->pending_frames,__ATOMIC_RELAXED);l->pending_frames=0;l->state=EldenActuatorPlaying;page(l,EldenActuatorReady|EldenActuatorRunning,0);}
    return 0;
}
int elden_actuator_live_step(EldenActuatorLive *l,int requested){
    if(!l||!l->ring||(requested!=0&&requested!=1))return -22;
    if(l->state==EldenActuatorUncertain||l->state==EldenActuatorTerminal)return l->error?l->error:-125;
    if(!requested)return elden_actuator_live_stop(l);
    if(elden_actuator_ring_check(l->ring))return failed(l,-84);
    int rc=l->io.admit(l->io.context,l->ring->owner,l->ring->epoch,0);if(rc)return failed(l,rc);
    uint64_t now=l->io.host(l->io.context);
    if(l->has_clock&&(now<l->last_host||((l->state!=EldenActuatorIdle)&&now-l->last_host>ticks_us(l->limits.max_gap_us))))return failed(l,-110);
    l->has_clock=1;l->last_host=now;
    if(l->state!=EldenActuatorIdle&&now-l->run_start_host>=ticks_us(l->limits.max_run_us))return failed(l,-27);
    if(l->state!=EldenActuatorIdle){
        uint64_t frames=(now-l->run_start_host)/800; // physical motor-on, also while FIFO waits
        for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m)){
            if(l->limits.max_on_frames[m]<l->limits.window_frames&&frames>l->limits.max_on_frames[m])return failed(l,-13);
        }
    }
    if(l->pending_frames){rc=feed(l);if(rc||l->pending_frames)return rc;}
    uint64_t target=elden_actuator_frame(l->ring,now);
    if(target>UINT64_MAX-ELDEN_ACTUATOR_TAKE_FRAMES)return failed(l,-22);
    target+=ELDEN_ACTUATOR_TAKE_FRAMES;
    // No stale turn is replayed after idle. Clear only already-stopped history
    // for the elapsed silent interval, not a fresh gain/budget reset.
    if(l->state==EldenActuatorIdle&&target>l->next_frame&&target-l->next_frame>ELDEN_ACTUATOR_RING_FRAMES){
        uint64_t elapsed=(target-l->next_frame)/2;
        if(elapsed>=l->limits.window_frames){for(size_t i=0;i<l->history_bytes;i++)((uint8_t *)l->history)[i]=0;l->history_count=0;l->history_at=0;
            l->rolling_squares[0]=l->rolling_squares[1]=0;l->rolling_on[0]=l->rolling_on[1]=0;}
        l->next_frame=(target&~UINT64_C(1));elden_actuator_ring_taken(l->ring,l->next_frame);
    }
    if(l->next_frame&1)l->next_frame++;
    uint64_t due=(target>l->next_frame?target-l->next_frame:0)/2;
    unsigned count=due>64?64:(unsigned)due;if(!count)return 0;
    int16_t raw[64];unsigned loud=0;uint64_t first=l->next_frame;
    for(unsigned i=0;i<count;i++){
        int16_t a=0,b=0;int ra=elden_actuator_ring_read(l->ring,l->next_frame+i*2,&a),rb=elden_actuator_ring_read(l->ring,l->next_frame+i*2+1,&b);
        if((ra&&ra!=-61)||(rb&&rb!=-61))return failed(l,ra&&ra!=-61?ra:rb);
        if(ra||rb){if(l->missing<=UINT32_MAX-2)l->missing+=2;__atomic_fetch_add(&l->ring->page->dropped,2u,__ATOMIC_RELAXED);}
        else l->missing=0;
        raw[i]=(int16_t)(((int32_t)a+b)/2);if(raw[i])loud=1;
    }
    if(l->state!=EldenActuatorIdle&&l->missing>l->limits.max_missing_frames)return failed(l,-61);
    int8_t converted[2][64]={{0}};
    for(unsigned m=0;m<2;m++)if(l->limits.mask&(1u<<m)){
        uint64_t cost;rc=elden_haptics_samples(raw,count,32767,&l->limits.sample_map[m],converted[m],&cost);if(rc)return failed(l,rc);
    }
    l->next_frame+=count*2;elden_actuator_ring_taken(l->ring,l->next_frame);__atomic_fetch_add(&l->ring->page->frames_consumed,count*2,__ATOMIC_RELAXED);
    if(l->state==EldenActuatorIdle&&!loud)return record(l,converted,count,0);
    l->first_frame=first;
    if(l->state==EldenActuatorIdle){
        if(l->run_sequence==UINT64_MAX)return failed(l,-22);l->run_sequence++;
        l->run_squares[0]=l->run_squares[1]=0;l->run_peak_squares[0]=l->run_peak_squares[1]=0;
    }
    rc=record(l,converted,count,l->limits.mask);if(rc)return failed(l,rc);
    if(l->state==EldenActuatorIdle){rc=begin(l);if(rc)return failed(l,rc);}
    if(loud)l->quiet=0;else l->quiet+=count;
    for(unsigned m=0;m<2;m++)for(unsigned i=0;i<count;i++)l->pending[m][i]=converted[m][i];l->pending_frames=count;l->pending_mask=l->limits.mask;
    rc=feed(l);if(rc)return rc;
    if(!l->pending_frames&&l->quiet>=l->limits.silence_frames)return elden_actuator_live_stop(l);return 0;
}
