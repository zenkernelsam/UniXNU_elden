/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual route functions; MMIO/GIC exclusive ownership are explicit peers. */
#include "Irq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {Cfg=0x110+4*109,Bank=0x10+4*3,Bit=1u<<13};
typedef struct {
    EldenWakeIrq route;uint32_t tlmm[4],pdc[0x2000/4];
    unsigned ops,fail_at,writes,masks,drains,attach_calls;int deny,drain_fail,attach_fail,masked,drop;
} Model;
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==11&&epoch==7);return ((Model *)v)->deny&&!cleanup?-13:0;}
static int read(void *v,unsigned space,unsigned reg,uint32_t *value){Model *m=v;if(++m->ops==m->fail_at)return -5;
    CHECK(space<2&&!(reg&3)&&(space?reg<0x2000:reg<16));*value=space?m->pdc[reg/4]:m->tlmm[reg/4];return 0;}
static int write(void *v,unsigned space,unsigned reg,uint32_t value){Model *m=v;if(++m->ops==m->fail_at)return -5;
    CHECK((space==0&&reg==8)||(space==1&&(reg==Cfg||reg==Bank)));m->writes++;
    if(!m->drop){if(space)m->pdc[reg/4]=value;else m->tlmm[reg/4]=value;}return 0;}
static int attach(void *v,unsigned id,int edge){Model *m=v;CHECK(id==652&&edge);m->attach_calls++;return m->attach_fail;}
static int mask(void *v,unsigned id,int on){Model *m=v;CHECK(id==652&&(on==0||on==1));m->masks++;m->masked=on;return 0;}
static int drain(void *v,unsigned id){Model *m=v;CHECK(id==652&&m->masked);m->drains++;return m->drain_fail;}
static void setup(Model *m,uint32_t version,int wake){cases++;memset(m,0,sizeof(*m));m->pdc[0x1000/4]=version;m->pdc[Cfg/4]=0x100;
    m->pdc[Bank/4]=0xdead0000&~Bit;m->tlmm[0]=3u|3u<<6;m->tlmm[2]=0x300|(wake?64:0);m->masked=1;}
static int acquire(Model *m){EldenWakeIrqIo io={m,admit,read,write,attach,mask,drain};return elden_wake_irq_acquire(&m->route,&io,11,7);}
int main(void){
    Model m;
    for(unsigned old=0;old<2;old++)for(unsigned wake=0;wake<2;wake++){
        setup(&m,old?0x30101:0x30200,wake);uint32_t initial_cfg=m.pdc[Cfg/4],initial_tlmm=m.tlmm[2],initial_bank=m.pdc[Bank/4];
        CHECK(!acquire(&m)&&m.route.state==EldenWakeRouteOwned&&m.masked&&m.attach_calls==1&&!m.writes);
        CHECK(elden_wake_irq_arm(&m.route,99,7)==-116&&!m.writes);
        CHECK(!elden_wake_irq_arm(&m.route,11,7)&&m.route.state==EldenWakeRouteArmed&&!m.masked);
        CHECK((m.pdc[Cfg/4]&7)==2);CHECK(old?((m.pdc[Bank/4]&Bit)!=0):((m.pdc[Cfg/4]&8)!=0));
        CHECK(wake?(m.tlmm[2]&128)!=0:m.tlmm[2]==initial_tlmm);CHECK(!(m.tlmm[2]&1)); // no TLMM summary
        unsigned written=m.writes;CHECK(elden_wake_irq_arm(&m.route,11,7)==-16&&written==m.writes);
        CHECK(!elden_wake_irq_disarm(&m.route,11,7)&&m.masked&&m.route.state==EldenWakeRouteOwned);
        CHECK(!elden_wake_irq_arm(&m.route,11,7));
        m.pdc[Bank/4]^=1u<<12; // independent consumer changes a sibling while owned
        uint32_t live_bank=m.pdc[Bank/4];CHECK(!elden_wake_irq_stop(&m.route,11,7)&&m.route.state==EldenWakeRouteStopped&&m.masked);
        CHECK(m.pdc[Cfg/4]==initial_cfg&&m.tlmm[2]==initial_tlmm);
        CHECK(old?m.pdc[Bank/4]==((live_bank&~Bit)|(initial_bank&Bit)):m.pdc[Bank/4]==live_bank);
        CHECK(elden_wake_irq_arm(&m.route,11,7)==-125);
    }
    for(unsigned fault=0;fault<6;fault++){
        setup(&m,0x30200,1);
        if(fault==0)m.deny=1;if(fault==1)m.tlmm[0]|=1u<<9;if(fault==2)m.tlmm[2]|=1;
        if(fault==3)m.pdc[Cfg/4]|=8;if(fault==4)m.pdc[0x1000/4]=UINT32_MAX;if(fault==5)m.pdc[Cfg/4]=UINT32_MAX;
        CHECK(acquire(&m)<0&&!m.attach_calls&&!m.writes&&!m.route.owner);
    }
    setup(&m,0x30200,1);m.attach_fail=-16;CHECK(acquire(&m)==-16&&m.route.state==EldenWakeRouteHeld);
    setup(&m,0x30200,1);CHECK(!acquire(&m));m.pdc[0x1000/4]++;CHECK(elden_wake_irq_arm(&m.route,11,7)==-116&&m.route.state==EldenWakeRouteHeld);
    setup(&m,0x30200,1);CHECK(!acquire(&m));m.drain_fail=-110;CHECK(elden_wake_irq_arm(&m.route,11,7)==-110&&m.route.state==EldenWakeRouteHeld&&m.masked);
    setup(&m,0x30200,1);CHECK(!acquire(&m));m.drop=1;CHECK(elden_wake_irq_arm(&m.route,11,7)==-71&&m.route.state==EldenWakeRouteHeld);
    setup(&m,0x30200,1);CHECK(!acquire(&m));m.tlmm[2]^=0x100;CHECK(elden_wake_irq_arm(&m.route,11,7)==-116&&!m.writes&&m.route.state==EldenWakeRouteHeld);
    // Every read/write failure of the actual acquire/arm/stop program must
    // refuse readiness and retain any touched state; never clear another IRQ.
    setup(&m,0x30200,1);CHECK(!acquire(&m));CHECK(!elden_wake_irq_arm(&m.route,11,7));CHECK(!elden_wake_irq_stop(&m.route,11,7));unsigned operations=m.ops;
    for(unsigned fail=1;fail<=operations;fail++){
        setup(&m,0x30200,1);m.fail_at=fail;int rc=acquire(&m);
        if(!rc)rc=elden_wake_irq_arm(&m.route,11,7);if(!rc)rc=elden_wake_irq_stop(&m.route,11,7);
        CHECK(rc==-5&&m.route.state!=EldenWakeRouteStopped&&m.route.state!=EldenWakeRouteArmed);
        if(m.route.owner){unsigned before=m.writes;CHECK(elden_wake_irq_stop(&m.route,11,7)<0&&before==m.writes);}
    }
    printf("PASS: %u actual GPIO/PDC route cases/%u checks; exact652/falling/type/version/readback/ownership/faults and sibling-preserving stop; MMIO/GIC modeled\n",cases,checks);return 0;
}
