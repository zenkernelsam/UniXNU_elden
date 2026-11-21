/* SPDX-License-Identifier: GPL-2.0-only */
/* HOST-ONLY synthetic provider peers — no physical NVMEM cell, PMK8550 alarm
 * owner, IRQ or RTC hardware is present or modeled as evidence. Every provider
 * callback is a test double that proves the facade's fences, never that a real
 * provider works. */
#include "Owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {
    EldenRtcOwner *f;EldenRtc *rtc;
    uint32_t raw[3],tick;uint8_t low,control;unsigned full,reads,fail_at;
    uint8_t offset[4];unsigned preads,pwrites;int persist_fail,read_fail,apply_then_fail,corrupt;
    uint8_t alarm[4],alarm_control,alarm_status;unsigned writes,nlog;char wlog[16];int alarm_fail;unsigned alarm_fail_at;
    const char *pname,*aname;uint64_t powner,pepoch,aowner,aepoch;int p_oe_fail,a_oe_fail;
    int p_stop_in_write,p_stop_in_read,a_stop_in_write,p_reenter,irq_obj,other;
    unsigned poe_calls,aoe_calls,p_stop_oe_at,a_stop_oe_at;int a_retire,retire_rc;
    uint64_t ngen,agen;int reenter_rc;
} M;
static int read_rtc(void *p,uint16_t a,uint8_t *o,size_t n){
    M*m=p;if(++m->reads==m->fail_at)return -71;
    if(a==0x6146){assert(n==1);*o=m->control;}
    else if(n==1){assert(a==0x6148);*o=m->low;}
    else{assert(a==0x6148&&n==4&&m->full<3);uint32_t v=m->raw[m->full++];for(unsigned i=0;i<4;++i)o[i]=(uint8_t)(v>>(i*8));}
    return 0;
}
static const char *p_name(void *c){M*m=c;return m->pname;}
static const char *a_name(void *c){M*m=c;return m->aname;}
static int p_oe(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->poe_calls;if(m->p_stop_oe_at==m->poe_calls)elden_rtc_owner_stop(m->f,m->ngen);if(m->p_oe_fail)return -1;*o=m->powner;*e=m->pepoch;return 0;}
static int a_oe(void *c,uint64_t *o,uint64_t *e){M*m=c;++m->aoe_calls;if(m->a_stop_oe_at==m->aoe_calls)elden_rtc_owner_stop(m->f,m->agen);if(m->a_oe_fail)return -1;*o=m->aowner;*e=m->aepoch;return 0;}
static int p_read(void *c,uint8_t out[4]){
    M*m=c;++m->preads;
    if(m->p_stop_in_read){m->p_stop_in_read=0;elden_rtc_owner_stop(m->f,m->ngen);}
    if(m->p_reenter){m->p_reenter=0;m->reenter_rc=elden_rtc_owner_restore(m->f,m->ngen);}
    if(m->persist_fail||m->read_fail)return -1;memcpy(out,m->offset,4);if(m->corrupt)out[0]^=0xff;return 0;
}
static int p_write(void *c,const uint8_t d[4]){
    M*m=c;++m->pwrites;
    if(m->p_stop_in_write){m->p_stop_in_write=0;elden_rtc_owner_stop(m->f,m->ngen);}
    if(m->persist_fail){if(m->apply_then_fail)memcpy(m->offset,d,4);return -1;}memcpy(m->offset,d,4);return 0;
}
static int a_read(void *p,uint16_t a,uint8_t *o,size_t n){
    M*m=p;
    if(m->alarm_fail)return -1;
    if(a==EldenRtcAlarmData){assert(n==4);memcpy(o,m->alarm,4);}
    else if(a==EldenRtcAlarmControl){assert(n==1);o[0]=m->alarm_control;}
    else if(a==EldenRtcAlarmStatus){assert(n==1);o[0]=m->alarm_status;}
    else return -1;return 0;
}
static int a_write(void *c,uint16_t a,const uint8_t *d,size_t n){
    M*m=c;
    if(m->a_stop_in_write){m->a_stop_in_write=0;elden_rtc_owner_stop(m->f,m->agen);}
    if(m->a_retire){m->a_retire=0;m->retire_rc=elden_rtc_owner_retire(m->f,m->agen);}
    if(m->alarm_fail)return -1;
    if(a!=EldenRtcAlarmData&&a!=EldenRtcAlarmControl&&a!=EldenRtcAlarmStatus)return -1;
    if(++m->writes==m->alarm_fail_at)return -1;
    char tag=a==EldenRtcAlarmData?'D':a==EldenRtcAlarmControl?'C':'S';assert(m->nlog<sizeof(m->wlog));m->wlog[m->nlog++]=tag;
    if(tag=='D'){assert(n==4);memcpy(m->alarm,d,4);if(m->tick){m->raw[0]+=m->tick;m->raw[1]=m->raw[2]=m->raw[0];m->low=(uint8_t)m->raw[0];}}
    else{assert(n==1);if(tag=='C')m->alarm_control=d[0];else m->alarm_status=d[0];}
    return 0;
}
static void setup(M*m,EldenRtc *rtc,EldenRtcOwner *f,uint32_t raw){
    memset(m,0,sizeof(*m));memset(rtc,0,sizeof(*rtc));
    m->raw[0]=m->raw[1]=m->raw[2]=raw;m->low=(uint8_t)raw;m->control=0x80;
    m->pname="nvmem-offset";m->aname="pmk8550-alarm";m->powner=7;m->pepoch=3;m->aowner=9;m->aepoch=5;
    m->f=f;m->rtc=rtc;
    EldenRtcIo io={m,read_rtc};assert(!elden_rtc_core_init(rtc,&io));
    assert(!elden_rtc_owner_init(f,rtc));
}
static void put32(uint8_t p[4],uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static EldenRtcNvmem NVM(M*m){EldenRtcNvmem n={m,p_name,p_oe,p_read,p_write};return n;}
static EldenRtcAlarmOwner ALM(M*m){EldenRtcAlarmOwner n={m,a_name,a_oe,a_read,a_write};return n;}
int main(void){
    M m;EldenRtc rtc;EldenRtcOwner f;uint64_t gn,ga,sec=0;int en=0;
    EldenRtcNvmem nvm;EldenRtcAlarmOwner alm;EldenRtcOwnerStatus st;
    /* Missing provider and missing callbacks refuse before any core bind. */
    setup(&m,&rtc,&f,100);
    CHECK(elden_rtc_owner_bind_nvmem(&f,0,"nvmem-offset",&gn)==EldenRtcInvalid);
    nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,0,&gn)==EldenRtcInvalid);
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"",&gn)==EldenRtcInvalid);
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",0)==EldenRtcInvalid);
    nvm=NVM(&m);nvm.name=0;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);
    nvm=NVM(&m);nvm.owner_epoch=0;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);
    nvm=NVM(&m);nvm.read=0;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);
    nvm=NVM(&m);nvm.write=0;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);
    CHECK(!rtc.persistence_bound&&!m.reads);
    /* Wrong named identity is refused: the caller's name must equal the
     * provider's declared name exactly. */
    nvm=NVM(&m);m.pname="other-cell";CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);m.pname="nvmem-offset";
    /* Zero owner or a failing owner_epoch refuses. */
    m.powner=0;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);m.powner=7;
    m.p_oe_fail=1;CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcInvalid);m.p_oe_fail=0;
    CHECK(!rtc.persistence_bound);
    /* Alarm bind requires callbacks, name, owner, and a non-null IRQ path. */
    CHECK(elden_rtc_owner_bind_alarm(&f,0,"pmk8550-alarm",&m.irq_obj,&ga)==EldenRtcInvalid);
    alm=ALM(&m);CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",0,&ga)==EldenRtcInvalid);
    alm=ALM(&m);alm.name=0;CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==EldenRtcInvalid);
    alm=ALM(&m);m.aname="other-alarm";CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==EldenRtcInvalid);m.aname="pmk8550-alarm";
    m.aowner=0;CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==EldenRtcInvalid);m.aowner=9;
    CHECK(!rtc.alarm_bound);
    /* Valid binds arm distinct generations and feed the core once each. */
    nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0&&gn);
    m.ngen=gn;
    alm=ALM(&m);CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==0&&ga&&ga!=gn);
    m.agen=ga;
    CHECK(rtc.persistence_bound&&rtc.alarm_bound&&f.nvmem_gen==gn&&f.alarm_gen==ga);
    /* A second bind on a bound slot refuses (no mid-run provider swap). */
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==EldenRtcOwnerBusy);
    CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==EldenRtcOwnerBusy);
    CHECK(elden_rtc_owner_status(&f,&st)==0&&st.nvmem_bound&&st.alarm_bound&&!st.stopped);
    /* Stale/wrong generation refused before provider I/O. */
    CHECK(elden_rtc_owner_restore(&f,gn+9)==EldenRtcOwnerStale);
    CHECK(elden_rtc_owner_restore(&f,ga)==EldenRtcOwnerStale);            /* alarm gen on nvmem op */
    CHECK(elden_rtc_owner_learn(&f,0,1800000700)==EldenRtcOwnerStale);
    CHECK(elden_rtc_owner_alarm_set(&f,gn,1800001000,1)==EldenRtcOwnerStale);/* nvmem gen on alarm op */
    CHECK(elden_rtc_owner_alarm_ack(&f,gn,&m.irq_obj)==EldenRtcOwnerStale);
    /* restore publishes the validated cell offset. */
    put32(m.offset,1800000000u-100);CHECK(elden_rtc_owner_restore(&f,gn)==0);
    CHECK(rtc.offset==1800000000u-100&&rtc.has_offset&&rtc.persist_state==EldenRtcPersistVerified);
    /* learn writes the cell, verifies readback, publishes Verified. */
    m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=103;m.low=103;
    CHECK(elden_rtc_owner_learn(&f,gn,1800000600)==0&&rtc.persist_state==EldenRtcPersistVerified);
    uint8_t cell[4];put32(cell,1800000600-103);CHECK(!memcmp(m.offset,cell,4));
    /* alarm_set uses the existing donor ordering disable->data->enable. */
    m.alarm_control=0xD5;m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=103;m.low=103;
    CHECK(elden_rtc_owner_alarm_set(&f,ga,1800001000,1)==0);
    CHECK(m.nlog==3&&!memcmp(m.wlog,"CDC",3)&&rtc.alarm_enabled&&rtc.alarm_seconds==1800001000);
    CHECK(elden_rtc_owner_alarm_read(&f,ga,&sec,&en)==0&&sec==1800001000&&en);
    /* acknowledge only from the bound IRQ path: a foreign or null context and
     * an arbitrary client generation refuse before any register access. */
    unsigned w0=m.writes;
    CHECK(elden_rtc_owner_alarm_ack(&f,ga,&m.other)==EldenRtcInvalid);
    CHECK(elden_rtc_owner_alarm_ack(&f,ga,0)==EldenRtcInvalid);
    CHECK(m.writes==w0);
    m.nlog=0;CHECK(elden_rtc_owner_alarm_ack(&f,ga,&m.irq_obj)==0);
    CHECK(m.nlog==2&&!memcmp(m.wlog,"CS",2)&&rtc.alarm_pending&&!rtc.alarm_enabled);
    /* owner/epoch drift after bind is a stale refusal before provider I/O. */
    m.powner=99;CHECK(elden_rtc_owner_restore(&f,gn)==EldenRtcOwnerStale);m.powner=7;
    m.pepoch=99;CHECK(elden_rtc_owner_learn(&f,gn,1800000800)==EldenRtcOwnerStale);m.pepoch=3;
    m.aowner=99;CHECK(elden_rtc_owner_alarm_read(&f,ga,&sec,&en)==EldenRtcOwnerStale);m.aowner=9;
    /* persistence read failure preserves prior offset and marks Uncertain. */
    m.persist_fail=1;CHECK(elden_rtc_owner_restore(&f,gn)==EldenRtcPersistError);
    CHECK(rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    /* persistence write failure: RAM offset and cell preserved, Uncertain. */
    uint8_t saved[4];memcpy(saved,m.offset,4);m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=104;m.low=104;
    CHECK(elden_rtc_owner_learn(&f,gn,1800000700)==EldenRtcPersistError);
    CHECK(!memcmp(saved,m.offset,4)&&rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    m.persist_fail=0;
    /* readback-verify failure after a good write keeps Uncertain. */
    m.read_fail=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=105;m.low=105;
    CHECK(elden_rtc_owner_learn(&f,gn,1800000800)==EldenRtcPersistError);
    CHECK(rtc.persist_state==EldenRtcPersistUncertain);m.read_fail=0;
    /* alarm partial write keeps alarm_uncertain and never enabled. */
    m.alarm_control=0xD5;m.nlog=0;m.writes=0;m.alarm_fail_at=2;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=105;m.low=105;
    CHECK(elden_rtc_owner_alarm_set(&f,ga,1800004000,1)==EldenRtcAlarmError);
    CHECK(rtc.alarm_uncertain&&!rtc.alarm_enabled);
    /* a resynchronizing read clears the uncertainty. */
    m.alarm_fail_at=0;m.nlog=0;CHECK(elden_rtc_owner_alarm_read(&f,ga,&sec,&en)==0&&!rtc.alarm_uncertain);
    /* alarm acknowledge failure marks uncertain. */
    m.alarm_fail=1;CHECK(elden_rtc_owner_alarm_ack(&f,ga,&m.irq_obj)==EldenRtcAlarmError);m.alarm_fail=0;
    /* a failed provider operation retains the bound handles. */
    CHECK(f.nvmem_bound&&f.alarm_bound&&rtc.persistence_bound&&rtc.alarm_bound);
    /* callback reentry into another facade op is fenced Busy. */
    m.p_reenter=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=106;m.low=106;
    put32(m.offset,1800000000u-100);CHECK(elden_rtc_owner_restore(&f,gn)==0);
    CHECK(m.reenter_rc==EldenRtcOwnerBusy);
    /* stop during a persistence write prevents publication and retains the
     * registration; later calls refuse Stopped until the retire fence. */
    setup(&m,&rtc,&f,110);nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    m.p_stop_in_write=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=110;m.low=110;
    CHECK(elden_rtc_owner_learn(&f,gn,1800000700)==EldenRtcPersistError);
    CHECK(rtc.persist_state==EldenRtcPersistUncertain&&!rtc.has_offset&&f.stopped);
    CHECK(f.nvmem_bound);                                            /* retained */
    CHECK(elden_rtc_owner_restore(&f,gn)==EldenRtcStopped);
    CHECK(elden_rtc_owner_learn(&f,gn,1800000800)==EldenRtcStopped);
    /* retire needs a live generation and a prior stop; it releases the
     * registration explicitly. */
    CHECK(elden_rtc_owner_retire(&f,gn+9)==EldenRtcOwnerStale);
    CHECK(elden_rtc_owner_retire(&f,gn)==0&&f.nvmem_bound==0);
    /* after retire the registration is gone and the facade is reusable. */
    CHECK(elden_rtc_owner_status(&f,&st)==0&&!st.nvmem_bound&&!st.stopped);
    /* a live (unstopped) facade refuses retire before stop. */
    setup(&m,&rtc,&f,120);nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    CHECK(elden_rtc_owner_retire(&f,gn)==EldenRtcOwnerDisabled);
    CHECK(f.nvmem_bound);
    /* stop during an alarm write prevents the enable from ever programming. */
    setup(&m,&rtc,&f,130);alm=ALM(&m);nvm=NVM(&m);
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==0);m.agen=ga;
    m.full=0;CHECK(elden_rtc_owner_learn(&f,gn,1800000000)==0);        /* arm offset */
    m.a_stop_in_write=1;m.alarm_control=0xD5;m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=130;m.low=130;
    int arc=elden_rtc_owner_alarm_set(&f,ga,1800005000,1);
    CHECK(arc<0&&!rtc.alarm_enabled&&f.stopped);
    CHECK(f.alarm_bound);                                            /* retained */
    CHECK(elden_rtc_owner_alarm_set(&f,ga,1800006000,1)==EldenRtcStopped);
    CHECK(elden_rtc_owner_alarm_ack(&f,ga,&m.irq_obj)==EldenRtcStopped);
    /* rtc-004 fix 1: a stop landing inside owner_epoch inside a provider
     * bridge must not let that bridge reach the provider read/write — the
     * in-flight op returns the existing uncertainty/error and issues no I/O.
     * p_oe runs at bind, the per-op fence, and inside each bridge. */
    setup(&m,&rtc,&f,140);nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    /* bind consumes oe call #1; fence #2 passes; the write-bridge's #3 stops. */
    m.p_stop_oe_at=3;
    CHECK(elden_rtc_owner_learn(&f,gn,1800000700)==EldenRtcPersistError);
    CHECK(m.pwrites==0&&m.poe_calls==3&&rtc.persist_state==EldenRtcPersistUncertain&&!rtc.has_offset&&f.stopped);
    /* the same stop during the fence's own owner_epoch call refuses before any
     * bridge — the read path is never issued either. fence oe is call #2. */
    setup(&m,&rtc,&f,150);nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    m.p_stop_oe_at=2;
    CHECK(elden_rtc_owner_restore(&f,gn)==EldenRtcStopped);
    CHECK(m.preads==0&&m.poe_calls==2&&f.stopped);
    /* alarm: stop during the control-write's owner_epoch keeps the write from
     * ever issuing — alarm_set fails uncertain and never enables. a_oe calls:
     * bind #1, fence #2, control-read #3, control-write #4. */
    setup(&m,&rtc,&f,160);alm=ALM(&m);nvm=NVM(&m);
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==0);m.agen=ga;
    m.full=0;CHECK(elden_rtc_owner_learn(&f,gn,1800000000)==0);        /* arm offset */
    m.a_stop_oe_at=4;m.alarm_control=0xD5;m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=160;m.low=160;
    int arc2=elden_rtc_owner_alarm_set(&f,ga,1800005000,1);
    CHECK(arc2==EldenRtcAlarmError&&m.writes==0&&rtc.alarm_uncertain&&!rtc.alarm_enabled&&f.stopped);
    CHECK(f.alarm_bound);                                            /* retained */
    /* rtc-004 fix 2: retire attempted inside an active provider callback is
     * fenced Busy and the binding is retained. */
    setup(&m,&rtc,&f,170);alm=ALM(&m);nvm=NVM(&m);
    CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    CHECK(elden_rtc_owner_bind_alarm(&f,&alm,"pmk8550-alarm",&m.irq_obj,&ga)==0);m.agen=ga;
    m.full=0;CHECK(elden_rtc_owner_learn(&f,gn,1800000000)==0);
    m.a_retire=1;m.alarm_control=0xD5;m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=170;m.low=170;
    CHECK(elden_rtc_owner_alarm_set(&f,ga,1800005000,1)==0);
    CHECK(m.retire_rc==EldenRtcOwnerBusy&&f.alarm_bound);
    CHECK(rtc.alarm_enabled&&rtc.alarm_seconds==1800005000);           /* alarm still programmed */
    /* rtc-004 fix 3: retire leaves the facade reusable — a new qualified bind
     * arms a fresh generation and prior generations stay stale. */
    setup(&m,&rtc,&f,180);nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn)==0);m.ngen=gn;
    put32(m.offset,1800000000u-180);m.full=0;CHECK(elden_rtc_owner_restore(&f,gn)==0);
    CHECK(elden_rtc_owner_stop(&f,gn)==0);
    CHECK(elden_rtc_owner_retire(&f,gn)==0);
    CHECK(elden_rtc_owner_status(&f,&st)==0&&!st.nvmem_bound&&!st.stopped);
    uint64_t gn2=0;nvm=NVM(&m);CHECK(elden_rtc_owner_bind_nvmem(&f,&nvm,"nvmem-offset",&gn2)==0&&gn2&&gn2!=gn);m.ngen=gn2;
    put32(m.offset,1800000000u-180);m.full=0;CHECK(elden_rtc_owner_restore(&f,gn2)==0);
    CHECK(rtc.offset==1800000000u-180&&rtc.persist_state==EldenRtcPersistVerified);
    CHECK(elden_rtc_owner_restore(&f,gn)==EldenRtcOwnerStale);          /* prior generation stale */
    CHECK(elden_rtc_owner_stop(&f,gn)==EldenRtcOwnerStale);             /* old gen cannot fence */
    printf("PASS %d\n",checks);return 0;
}
