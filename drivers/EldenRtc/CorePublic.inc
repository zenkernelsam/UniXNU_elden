/* SPDX-License-Identifier: GPL-2.0-only */
int elden_rtc_core_init(EldenRtc *r,const EldenRtcIo *io){
    if(!r||r->initialized||!io||!io->read)return EldenRtcInvalid;
    EldenRtc value={0};value.io=*io;value.initialized=1;*r=value;return 0;
}
int elden_rtc_core_raw(EldenRtc *r,uint32_t *out){
    if(!r||!r->initialized||!out)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    uint8_t control;int rc=rtc_read_bytes(r,0x6146,&control,1);if(rc)return rc;
    if(!(control&0x80))return EldenRtcStopped;
    const struct pm8xxx_rtc_regs regs={0x6148};struct pm8xxx_rtc original={r,&regs};uint32_t raw;
    rc=pm8xxx_rtc_read_raw(&original,&raw);if(rc)return rc;
    if(r->has_raw&&raw<r->raw){r->has_offset=0;if(r->persistence_bound)r->persist_state=EldenRtcPersistUncertain;r->reset=1;return EldenRtcCounterReset;}
    if(r->reset)return EldenRtcCounterReset;
    r->raw=raw;r->has_raw=1;*out=raw;return 0;
}
int elden_rtc_core_read(EldenRtc *r,uint64_t *out){
    if(!out)return EldenRtcInvalid;uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;
    if(!r->has_offset)return EldenRtcNoOffset;
    uint64_t seconds=(uint64_t)raw+r->offset;
    if(seconds>UINT32_MAX||seconds<UINT64_C(1672531200))return EldenRtcRange;
    *out=seconds;return 0;
}
int elden_rtc_core_learn(EldenRtc *r,uint64_t seconds){
    if(!r||!r->initialized)return EldenRtcRange;
    if(r->stopped)return EldenRtcStopped;
    if(seconds<UINT64_C(1672531200)||seconds>UINT32_MAX)return EldenRtcRange;
    uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;
    if(seconds<=raw)return EldenRtcRange;
    uint32_t offset=(uint32_t)(seconds-raw);
    if(r->persistence_bound&&(!r->has_offset||r->offset!=offset))r->persist_state=EldenRtcPersistUncertain;
    r->offset=offset;r->has_offset=1;++r->learned;return 0;
}
static uint32_t rtc_core_le32(const uint8_t p[4]){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void rtc_core_put32(uint8_t p[4],uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static int rtc_core_eq(const uint8_t a[4],const uint8_t b[4]){for(unsigned i=0;i<4;i++)if(a[i]!=b[i])return 0;return 1;}
int elden_rtc_core_bind_persistence(EldenRtc *r,const EldenRtcPersistence *p){
    if(!r||!r->initialized||r->persistence_bound||!p||!p->read||!p->write)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    r->persistence=*p;r->persistence_bound=1;r->persist_state=EldenRtcPersistUncertain;return 0;
}
int elden_rtc_core_persist_state(EldenRtc *r){
    if(!r||!r->initialized)return EldenRtcInvalid;
    return r->persist_state;
}
int elden_rtc_core_restore_offset(EldenRtc *r){
    if(!r||!r->initialized)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    if(!r->persistence_bound)return EldenRtcNoProvider;
    uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;
    uint8_t data[4];
    if(r->persistence.read(r->persistence.context,data)){r->persist_state=EldenRtcPersistUncertain;return EldenRtcPersistError;}
    uint32_t offset=rtc_core_le32(data);
    uint64_t seconds=(uint64_t)offset+raw;
    if(seconds<UINT64_C(1672531200)||seconds>UINT32_MAX){r->persist_state=EldenRtcPersistUncertain;return EldenRtcRange;}
    r->offset=offset;r->has_offset=1;r->persist_state=EldenRtcPersistVerified;return 0;
}
int elden_rtc_core_learn_persistent(EldenRtc *r,uint64_t seconds){
    if(!r||!r->initialized)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    if(!r->persistence_bound)return EldenRtcNoProvider;
    if(seconds<UINT64_C(1672531200)||seconds>UINT32_MAX)return EldenRtcRange;
    uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;if(seconds<=raw)return EldenRtcRange;
    uint32_t offset=(uint32_t)(seconds-raw);uint8_t data[4];rtc_core_put32(data,offset);
    if(r->has_offset&&r->offset==offset&&r->persist_state==EldenRtcPersistVerified)return 0;
    if(r->persistence.write(r->persistence.context,data)){r->persist_state=EldenRtcPersistUncertain;return EldenRtcPersistError;}
    uint8_t back[4];
    if(r->persistence.read(r->persistence.context,back)||!rtc_core_eq(back,data)){r->persist_state=EldenRtcPersistUncertain;return EldenRtcPersistError;}
    r->offset=offset;r->has_offset=1;r->persist_state=EldenRtcPersistVerified;++r->learned;return 0;
}
int elden_rtc_core_bind_alarm(EldenRtc *r,const EldenRtcAlarmIo *io){
    if(!r||!r->initialized||r->alarm_bound||!io||!io->read||!io->write)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    r->alarm_io=*io;r->alarm_bound=1;return 0;
}
static int rtc_alarm_control(EldenRtc *r,uint8_t value){
    uint8_t current;int rc=r->alarm_io.read(r->alarm_io.context,EldenRtcAlarmControl,&current,1);if(rc)return EldenRtcAlarmError;
    current=(uint8_t)((current&~((uint8_t)EldenRtcAlarmEnableBit))|(value&EldenRtcAlarmEnableBit));
    if(r->alarm_io.write(r->alarm_io.context,EldenRtcAlarmControl,&current,1)){r->alarm_uncertain=1;return EldenRtcAlarmError;}
    return 0;
}
int elden_rtc_core_alarm_set(EldenRtc *r,uint64_t seconds,int enabled){
    if(!r||!r->initialized)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    if(!r->alarm_bound)return EldenRtcNoProvider;
    if(!r->has_offset)return EldenRtcNoOffset;
    if(seconds<UINT64_C(1672531200)||seconds>UINT32_MAX||seconds<r->offset)return EldenRtcRange;
    uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;
    if(enabled&&seconds<=(uint64_t)raw+r->offset)return EldenRtcRange;
    uint8_t data[4];rtc_core_put32(data,(uint32_t)(seconds-r->offset));
    if((rc=rtc_alarm_control(r,0)))return rc;
    r->alarm_enabled=0;
    if(r->alarm_io.write(r->alarm_io.context,EldenRtcAlarmData,data,4)){r->alarm_uncertain=1;return EldenRtcAlarmError;}
    r->alarm_seconds=seconds;
    if(enabled){
        uint32_t again;rc=elden_rtc_core_raw(r,&again);if(rc)return rc;
        if(seconds<=(uint64_t)again+r->offset)return EldenRtcRange;
        if((rc=rtc_alarm_control(r,EldenRtcAlarmEnableBit)))return rc;
    }
    r->alarm_enabled=enabled!=0;r->alarm_pending=0;r->alarm_uncertain=0;return 0;
}
int elden_rtc_core_alarm_read(EldenRtc *r,uint64_t *seconds,int *enabled){
    if(!r||!r->initialized||!seconds||!enabled)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    if(!r->alarm_bound)return EldenRtcNoProvider;
    if(!r->has_offset)return EldenRtcNoOffset;
    uint8_t data[4],control;int rc=r->alarm_io.read(r->alarm_io.context,EldenRtcAlarmData,data,4);if(rc)return EldenRtcAlarmError;
    if(r->alarm_io.read(r->alarm_io.context,EldenRtcAlarmControl,&control,1))return EldenRtcAlarmError;
    uint64_t value=(uint64_t)rtc_core_le32(data)+r->offset;if(value>UINT32_MAX)return EldenRtcRange;
    *seconds=value;*enabled=(control&EldenRtcAlarmEnableBit)!=0;r->alarm_seconds=value;r->alarm_enabled=*enabled;r->alarm_uncertain=0;return 0;
}
int elden_rtc_core_alarm_ack(EldenRtc *r){
    if(!r||!r->initialized)return EldenRtcInvalid;
    if(r->stopped)return EldenRtcStopped;
    if(!r->alarm_bound)return EldenRtcNoProvider;
    int rc=rtc_alarm_control(r,0);if(rc)return rc;
    r->alarm_enabled=0;
    uint8_t status;rc=r->alarm_io.read(r->alarm_io.context,EldenRtcAlarmStatus,&status,1);if(rc){r->alarm_uncertain=1;return EldenRtcAlarmError;}
    status=(uint8_t)(status&~((uint8_t)EldenRtcAlarmClearBit));
    if(r->alarm_io.write(r->alarm_io.context,EldenRtcAlarmStatus,&status,1)){r->alarm_uncertain=1;return EldenRtcAlarmError;}
    r->alarm_pending=1;r->alarm_uncertain=0;return 0;
}
void elden_rtc_core_stop(EldenRtc *r){if(r&&r->initialized)r->stopped=1;}
