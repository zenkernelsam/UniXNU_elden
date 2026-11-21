/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY synthetic provider peers — no real NT36536 worker, SPI bus, HID
 * service or Pencil acceptance is present or claimed. Every provider/send
 * callback is a test double that proves the facade's fences, never that a real
 * AP501U or touch owner works. */
#include "Owner.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenPenLimits limits={19040,30400,4095,60};
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
/* Envelope-clean packets: bytes1..65 stay zero so the byte1..65 sum passes. */
static void coord(uint8_t *p,unsigned pressure){
    memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);
}
static void meta(uint8_t *p){memset(p,0,109);p[66]=0xf1;for(unsigned i=0;i<9;i++)p[67+i]=(uint8_t)(i+1);}
static void leave(uint8_t *p){memset(p,0,109);p[66]=0xff;}
static void badenv(uint8_t *p){memset(p,0,109);p[1]=0xff;p[66]=1;put(p,71,100);}
typedef struct {unsigned count,fail;EldenPenFrame trace[64];
                EldenPenOwner *f;uint64_t gen,stop_now;int stop_in_send;} Sink;
static int send(void *p,const EldenPenFrame *f){
    Sink *s=p;
    if(s->stop_in_send){s->stop_in_send=0;elden_pen_owner_stop(s->f,s->gen,s->stop_now);}
    if(s->fail){--s->fail;return -99;}CHECK(s->count<64);s->trace[s->count++]=*f;return 0;
}
typedef struct {
    EldenPenOwner *f;uint64_t gen;
    const char *pname;uint64_t powner,pepoch;
    uint64_t ro_owner,ro_epoch,rf_owner,rf_epoch;   /* reported identities */
    int oe_fail,fo_fail,pk_fail;unsigned pk_fail_at;
    const uint8_t *pkts[24];uint64_t pts[24],pseq[24];unsigned npkts,pk_at;
    unsigned oe_calls,fo_calls,pk_calls;
    int reenter_op,reenter_rc,stop_in_packet,stop_in_oe;uint64_t stop_now;
    int oe_reenter_op,oe_reenter_rc;
    int reenter_arg;
} M;
static uint8_t PK[24][109];
static void queue(M*m,const uint8_t*p,uint64_t ts,uint64_t seq){
    assert(m->npkts<24);memcpy(PK[m->npkts],p,109);
    m->pkts[m->npkts]=PK[m->npkts];m->pts[m->npkts]=ts;m->pseq[m->npkts]=seq;++m->npkts;
}
static const char *p_name(void *c){M*m=c;return m->pname;}
static int p_oe(void *c,uint64_t *o,uint64_t *e){
    M*m=c;++m->oe_calls;
    if(m->oe_reenter_op){int op=m->oe_reenter_op;m->oe_reenter_op=0;
        m->oe_reenter_rc= op==1?elden_pen_owner_packet(m->f,m->gen)
                         :op==2?elden_pen_owner_tick(m->f,m->gen,900)
                         :elden_pen_owner_release(m->f,m->gen);}
    if(m->stop_in_oe){m->stop_in_oe=0;elden_pen_owner_stop(m->f,m->gen,m->stop_now);}
    if(m->oe_fail)return -1;*o=m->ro_owner;*e=m->ro_epoch;return 0;
}
static int p_fo(void *c,uint64_t *o,uint64_t *e){
    M*m=c;++m->fo_calls;if(m->fo_fail)return -1;*o=m->rf_owner;*e=m->rf_epoch;return 0;
}
static int p_pk(void *c,uint8_t *out,uint64_t *ts,uint64_t *seq){
    M*m=c;++m->pk_calls;
    if(m->reenter_op){int op=m->reenter_op;m->reenter_op=0;
        m->reenter_rc= op==1?elden_pen_owner_packet(m->f,m->gen)
                      :op==2?elden_pen_owner_tick(m->f,m->gen,900)
                      :op==3?elden_pen_owner_pause(m->f,m->gen,900)
                      :op==4?elden_pen_owner_resume(m->f,m->gen,900)
                      :op==5?elden_pen_owner_fault(m->f,m->gen,900)
                      :op==7?elden_pen_owner_release(m->f,m->gen)
                      :elden_pen_owner_retire(m->f,m->gen);}
    if(m->stop_in_packet){m->stop_in_packet=0;elden_pen_owner_stop(m->f,m->gen,m->stop_now);}
    if(m->pk_fail||m->pk_calls==m->pk_fail_at)return -17;
    if(m->pk_at>=m->npkts)return -1;
    memcpy(out,m->pkts[m->pk_at],109);*ts=m->pts[m->pk_at];*seq=m->pseq[m->pk_at];++m->pk_at;return 0;
}
static EldenPenProvider PROV(M*m){EldenPenProvider p={m,p_name,p_oe,p_fo,p_pk};return p;}
static void setup(M*m,EldenPenDelivery *d,EldenPenOwner *f,Sink *sink){
    memset(m,0,sizeof(*m));memset(d,0,sizeof(*d));memset(sink,0,sizeof(*sink));
    m->f=f;sink->f=f;sink->stop_now=900;m->pname="ap501u-nt36536-sampler";
    m->powner=7;m->pepoch=3;m->ro_owner=7;m->ro_epoch=3;
    m->rf_owner=11;m->rf_epoch=22;m->stop_now=900;
    assert(!elden_pen_delivery_init(d,100));
    assert(!elden_pen_owner_init(f,d,&limits,send,sink));
}
static uint8_t buf[109];
static uint8_t *down(unsigned p){coord(buf,p);return buf;}
static uint8_t *hover(void){coord(buf,0);put(buf,75,0xffff);return buf;}
static void bind_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    /* Missing provider and each missing callback refuse before any bind. */
    CHECK(elden_pen_owner_bind(&f,0,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);
    p.name=0;CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.name=p_name;
    p.owner_epoch=0;CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.owner_epoch=p_oe;
    p.frame_owner=0;CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.frame_owner=p_fo;
    p.packet=0;CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.packet=p_pk;
    CHECK(elden_pen_owner_bind(&f,&p,"",11,22,&gen)==EldenPenDeliveryArgument);
    CHECK(elden_pen_owner_bind(&f,&p,"other-provider",11,22,&gen)==EldenPenDeliveryArgument);
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",0,22,&gen)==EldenPenDeliveryArgument); /* frame owner required */
    m.powner=0;m.ro_owner=0;
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument); /* owner nonzero */
    m.powner=7;m.ro_owner=7;
    m.rf_owner=99;
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenOwnerForeign); /* provider's frame owner must match */
    m.rf_owner=11;
    m.oe_fail=1;CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);m.oe_fail=0;
    /* Missing provider: every facade op refuses NoProvider, not decode/fault. */
    CHECK(elden_pen_owner_packet(&f,1)==EldenPenNoProvider);
    CHECK(elden_pen_owner_tick(&f,1,1)==EldenPenNoProvider);
    CHECK(elden_pen_owner_fault(&f,1,1)==EldenPenNoProvider);
    CHECK(elden_pen_owner_pause(&f,1,1)==EldenPenNoProvider);
    CHECK(elden_pen_owner_resume(&f,1,1)==EldenPenNoProvider);
    CHECK(elden_pen_owner_stop(&f,1,1)==EldenPenOwnerStale);          /* gen_live gate */
    CHECK(elden_pen_owner_release(&f,1)==EldenPenOwnerStale);
    CHECK(elden_pen_owner_retire(&f,1)==EldenPenOwnerStale);
    CHECK(!f.packets&&!f.decoded&&!sink.count&&!d.sent);
    /* Qualified bind: named provider + nonzero owner + matching frame owner. */
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)&&gen==1);
    m.gen=gen;
    CHECK(elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenOwnerBusy);   /* double bind */
    CHECK(elden_pen_owner_packet(&f,gen+9)==EldenPenOwnerStale);      /* foreign generation */
    CHECK(f.rejected&&f.bound&&!f.stopped);
}
static void packet_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    /* Happy path: coordinate packet -> decoded -> sink; repeat is deduplicated. */
    queue(&m,down(100),1,1);queue(&m,down(100),2,2);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1&&sink.trace[0].tip&&sink.trace[0].pressure==100);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1&&f.packets==2&&f.decoded==2);
    /* Signed tilt, distance and barrel buttons pass through existing decode. */
    coord(buf,50);buf[73]=196;buf[74]=60;buf[77]=3;queue(&m,buf,3,3);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==2);
    CHECK(sink.trace[1].tilt_x==-60&&sink.trace[1].tilt_y==60&&sink.trace[1].buttons==3);
    queue(&m,hover(),4,4);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==3&&!sink.trace[2].tip&&sink.trace[2].in_range&&sink.trace[2].distance==0xffff);
    /* Metadata-only packet: decoded + counted, never a contact, no send. */
    meta(buf);queue(&m,buf,5,5);
    CHECK(!elden_pen_owner_packet(&f,gen)&&f.metadata==1&&sink.count==3&&sink.trace[2].in_range);
    /* Leave packet releases through the existing delivery release path. */
    leave(buf);queue(&m,buf,6,6);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==4&&!sink.trace[3].tip&&!sink.trace[3].in_range);
    CHECK(elden_pen_owner_released(&f)&&elden_pen_delivery_released(&d));
    /* owner_epoch is rechecked before every facade op, not only pulls. */
    unsigned base=m.oe_calls;
    CHECK(!elden_pen_owner_tick(&f,gen,7)&&!elden_pen_owner_fault(&f,gen,8));
    CHECK(m.oe_calls>=base+2&&f.faults==1);
}
static void drift_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    queue(&m,down(100),1,1);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1&&sink.trace[0].tip);
    /* Identity drift on the live generation refuses AND lifts the held
     * contact through the neutral release — the provider is never pulled. */
    m.ro_owner=99;unsigned pk=m.pk_calls;
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&m.pk_calls==pk&&f.packets==1);
    CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));
    m.ro_owner=7;m.ro_epoch=99;
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&sink.count==2&&f.packets==1);
    m.ro_epoch=3;m.oe_fail=1;
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&sink.count==2);m.oe_fail=0;
    /* The undelivered queued sample lands once identity is restored. */
    queue(&m,down(120),2,2);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==3&&sink.trace[2].pressure==120);
    /* Foreign frame owner with a fresh contact held: refused + lifted. */
    m.rf_owner=77;queue(&m,down(140),4,4);queue(&m,down(150),5,5);queue(&m,down(160),6,6);
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerForeign&&f.foreign_frames==1);
    CHECK(sink.count==4&&!sink.trace[3].tip&&elden_pen_owner_released(&f));
    m.rf_owner=11;m.fo_fail=1;
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerForeign&&f.foreign_frames==2&&sink.count==4);m.fo_fail=0;
    /* And a clean packet is still admitted after the foreign refusal. */
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==5&&sink.trace[4].pressure==160);
}
static void order_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    queue(&m,down(100),10,5);queue(&m,down(110),11,6);queue(&m,down(120),12,7);
    CHECK(!elden_pen_owner_packet(&f,gen)&&!elden_pen_owner_packet(&f,gen));
    /* Sequence rollback refused; the following in-order packet still lands. */
    CHECK(!elden_pen_owner_packet(&f,gen)&&f.packets==3&&sink.count==3);
    queue(&m,down(130),13,6);  /* seq repeats last accepted */
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&f.stale_seq==1&&f.packets==3);
    /* The rollback refusal lifts the contact the session was still holding. */
    CHECK(sink.count==4&&!sink.trace[3].tip&&elden_pen_owner_released(&f));
    queue(&m,down(130),14,7);  /* seq<last_seq */
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&f.stale_seq==2&&f.packets==3&&sink.count==4);
    /* Timestamp rollback refused separately, already-released stays quiet. */
    queue(&m,down(130),11,8);
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&f.stale_ts==1&&f.packets==3&&sink.count==4);
    queue(&m,down(130),12,8);  /* ts==last_ts is still monotonic */
    CHECK(!elden_pen_owner_packet(&f,gen)&&f.packets==4&&sink.count==5&&sink.trace[4].pressure==130);
}
static void fault_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    /* Malformed packet: decode fails -> existing fault path releases contact. */
    queue(&m,down(100),1,1);badenv(buf);queue(&m,buf,2,2);queue(&m,down(110),3,3);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1&&sink.trace[0].tip);
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenEnvelope&&f.faults==1);
    CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));   /* fault lift emitted */
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==3&&sink.trace[2].tip); /* next clean packet lands */
    /* Out-of-range pressure/tilt take the same fault path, not a clamped sample. */
    coord(buf,0);put(buf,71,4096);queue(&m,buf,4,4);
    coord(buf,0);put(buf,71,100);buf[73]=61;queue(&m,buf,5,5);   /* tilt 61 > max 60 */
    coord(buf,0);put(buf,71,100);buf[74]=0xc3;queue(&m,buf,6,6); /* tilt -61 */
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenRange&&f.faults==2);
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenRange&&f.faults==3);
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenRange&&f.faults==4);
    /* Provider transport fault: packet() error drives delivery_fault. */
    m.pk_fail=1;CHECK(elden_pen_owner_packet(&f,gen)==-17&&f.faults==5&&f.packets==6);m.pk_fail=0;
}
static void pause_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    /* Packet timestamps share the worker clock the op `now` values use. */
    queue(&m,down(100),1,1);queue(&m,down(200),7,2);queue(&m,down(100),10,3);
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1);
    /* Failed lift on pause: stays paused, resume refuses, no sample consumed. */
    sink.fail=2;unsigned oe=m.oe_calls;
    CHECK(elden_pen_owner_pause(&f,gen,2)==-99&&d.paused&&d.release_pending);
    CHECK(elden_pen_owner_resume(&f,gen,3)==EldenPenPaused&&!elden_pen_owner_released(&f));
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenPaused&&f.packets==1&&m.pk_calls==1&&m.oe_calls==oe+3);
    /* The second emit failure keeps the lift pending; tick retries through the
     * retained send until the release is delivered, then resume succeeds and
     * never replays the pre-sleep contact. */
    CHECK(elden_pen_owner_tick(&f,gen,4)==-99&&d.release_pending&&sink.count==1);
    CHECK(!elden_pen_owner_tick(&f,gen,5)&&sink.count==2&&!sink.trace[1].tip&&d.paused);
    CHECK(!elden_pen_owner_resume(&f,gen,6)&&!d.paused);
    unsigned old=sink.count;
    CHECK(!elden_pen_owner_tick(&f,gen,7)&&sink.count==old);                 /* no replay */
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.trace[old].tip&&sink.trace[old].pressure==200);
    /* Clean pause emits the lift, resumes freely, and a real lift ack gates it. */
    CHECK(!elden_pen_owner_pause(&f,gen,8)&&elden_pen_owner_released(&f));
    CHECK(!elden_pen_owner_resume(&f,gen,9)&&!d.paused&&f.resumes==2&&f.pauses==1);
    /* Stale-contact release: an aging coordinate is lifted by tick. */
    CHECK(!elden_pen_owner_packet(&f,gen)&&sink.trace[sink.count-1].pressure==100);
    old=sink.count;CHECK(!elden_pen_owner_tick(&f,gen,120)&&sink.count==old+1&&!sink.trace[old].tip);
    CHECK(elden_pen_owner_released(&f));
}
static void reentry_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    /* Callback re-entry: facade ops issued from inside provider.packet() are
     * fenced Busy — the packet itself still completes. */
    queue(&m,down(100),1,1);m.reenter_op=1;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy&&sink.count==1);
    queue(&m,down(110),2,2);m.reenter_op=2;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy);
    queue(&m,down(120),3,3);m.reenter_op=3;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy);
    queue(&m,down(130),4,4);m.reenter_op=4;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy);
    queue(&m,down(140),5,5);m.reenter_op=5;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy);
    queue(&m,down(150),6,6);m.reenter_op=6;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy&&f.bound);
    queue(&m,down(160),7,7);m.reenter_op=7;
    CHECK(!elden_pen_owner_packet(&f,gen)&&m.reenter_rc==EldenPenOwnerBusy);   /* release retry is fenced too */
    /* Stop during delivery: the provider invokes owner_stop inside packet();
     * the op drops the sample, the deferred stop still releases via send. */
    queue(&m,down(170),8,8);m.stop_in_packet=1;m.stop_now=800;
    unsigned old=sink.count;
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenStopped&&f.stopped&&f.dropped==1);
    CHECK(sink.count==old+1&&!sink.trace[old].tip&&elden_pen_owner_released(&f));   /* lift emitted */
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenStopped);
    CHECK(elden_pen_owner_tick(&f,gen,9)==EldenPenStopped);
    CHECK(elden_pen_owner_pause(&f,gen,10)==EldenPenStopped);
    /* A stop inside owner_epoch unwinds the same way. */
    M m2;EldenPenDelivery d2;EldenPenOwner f2;Sink s2;
    setup(&m2,&d2,&f2,&s2);p=PROV(&m2);
    CHECK(!elden_pen_owner_bind(&f2,&p,"ap501u-nt36536-sampler",11,22,&gen));m2.gen=gen;
    queue(&m2,down(100),1,1);CHECK(!elden_pen_owner_packet(&f2,gen)&&s2.count==1);
    m2.stop_in_oe=1;m2.stop_now=801;
    CHECK(elden_pen_owner_packet(&f2,gen)==EldenPenStopped&&f2.stopped);
    CHECK(s2.count==2&&!s2.trace[1].tip&&elden_pen_owner_released(&f2));
}
static void retire_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen,gen2;EldenPenProvider p;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    queue(&m,down(100),1,1);CHECK(!elden_pen_owner_packet(&f,gen));
    /* Ordered fence: retire before stop is Disabled; stop->retire releases the
     * binding, re-arms the borrowed delivery itself, and leaves the facade
     * reusable — no caller reinit is needed or permitted in between. */
    CHECK(elden_pen_owner_retire(&f,gen)==EldenPenOwnerDisabled&&f.bound);
    CHECK(!elden_pen_owner_stop(&f,gen,2)&&f.stopped);
    CHECK(sink.count==2&&!sink.trace[1].tip);
    CHECK(!elden_pen_owner_retire(&f,gen)&&!f.bound&&!f.stopped);
    CHECK(!d.stopped&&!d.has_accepted&&!d.release_pending&&elden_pen_delivery_released(&d));
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenNoProvider);          /* old gen dead */
    CHECK(elden_pen_owner_release(&f,gen)==EldenPenOwnerStale);
    /* Rebind a qualified provider: fresh generation; old gen stays stale; the
     * seq/ts/delivery cursors all restart — ts=1 proving the retire re-arm. */
    m.ro_owner=8;m.ro_epoch=4;m.rf_owner=12;m.rf_epoch=23;m.pk_at=0;m.npkts=0;
    queue(&m,down(111),1,1);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",12,23,&gen2)&&gen2!=gen);
    m.gen=gen2;
    /* Old-generation packet/release/tick mutate nothing: the new session is
     * undisturbed and, once a new contact is down, nothing lifts it. */
    CHECK(elden_pen_owner_packet(&f,gen)==EldenPenOwnerStale&&sink.count==2);
    CHECK(!elden_pen_owner_packet(&f,gen2)&&sink.count==3&&sink.trace[2].pressure==111);
    CHECK(elden_pen_owner_release(&f,gen)==EldenPenOwnerStale&&sink.count==3&&!elden_pen_owner_released(&f));
    CHECK(elden_pen_owner_tick(&f,gen,3)==EldenPenOwnerStale&&sink.count==3&&!elden_pen_owner_released(&f));
    CHECK(f.owner==8&&f.frame_owner==12);
    /* Failed stop lift: release stays pending, retire refuses, packets stay
     * terminal — but the oe-free release retry completes the lift through the
     * bound send, then retire is confirmed. */
    sink.fail=1;
    CHECK(elden_pen_owner_stop(&f,gen2,7)==-99&&f.stopped&&d.release_pending&&!elden_pen_owner_released(&f));
    CHECK(elden_pen_owner_retire(&f,gen2)==EldenPenOwnerDisabled&&f.bound);   /* lift still owed */
    CHECK(elden_pen_owner_packet(&f,gen2)==EldenPenStopped);                  /* packets terminal */
    unsigned oe0=m.oe_calls,pk0=m.pk_calls;
    CHECK(!elden_pen_owner_release(&f,gen2)&&m.oe_calls==oe0&&m.pk_calls==pk0); /* no provider callback */
    CHECK(sink.count==4&&!sink.trace[3].tip&&elden_pen_owner_released(&f));
    CHECK(!elden_pen_owner_release(&f,gen2));                                 /* idempotent once clean */
    CHECK(!elden_pen_owner_retire(&f,gen2)&&!f.bound&&!f.stopped);
    CHECK(elden_pen_owner_release(&f,gen2)==EldenPenOwnerStale);              /* gen2 dead too */
}
/* pen-005: drift first observed by any non-packet op must lift a held contact
 * through the same neutral release — one emit, no further provider I/O. */
