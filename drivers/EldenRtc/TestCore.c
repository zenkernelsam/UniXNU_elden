/* SPDX-License-Identifier: GPL-2.0-only */
#include "Core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t raw[3],tick;uint8_t low,control,alarm_control,alarm_status,offset[4],alarm[4];char wlog[16];unsigned full,reads,fail_at,writes,preads,pwrites,nlog,persist_fail,read_fail,apply_then_fail,corrupt,alarm_fail,alarm_fail_at;} Model;
static int read_rtc(void *p,uint16_t address,uint8_t *out,size_t bytes){
    Model *m=p;if(++m->reads==m->fail_at)return -71;
    if(address==0x6146){assert(bytes==1);*out=m->control;}
    else if(bytes==1){assert(address==0x6148);*out=m->low;}
    else{assert(address==0x6148&&bytes==4&&m->full<3);uint32_t value=m->raw[m->full++];for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(value>>(i*8));}
    return 0;
}
static int persist_read(void *p,uint8_t out[4]){Model *m=p;++m->preads;if(m->persist_fail||m->read_fail)return -1;memcpy(out,m->offset,4);if(m->corrupt)out[0]^=0xff;return 0;}
static int persist_write(void *p,const uint8_t data[4]){Model *m=p;++m->pwrites;if(m->persist_fail){if(m->apply_then_fail)memcpy(m->offset,data,4);return -1;}memcpy(m->offset,data,4);return 0;}
static int alarm_read(void *p,uint16_t address,uint8_t *out,size_t bytes){
    Model *m=p;if(m->alarm_fail)return -1;
    if(address==EldenRtcAlarmData){assert(bytes==4);memcpy(out,m->alarm,4);}
    else if(address==EldenRtcAlarmControl){assert(bytes==1);out[0]=m->alarm_control;}
    else if(address==EldenRtcAlarmStatus){assert(bytes==1);out[0]=m->alarm_status;}
    else return -1;
    return 0;
}
static int alarm_write(void *p,uint16_t address,const uint8_t *data,size_t bytes){
    Model *m=p;if(m->alarm_fail)return -1;
    if(address!=EldenRtcAlarmData&&address!=EldenRtcAlarmControl&&address!=EldenRtcAlarmStatus)return -1;
    if(++m->writes==m->alarm_fail_at)return -1;
    char tag=address==EldenRtcAlarmData?'D':address==EldenRtcAlarmControl?'C':'S';assert(m->nlog<sizeof(m->wlog));m->wlog[m->nlog++]=tag;
    if(tag=='D'){assert(bytes==4);memcpy(m->alarm,data,4);if(m->tick){m->raw[0]+=m->tick;m->raw[1]=m->raw[2]=m->raw[0];m->low=(uint8_t)m->raw[0];}}
    else{assert(bytes==1);if(tag=='C')m->alarm_control=data[0];else m->alarm_status=data[0];}
    return 0;
}
static void setup(Model *m,EldenRtc *r,uint32_t raw){
    memset(m,0,sizeof(*m));memset(r,0,sizeof(*r));m->raw[0]=m->raw[1]=m->raw[2]=raw;m->low=(uint8_t)raw;m->control=0x80;
    EldenRtcIo io={m,read_rtc};assert(!elden_rtc_core_init(r,&io));
}
static void put32(uint8_t p[4],uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t le32(const uint8_t p[4]){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(void){
    Model m;EldenRtc rtc;uint32_t raw;uint64_t time=123;
    EldenRtcPersistence persistence={&m,persist_read,persist_write};
    EldenRtcAlarmIo aio={&m,alarm_read,alarm_write};
    uint64_t alarm_seconds=0;int enabled=0;
    setup(&m,&rtc,1234);assert(elden_rtc_core_read(&rtc,&time)==EldenRtcNoOffset&&time==123);
    m.full=0;m.reads=0;assert(!elden_rtc_core_learn(&rtc,1800000000));
    m.full=0;m.low=211;m.raw[0]=1235;assert(!elden_rtc_core_read(&rtc,&time)&&time==1800000001);
    setup(&m,&rtc,255);m.raw[0]=0x1200ff;m.raw[1]=0x120100;m.low=0;
    assert(!elden_rtc_core_raw(&rtc,&raw)&&raw==0x120100&&m.full==2);
    for(unsigned fail=1;fail<=4;++fail){
        setup(&m,&rtc,255);m.raw[0]=255;m.raw[1]=256;m.low=0;m.fail_at=fail;raw=0xabcdef;
        assert(elden_rtc_core_raw(&rtc,&raw)==-71&&raw==0xabcdef);
    }
    setup(&m,&rtc,10);m.control=0;assert(elden_rtc_core_raw(&rtc,&raw)==EldenRtcStopped&&m.full==0);
    setup(&m,&rtc,10);assert(!elden_rtc_core_learn(&rtc,1800000000));m.full=0;m.raw[0]=9;m.low=9;time=123;
    assert(elden_rtc_core_read(&rtc,&time)==EldenRtcCounterReset&&time==123&&!rtc.has_offset);
    m.full=0;m.raw[0]=10;m.low=10;assert(elden_rtc_core_learn(&rtc,1800000000)==EldenRtcCounterReset);
    setup(&m,&rtc,1);assert(elden_rtc_core_learn(&rtc,0)==EldenRtcRange);
    assert(elden_rtc_core_learn(&rtc,UINT64_MAX)==EldenRtcRange);
    elden_rtc_core_stop(&rtc);assert(elden_rtc_core_raw(&rtc,&raw)==EldenRtcStopped);
    /* Unbound persistence stays unbound: no guessed cell, honest None state. */
    setup(&m,&rtc,100);
    assert(elden_rtc_core_persist_state(&rtc)==EldenRtcPersistNone);
    assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcNoProvider);
    assert(elden_rtc_core_learn_persistent(&rtc,1800000000)==EldenRtcNoProvider);
    assert(!elden_rtc_core_bind_persistence(&rtc,&persistence));
    assert(rtc.persist_state==EldenRtcPersistUncertain);
    assert(elden_rtc_core_bind_persistence(&rtc,&persistence)==EldenRtcInvalid);
    /* Stored offset is validated against the live counter before acceptance;
     * the counter is read before any NVMEM access. */
    put32(m.offset,1800000000u-100);assert(!elden_rtc_core_restore_offset(&rtc));
    assert(rtc.offset==1800000000u-100&&rtc.has_offset&&rtc.persist_state==EldenRtcPersistVerified);
    m.full=0;m.raw[0]=101;m.low=101;assert(!elden_rtc_core_read(&rtc,&time)&&time==1800000001);
    /* Corrupt/below-floor and overflowing cells are rejected without touching
     * the prior accepted RAM offset. */
    m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=101;m.low=101;
    put32(m.offset,0x1234);assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcRange);
    assert(rtc.offset==1800000000u-100&&rtc.has_offset&&rtc.persist_state==EldenRtcPersistUncertain);
    put32(m.offset,UINT32_MAX);assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcRange&&rtc.offset==1800000000u-100);
    m.persist_fail=1;assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcPersistError&&rtc.offset==1800000000u-100);m.persist_fail=0;
    /* Counter rollback drops the offset and the Verified claim. */
    m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=50;m.low=50;
    assert(elden_rtc_core_raw(&rtc,&raw)==EldenRtcCounterReset);
    assert(!rtc.has_offset&&rtc.persist_state==EldenRtcPersistUncertain);
    /* Volatile learn while bound marks NVM stale until a persistent write. */
    setup(&m,&rtc,102);
    assert(!elden_rtc_core_bind_persistence(&rtc,&persistence));
    m.full=0;assert(!elden_rtc_core_learn(&rtc,1800000400));
    assert(rtc.offset==1800000298&&rtc.persist_state==EldenRtcPersistUncertain);
    /* Persistent learn writes the cell, reads it back byte-exact and only then
     * publishes RAM+Verified; every failure keeps prior accepted RAM and
     * marks storage Uncertain. */
    m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=103;m.low=103;unsigned pw=m.pwrites;assert(!elden_rtc_core_learn_persistent(&rtc,1800000600));
    assert(rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistVerified&&m.pwrites==pw+1);
    uint8_t cell[4];put32(cell,1800000600-103);assert(!memcmp(m.offset,cell,4));
    pw=m.pwrites;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=103;m.low=103;assert(!elden_rtc_core_learn_persistent(&rtc,1800000600));
    assert(m.pwrites==pw);/* Verified same-offset skips NVM wear. */
    /* Write reported failure without applying: cell and RAM preserved. */
    uint8_t saved[4];memcpy(saved,m.offset,4);m.persist_fail=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=104;m.low=104;
    assert(elden_rtc_core_learn_persistent(&rtc,1800000700)==EldenRtcPersistError);
    assert(!memcmp(saved,m.offset,4)&&rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    /* Write reported failure but applied: NVM holds new bytes, RAM still the
     * prior accepted offset, state Uncertain. */
    m.apply_then_fail=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=105;m.low=105;
    assert(elden_rtc_core_learn_persistent(&rtc,1800000700)==EldenRtcPersistError);
    put32(cell,1800000700-105);assert(!memcmp(m.offset,cell,4)&&rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    m.persist_fail=0;m.apply_then_fail=0;
    /* Verify read failure after a good write: RAM preserved, Uncertain. */
    m.read_fail=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=106;m.low=106;
    assert(elden_rtc_core_learn_persistent(&rtc,1800000800)==EldenRtcPersistError);
    put32(cell,1800000800-106);assert(!memcmp(m.offset,cell,4)&&rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    /* Successful write with wrong readback data: same honest accounting. */
    m.read_fail=0;m.corrupt=1;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=107;m.low=107;
    assert(elden_rtc_core_learn_persistent(&rtc,1800000900)==EldenRtcPersistError);
    put32(cell,1800000900-107);assert(!memcmp(m.offset,cell,4)&&rtc.offset==1800000600-103&&rtc.persist_state==EldenRtcPersistUncertain);
    m.corrupt=0;
    /* Alarm bank is 0x62xx only; unbound provider and missing offset refuse. */
    setup(&m,&rtc,50);
    EldenRtcAlarmIo bad={&m,alarm_read,0};assert(elden_rtc_core_bind_alarm(&rtc,&bad)==EldenRtcInvalid);
    assert(elden_rtc_core_alarm_set(&rtc,1800000000,1)==EldenRtcNoProvider);
    assert(elden_rtc_core_alarm_ack(&rtc)==EldenRtcNoProvider);
    assert(elden_rtc_core_alarm_read(&rtc,&alarm_seconds,&enabled)==EldenRtcNoProvider);
    assert(!elden_rtc_core_bind_alarm(&rtc,&aio));
    assert(elden_rtc_core_bind_alarm(&rtc,&aio)==EldenRtcInvalid);
    assert(elden_rtc_core_alarm_set(&rtc,1800000000,1)==EldenRtcNoOffset);
    assert(elden_rtc_core_alarm_read(&rtc,&alarm_seconds,&enabled)==EldenRtcNoOffset);
    assert(!elden_rtc_core_learn(&rtc,1800000000));/* raw=50 -> offset 1799999950, now=1800000000 */
    /* Donor ordering disable->data->enable; RMW preserves unrelated bits. */
    m.alarm_control=0xD5;m.alarm_status=0xAB;m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=50;m.low=50;
    assert(!elden_rtc_core_alarm_set(&rtc,1800001000,1));
    assert(m.nlog==3&&!memcmp(m.wlog,"CDC",3)&&m.alarm_control==0xD5);
    assert(le32(m.alarm)==1050&&rtc.alarm_enabled&&rtc.alarm_seconds==1800001000&&!rtc.alarm_pending&&!rtc.alarm_uncertain);
    assert(!elden_rtc_core_alarm_read(&rtc,&alarm_seconds,&enabled)&&alarm_seconds==1800001000&&enabled);
    m.nlog=0;m.full=0;assert(!elden_rtc_core_alarm_set(&rtc,1800002000,0));
    assert(m.nlog==2&&!memcmp(m.wlog,"CD",2)&&m.alarm_control==0x55&&!rtc.alarm_enabled&&rtc.alarm_seconds==1800002000);
    /* Stale/past enabled requests are rejected before any register access. */
    m.nlog=0;m.writes=0;m.full=0;m.raw[0]=m.raw[1]=m.raw[2]=50;m.low=50;
    assert(elden_rtc_core_alarm_set(&rtc,1800000000,1)==EldenRtcRange);
    assert(elden_rtc_core_alarm_set(&rtc,1799999999,1)==EldenRtcRange);
    assert(elden_rtc_core_alarm_set(&rtc,1672531200,1)==EldenRtcRange);/* below offset */
    assert(!m.writes&&!m.nlog&&m.alarm_control==0x55&&le32(m.alarm)==1800002000-rtc.offset);
    /* A counter tick during programming can overrun the deadline: the recheck
     * before enable leaves the comparator disabled and exposes the confirmed
     * data value; a tick that stays inside the deadline still enables. */
    m.alarm_control=0x55;m.nlog=0;m.writes=0;m.full=0;m.tick=1550;
    assert(elden_rtc_core_alarm_set(&rtc,1800001000,1)==EldenRtcRange);
    assert(m.nlog==2&&!memcmp(m.wlog,"CD",2)&&m.alarm_control==0x55);
    assert(le32(m.alarm)==1050&&rtc.alarm_seconds==1800001000&&!rtc.alarm_enabled&&!rtc.alarm_uncertain);
    m.nlog=0;m.writes=0;m.full=0;m.tick=60;
    assert(!elden_rtc_core_alarm_set(&rtc,1800005000,1));
    assert(m.nlog==3&&!memcmp(m.wlog,"CDC",3)&&m.alarm_control==0xD5&&rtc.alarm_enabled&&rtc.alarm_seconds==1800005000);
    m.tick=0;
    /* Partially applied failures: disable-write may not have landed so the
     * cache stays conservative; data-write failure confirms disabled only;
     * enable-write failure confirms data+disabled. alarm_read resynchronizes. */
    m.alarm_control=0xD5;m.writes=0;m.nlog=0;m.alarm_fail_at=1;m.full=0;
    assert(elden_rtc_core_alarm_set(&rtc,1800004000,1)==EldenRtcAlarmError);
    assert(rtc.alarm_uncertain&&m.alarm_control==0xD5);
    m.alarm_fail_at=0;assert(!elden_rtc_core_alarm_read(&rtc,&alarm_seconds,&enabled)&&!rtc.alarm_uncertain);
    m.writes=0;m.alarm_fail_at=2;m.alarm_control=0xD5;m.full=0;
    assert(elden_rtc_core_alarm_set(&rtc,1800004000,1)==EldenRtcAlarmError);
    assert(rtc.alarm_uncertain&&!rtc.alarm_enabled&&m.alarm_control==0x55&&le32(m.alarm)==5050);
    m.writes=0;m.alarm_fail_at=3;m.alarm_control=0xD5;m.full=0;
    assert(elden_rtc_core_alarm_set(&rtc,1800004000,1)==EldenRtcAlarmError);
    assert(rtc.alarm_uncertain&&!rtc.alarm_enabled&&rtc.alarm_seconds==1800004000&&le32(m.alarm)==1800004000-rtc.offset);
    m.alarm_fail_at=0;
    /* Acknowledge preserves donor order disable->clear and unrelated bits. */
    m.writes=0;m.nlog=0;m.alarm_control=0xD5;m.alarm_status=0xAB;
    assert(!elden_rtc_core_alarm_ack(&rtc));
    assert(m.nlog==2&&!memcmp(m.wlog,"CS",2)&&m.alarm_control==0x55&&m.alarm_status==0xAA);
    assert(!rtc.alarm_enabled&&rtc.alarm_pending&&!rtc.alarm_uncertain);
    m.writes=0;m.alarm_fail_at=2;m.alarm_status=0xAB;
    assert(elden_rtc_core_alarm_ack(&rtc)==EldenRtcAlarmError);
    assert(rtc.alarm_uncertain&&!rtc.alarm_enabled&&m.alarm_status==0xAB);m.alarm_fail_at=0;
    m.alarm_fail=1;m.full=0;assert(elden_rtc_core_alarm_set(&rtc,1800005000,1)==EldenRtcAlarmError);m.alarm_fail=0;
    /* After stop every provider call refuses before any provider I/O, state
     * and outputs preserved. */
    setup(&m,&rtc,60);elden_rtc_core_stop(&rtc);
    assert(elden_rtc_core_bind_persistence(&rtc,&persistence)==EldenRtcStopped);
    assert(elden_rtc_core_bind_alarm(&rtc,&aio)==EldenRtcStopped);
    assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcStopped);
    assert(elden_rtc_core_learn_persistent(&rtc,1800000000)==EldenRtcStopped);
    assert(elden_rtc_core_alarm_set(&rtc,1800001000,1)==EldenRtcStopped);
    assert(elden_rtc_core_alarm_read(&rtc,&alarm_seconds,&enabled)==EldenRtcStopped);
    assert(elden_rtc_core_alarm_ack(&rtc)==EldenRtcStopped);
    assert(elden_rtc_core_learn(&rtc,1800000000)==EldenRtcStopped);
    assert(!m.reads&&!m.writes&&!m.preads&&!m.pwrites&&!rtc.persistence_bound&&!rtc.alarm_bound);
    assert(elden_rtc_core_persist_state(&rtc)==EldenRtcPersistNone);
    setup(&m,&rtc,60);
    assert(!elden_rtc_core_bind_persistence(&rtc,&persistence)&&!elden_rtc_core_bind_alarm(&rtc,&aio));
    m.full=0;assert(!elden_rtc_core_learn(&rtc,1800000000));/* offset 1799999940, now 1800000000 */
    m.full=0;m.alarm_control=0xD5;assert(!elden_rtc_core_alarm_set(&rtc,1800001000,1));
    uint64_t keep_s=77;int keep_e=9;m.reads=m.writes=m.preads=m.pwrites=0;
    elden_rtc_core_stop(&rtc);
    assert(elden_rtc_core_restore_offset(&rtc)==EldenRtcStopped);
    assert(elden_rtc_core_learn_persistent(&rtc,1800000700)==EldenRtcStopped);
    assert(elden_rtc_core_alarm_set(&rtc,1800002000,1)==EldenRtcStopped);
    assert(elden_rtc_core_alarm_read(&rtc,&keep_s,&keep_e)==EldenRtcStopped&&keep_s==77&&keep_e==9);
    assert(elden_rtc_core_alarm_ack(&rtc)==EldenRtcStopped);
    assert(!m.reads&&!m.writes&&!m.preads&&!m.pwrites);
    assert(rtc.alarm_enabled&&rtc.alarm_seconds==1800001000&&rtc.persist_state==EldenRtcPersistUncertain);
    puts("PASS: actual Gen4 RTC read/carry, output preservation, volatile+persistent offset, verified NVMEM write, validated restore, persistence states, 0x62xx alarm ordering/stale/deadline/partial failures, stop gating; PMIC reads modeled");
}
