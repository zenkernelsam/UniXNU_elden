/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY bridge qualification: synthetic provider/send peers exercise the
 * default-off native-surface gate over EldenPenOwner. No real NT36536 worker,
 * SPI bus, AP501U, touch frame owner, HID service or Pencil acceptance is
 * present or claimed — every callback is a test double proving the fences. */
#include "NativeBridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EldenPenLimits limits={19040,30400,4095,60};
static void put(uint8_t *p,unsigned at,unsigned v){p[at]=(uint8_t)(v>>8);p[at+1]=(uint8_t)v;}
static void coord(uint8_t *p,unsigned pressure){
    memset(p,0,109);p[66]=1;put(p,67,9520);put(p,69,15200);put(p,71,pressure);
}
static void leave(uint8_t *p){memset(p,0,109);p[66]=0xff;}
typedef struct {unsigned count,fail;EldenPenFrame trace[64];} Sink;
static int send(void *p,const EldenPenFrame *f){
    Sink *s=p;if(s->fail){--s->fail;return -99;}CHECK(s->count<64);s->trace[s->count++]=*f;return 0;
}
typedef struct {
    EldenPenNativeBridge *b;uint64_t gen;
    const char *pname;uint64_t ro_owner,ro_epoch,rf_owner,rf_epoch;
    int oe_fail,fo_fail,pk_fail;
    const uint8_t *pkts[24];uint64_t pts[24],pseq[24];unsigned npkts,pk_at;
    unsigned oe_calls,fo_calls,pk_calls;
} M;
static uint8_t PK[24][109];
static void queue(M*m,const uint8_t*p,uint64_t ts,uint64_t seq){
    assert(m->npkts<24);memcpy(PK[m->npkts],p,109);
    m->pkts[m->npkts]=PK[m->npkts];m->pts[m->npkts]=ts;m->pseq[m->npkts]=seq;++m->npkts;
}
static const char *p_name(void *c){M*m=c;return m->pname;}
static int p_oe(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->oe_calls;if(m->oe_fail)return -1;*o=m->ro_owner;*e=m->ro_epoch;return 0;}
static int p_fo(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->fo_calls;if(m->fo_fail)return -1;*o=m->rf_owner;*e=m->rf_epoch;return 0;}
static int p_pk(void *c,uint8_t *out,uint64_t *ts,uint64_t *seq){
    M*m=c;++m->pk_calls;
    if(m->pk_fail)return -17;
    if(m->pk_at>=m->npkts)return -1;
    memcpy(out,m->pkts[m->pk_at],109);*ts=m->pts[m->pk_at];*seq=m->pseq[m->pk_at];++m->pk_at;return 0;
}
static EldenPenProvider PROV(M*m){EldenPenProvider p={m,p_name,p_oe,p_fo,p_pk};return p;}
static void setup(M*m,EldenPenNativeBridge *b,Sink *sink){
    memset(m,0,sizeof(*m));memset(b,0,sizeof(*b));memset(sink,0,sizeof(*sink));
    m->b=b;m->pname="ap501u-nt36536-sampler";m->ro_owner=7;m->ro_epoch=3;m->rf_owner=11;m->rf_epoch=22;
    assert(!elden_pen_bridge_init(b,&limits,send,sink));
}
static uint8_t buf[109];
static uint8_t *down(unsigned p){coord(buf,p);return buf;}
static uint8_t *lv(void){leave(buf);return buf;}
static uint64_t bindit(M*m,EldenPenNativeBridge *b){
    EldenPenProvider p=PROV(m);uint64_t gen=0;
    CHECK(!elden_pen_bridge_bind(b,&p,"ap501u-nt36536-sampler",11,22,&gen)&&gen);return gen;
}
int main(void){
    M m;EldenPenNativeBridge b;Sink sink;uint64_t gen;EldenPenOwnerStatus st;

    /* init arg refusals. */
    setup(&m,&b,&sink);
    CHECK(elden_pen_bridge_init(0,&limits,send,&sink)==EldenPenDeliveryArgument);
    CHECK(elden_pen_bridge_init(&b,0,send,&sink)==EldenPenDeliveryArgument);
    CHECK(elden_pen_bridge_init(&b,&limits,0,&sink)==EldenPenDeliveryArgument);

    /* No-provider native startup refuses outright: bound/started/pencil stay
     * false and every data-path hook is gated disabled. */
    setup(&m,&b,&sink);
    CHECK(elden_pen_bridge_start(&b,1)==EldenPenNoProvider);
    CHECK(elden_pen_bridge_started(&b)==0&&elden_pen_bridge_bound(&b)==0);
    CHECK(elden_pen_bridge_packet(&b,1)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_tick(&b,1,900)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_fault(&b,1,900)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_pause(&b,1,900)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_resume(&b,1,900)==EldenPenOwnerDisabled);
    CHECK(!elden_pen_bridge_status(&b,&st)&&st.bound==0&&st.pencil_accepted==0);

    /* Bind argument/provider refusals — no state changes on failure. */
    setup(&m,&b,&sink);EldenPenProvider p=PROV(&m);
    CHECK(elden_pen_bridge_bind(&b,0,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);
    p.name=0;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.name=p_name;
    p.owner_epoch=0;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.owner_epoch=p_oe;
    p.frame_owner=0;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.frame_owner=p_fo;
    p.packet=0;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);p.packet=p_pk;
    CHECK(elden_pen_bridge_bind(&b,&p,0,11,22,&gen)==EldenPenDeliveryArgument);
    CHECK(elden_pen_bridge_bind(&b,&p,"other-name",11,22,&gen)==EldenPenDeliveryArgument);
    CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",0,22,&gen)==EldenPenDeliveryArgument);
    m.oe_fail=1;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);m.oe_fail=0;
    m.ro_owner=0;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenDeliveryArgument);m.ro_owner=7;
    m.rf_owner=99;CHECK(elden_pen_bridge_bind(&b,&p,"ap501u-nt36536-sampler",11,22,&gen)==EldenPenOwnerForeign);m.rf_owner=11;
    CHECK(elden_pen_bridge_bound(&b)==0&&m.pk_calls==0);   /* nothing bound */

    /* Valid bind: bound surfaces, started stays gated until explicit start. */
    setup(&m,&b,&sink);gen=bindit(&m,&b);
    CHECK(elden_pen_bridge_bound(&b)==1&&elden_pen_bridge_started(&b)==0);
    CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerDisabled);   /* gate: no start */
    CHECK(elden_pen_bridge_start(&b,gen+1)==EldenPenOwnerStale);
    CHECK(elden_pen_bridge_start(&b,gen)==0&&elden_pen_bridge_started(&b)==1);

    /* A bound+started bridge pulls provider packets through decode->delivery
     * ->send: a contact frame is published through the caller's transport. */
    queue(&m,down(300),100,1);queue(&m,down(400),110,2);queue(&m,lv(),120,3);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(sink.count>=1&&m.pk_calls==1&&m.fo_calls>=1);
    CHECK(!elden_pen_bridge_status(&b,&st)&&st.packets==1&&st.decoded==1&&st.pencil_accepted==0);

    /* Wrong-generation token mutates nothing — refused at the generation
     * gate (tag names `gen`), so no provider I/O and no counters. */
    unsigned pk=m.pk_calls;
    CHECK(elden_pen_bridge_packet(&b,gen+1)==EldenPenOwnerDisabled&&m.pk_calls==pk);
    CHECK(elden_pen_bridge_stop(&b,gen+1,900)==EldenPenOwnerStale);
    CHECK(elden_pen_bridge_release(&b,gen+1)==EldenPenOwnerStale);
    CHECK(elden_pen_bridge_retire(&b,gen+1)==EldenPenOwnerStale);

    /* Second down sample + lift through the bridge. */
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(!elden_pen_bridge_status(&b,&st)&&st.transport_delivered>=1);

    /* Provider owner/epoch drift: the live generation fences with the
     * owner's release semantics — a held contact is lifted. */
    m.ro_owner=8;
    CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerStale);
    m.ro_owner=7;
    CHECK(elden_pen_bridge_stop(&b,gen,900)==0&&elden_pen_bridge_started(&b)==0);
    CHECK(!elden_pen_bridge_status(&b,&st)&&st.stopped==1);

    /* Release retry needs no provider callback; retire re-arms delivery. */
    CHECK(elden_pen_bridge_retire(&b,gen)==0&&elden_pen_bridge_bound(&b)==0);

    /* Rebind gets a fresh generation; the old token stays stale forever.
     * Before the new start the default-off gate answers Disabled; once
     * started, the same stale token fences at the owner as Stale. */
    uint64_t old=gen;gen=bindit(&m,&b);CHECK(gen!=old);
    CHECK(elden_pen_bridge_packet(&b,old)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_stop(&b,old,900)==EldenPenOwnerStale);
    CHECK(elden_pen_bridge_start(&b,gen)==0);
    CHECK(elden_pen_bridge_packet(&b,old)==EldenPenOwnerDisabled);   /* gate tag */
    m.pk_at=0;queue(&m,down(200),200,10);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(elden_pen_bridge_stop(&b,gen,900)==0);
    CHECK(elden_pen_bridge_retire(&b,gen)==0);

    /* Foreign frame owner on the live generation rejects the packet but
     * still lifts a held contact (delivery release through send). */
    setup(&m,&b,&sink);gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
    queue(&m,down(300),100,1);queue(&m,down(300),110,2);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    m.rf_owner=99;
    CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenOwnerForeign);
    CHECK(!elden_pen_bridge_status(&b,&st)&&st.foreign_frames==1);
    m.rf_owner=11;
    CHECK(elden_pen_bridge_stop(&b,gen,900)==0);
    CHECK(elden_pen_bridge_retire(&b,gen)==0);

    /* Pause/resume route the existing reversible release: pause lifts the
     * held contact, a packet while paused refuses before provider I/O, and
     * resume re-arms the same service/clock. */
    setup(&m,&b,&sink);gen=bindit(&m,&b);CHECK(!elden_pen_bridge_start(&b,gen));
    queue(&m,down(300),100,1);queue(&m,down(320),200,2);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(elden_pen_bridge_pause(&b,gen,150)==0);
    CHECK(elden_pen_bridge_packet(&b,gen)==EldenPenPaused&&m.pk_calls==1);
    CHECK(elden_pen_bridge_resume(&b,gen,151)==0);
    CHECK(elden_pen_bridge_packet(&b,gen)==0);
    CHECK(elden_pen_bridge_stop(&b,gen,300)==0);
    CHECK(elden_pen_bridge_retire(&b,gen)==0);

    /* A stopped generation refuses start; retire is still required first. */
    setup(&m,&b,&sink);gen=bindit(&m,&b);
    CHECK(elden_pen_bridge_stop(&b,gen,900)==0);
    CHECK(elden_pen_bridge_start(&b,gen)==EldenPenOwnerDisabled);
    CHECK(elden_pen_bridge_bind(&b,&(EldenPenProvider){&m,p_name,p_oe,p_fo,p_pk},"ap501u-nt36536-sampler",11,22,&gen)==EldenPenStopped);
    CHECK(elden_pen_bridge_retire(&b,gen)==0);

    /* Status is coherent and honest: counters and false acceptance flags. */
    setup(&m,&b,&sink);gen=bindit(&m,&b);
    CHECK(!elden_pen_bridge_status(&b,&st));
    CHECK(st.bound==1&&st.gen==gen&&st.owner==7&&st.frame_owner==11&&st.pencil_accepted==0);

    printf("PASS: %u native-bridge checks; default-off gate, named-provider bind through EldenPenOwner, no packets/identity synthesized\n",checks);
    return 0;
}