static void guard_release_cases(void){
    /* tick / pause / resume / fault each as the first drift observation. */
    for(int op=1;op<=4;++op){
        M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
        setup(&m,&d,&f,&sink);p=PROV(&m);
        CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
        queue(&m,down(100),1,1);
        CHECK(!elden_pen_owner_packet(&f,gen)&&sink.count==1&&sink.trace[0].tip);
        m.ro_owner=99;unsigned pk=m.pk_calls,fo=m.fo_calls;
        int rc= op==1?elden_pen_owner_tick(&f,gen,2)
               :op==2?elden_pen_owner_pause(&f,gen,2)
               :op==3?elden_pen_owner_resume(&f,gen,2)
               :elden_pen_owner_fault(&f,gen,2);
        CHECK(rc==EldenPenOwnerStale&&m.pk_calls==pk&&m.fo_calls==fo);
        CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));
    }
    /* The same drift on a dead generation is a pure refusal — no lift send. */
    {
        M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
        setup(&m,&d,&f,&sink);p=PROV(&m);
        CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
        queue(&m,down(100),1,1);CHECK(!elden_pen_owner_packet(&f,gen));
        CHECK(elden_pen_owner_tick(&f,gen+9,2)==EldenPenOwnerStale&&sink.count==1&&!elden_pen_owner_released(&f));
    }
    /* Failed lift inside the guard release: op surfaces the send error, lift
     * stays pending, and the provider-free retry completes it. */
    {
        M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
        setup(&m,&d,&f,&sink);p=PROV(&m);
        CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
        queue(&m,down(100),1,1);CHECK(!elden_pen_owner_packet(&f,gen));
        m.ro_epoch=99;sink.fail=1;
        CHECK(elden_pen_owner_tick(&f,gen,2)==-99&&d.release_pending&&!elden_pen_owner_released(&f));
        unsigned oe=m.oe_calls,pk=m.pk_calls;
        CHECK(!elden_pen_owner_release(&f,gen)&&m.oe_calls==oe&&m.pk_calls==pk);
        CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));
    }
    /* Stop landing inside the guard release send: deferred, unwinds stopped,
     * and later packet entry is terminal. */
    {
        M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
        setup(&m,&d,&f,&sink);p=PROV(&m);
        CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));
        m.gen=gen;sink.gen=gen;
        queue(&m,down(100),1,1);CHECK(!elden_pen_owner_packet(&f,gen));
        sink.stop_in_send=1;          /* fires inside the drift lift send */
        m.ro_owner=99;
        CHECK(elden_pen_owner_tick(&f,gen,2)==EldenPenOwnerStale&&f.stopped);
        CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));
        CHECK(elden_pen_owner_packet(&f,gen)==EldenPenStopped);
    }
    /* Reentry inside owner_epoch on a non-packet path: fenced Busy while the
     * outer op still observes the drift and lifts. */
    {
        M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;
        setup(&m,&d,&f,&sink);p=PROV(&m);
        CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
        queue(&m,down(100),1,1);CHECK(!elden_pen_owner_packet(&f,gen));
        m.ro_owner=99;m.oe_reenter_op=2;
        CHECK(elden_pen_owner_tick(&f,gen,2)==EldenPenOwnerStale&&m.oe_reenter_rc==EldenPenOwnerBusy);
        CHECK(sink.count==2&&!sink.trace[1].tip&&elden_pen_owner_released(&f));
    }
}
static void status_cases(void){
    M m;EldenPenDelivery d;EldenPenOwner f;Sink sink;uint64_t gen;EldenPenProvider p;EldenPenOwnerStatus st;
    setup(&m,&d,&f,&sink);p=PROV(&m);
    CHECK(!elden_pen_owner_bind(&f,&p,"ap501u-nt36536-sampler",11,22,&gen));m.gen=gen;
    queue(&m,down(100),1,1);queue(&m,down(150),2,2);meta(buf);queue(&m,buf,3,3);leave(buf);queue(&m,buf,4,4);
    CHECK(!elden_pen_owner_packet(&f,gen)&&!elden_pen_owner_packet(&f,gen));
    CHECK(!elden_pen_owner_packet(&f,gen)&&!elden_pen_owner_packet(&f,gen));
    CHECK(!elden_pen_owner_status(&f,&st));
    CHECK(st.bound&&!st.stopped&&st.gen==gen&&st.owner==7&&st.epoch==3&&st.frame_owner==11&&st.frame_epoch==22);
    CHECK(st.packets==4&&st.decoded==4&&st.metadata==1&&st.last_ts==4&&st.last_seq==4);
    /* transport_delivered is what the bound send() accepted; the pen/Pencil
     * acceptance flag is permanently 0 at this layer — separate downstream
     * claim. */
    CHECK(st.transport_delivered==d.sent&&d.sent==3&&st.transport_failures==0);
    CHECK(st.pencil_accepted==0&&st.delivery_released);
    sink.fail=1;queue(&m,down(100),5,5);
    CHECK(elden_pen_owner_packet(&f,gen)==-99);
    /* The failed emit leaves accepted==leave (nothing new owed a lift), so
     * released stays true while transport_failures records the refusal. */
    CHECK(!elden_pen_owner_status(&f,&st)&&st.transport_failures==1&&st.delivery_released);
}
int main(void){
    bind_cases();packet_cases();drift_cases();order_cases();fault_cases();
    pause_cases();reentry_cases();retire_cases();guard_release_cases();status_cases();
    printf("PASS: %u pen owner-facade checks; named provider, owner/epoch+frame-owner fences, monotonic ts/seq, refusal lifts held contact, oe-free release retry, confirmed retire/rearm, no Pencil acceptance claim\n",checks);
    return 0;
}
