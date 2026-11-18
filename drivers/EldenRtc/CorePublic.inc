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
    if(r->has_raw&&raw<r->raw){r->has_offset=0;r->reset=1;return EldenRtcCounterReset;}
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
    if(!r||!r->initialized||seconds<UINT64_C(1672531200)||seconds>UINT32_MAX)return EldenRtcRange;
    uint32_t raw;int rc=elden_rtc_core_raw(r,&raw);if(rc)return rc;
    if(seconds<=raw)return EldenRtcRange;
    r->offset=(uint32_t)(seconds-raw);r->has_offset=1;++r->learned;return 0;
}
void elden_rtc_core_stop(EldenRtc *r){if(r&&r->initialized)r->stopped=1;}
