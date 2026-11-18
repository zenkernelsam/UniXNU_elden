/* SPDX-License-Identifier: GPL-2.0-only */
static uint16_t aw_u16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
int elden_aw_image(const uint8_t *data,size_t bytes,EldenAwImage *out){
    if(!data||!out||bytes<9||bytes>4096)return EldenAwArgument;
    uint16_t sum=0;for(size_t i=2;i<bytes;++i)sum=(uint16_t)(sum+data[i]);
    unsigned base=aw_u16(data+2),first=aw_u16(data+5),end=base+(unsigned)bytes-4;
    if(sum!=aw_u16(data)||base<0x800||end>0x1000||first<=base||(first-base-1)%4||first>=end)return EldenAwIntegrity;
    unsigned count=(first-base-1)/4;if(!count||count>127)return EldenAwIntegrity;
    unsigned previous=first;
    for(unsigned i=0;i<count;++i){
        unsigned at=5+i*4,start=aw_u16(data+at),stop=aw_u16(data+at+2);
        if(start<first||start<previous||stop<start||stop>=end)return EldenAwIntegrity;
        previous=stop+1;
    }
    EldenAwImage value={(uint16_t)base,(uint16_t)first,(uint16_t)end,(uint8_t)count};*out=value;return 0;
}
int elden_aw_bind(EldenAwChip *out,const EldenAwIo *io,uint8_t address){
    if(!out||!io||!io->read||!io->write||!io->admit||!io->firmware||!io->now_us||!io->delay_us||
       (address!=0x5a&&address!=0x5b))return EldenAwArgument;
    EldenAwChip value={0};value.io=*io;value.address=address;*out=value;return 0;
}
int elden_aw_identity(EldenAwChip *c,uint16_t *out){
    if(!c||!out||!c->io.read)return EldenAwArgument;
    int rc=c->io.admit(c->io.context,c->address,EldenAwInspect);if(rc)return rc;
    struct aw86927_data h={c,0,AW86937,1};c->inspecting=1;
    rc=aw86927_detect(&h);c->inspecting=0;if(rc)return rc;
    if(h.model!=AW86937)return EldenAwNoDevice;*out=0x9370;return 0;
}
int elden_aw_stop(EldenAwChip *c){
    if(!c||!c->io.admit)return EldenAwArgument;
    c->cleanup=1;c->sticky_error=0;
    c->starting=0;
    struct aw86927_data h={c,0,AW86937,c->active_wave?c->active_wave:1};
    int rc=aw86927_stop(&h);++c->stops;c->cleanup=0;
    if(rc){c->held=1;c->ready=0;return rc;}
    c->busy=0;c->active_wave=0;c->active_gain=0;return 0;
}
int elden_aw_initialize(EldenAwChip *c,const uint8_t *data,size_t bytes,uint8_t max_gain){
    if(!c||!c->io.admit||c->held||c->ready||c->busy||max_gain>128)return EldenAwArgument;
    EldenAwImage image;int rc=elden_aw_image(data,bytes,&image);if(rc)return rc;
    rc=c->io.firmware(c->io.context,data,bytes);if(rc)return rc;
    uint16_t id;rc=elden_aw_identity(c,&id);if(rc)return rc;
    c->image=data;c->image_bytes=bytes;c->base=image.base;c->first=image.first;c->end=image.end;c->waves=image.waves;
    c->max_gain=max_gain;c->sticky_error=0;
    struct aw86927_data h={c,0,AW86937,1};
    /* Do not reset an unowned chip or touch another motor. Zero gain first;
     * the upstream STOP-mode GO does not execute a waveform. */
    rc=aw_reg_write(c,0x07,0);if(rc)goto failed;
    rc=aw86927_stop(&h);if(rc)goto failed;
    rc=aw8693x_haptic_init(&h);if(rc||c->sticky_error){if(!rc)rc=c->sticky_error;goto failed;}
    rc=aw86927_ram_load(&h,"verified-native-input");if(rc||c->sticky_error){if(!rc)rc=c->sticky_error;goto failed;}
    c->ready=1;return 0;
failed:
    c->held=1;c->ready=0;(void)elden_aw_stop(c);return rc;
}
int elden_aw_ram_start(EldenAwChip *c,uint8_t wave,uint8_t gain,uint32_t duration){
    if(!c||!c->ready||c->held||c->busy||!gain||gain>c->max_gain||!wave||wave>c->waves||!duration||duration>250000)return EldenAwArgument;
    unsigned at=5+(wave-1)*4,first=aw_u16(c->image+at),end=aw_u16(c->image+at+2);
    unsigned samples=end-first+1;
    if(duration>(samples*1000000u+23999u)/24000u)return EldenAwArgument;
    /* Exact Linux arithmetic for level -> gain: ceil(gain*65535/128) ensures
     * the proven playback function writes the requested, capped register gain. */
    uint16_t level=(uint16_t)(((uint32_t)gain*65535u+127u)/128u);
    struct aw86927_data h={c,0,AW86937,wave};c->sticky_error=0;
    uint64_t now=c->io.now_us(c->io.context);if(now>UINT64_MAX-duration)return EldenAwArgument;
    if(now>UINT64_MAX-250000)return EldenAwArgument;
    c->requested_duration=duration;c->setup_deadline=now+250000;c->last_clock=now;c->starting=1;
    int rc=aw86927_play_sine(&h,level);
    c->starting=0;
    if(rc){c->held=1;c->ready=0;(void)elden_aw_stop(c);return rc;}
    c->busy=1;c->active_wave=wave;c->active_gain=gain;++c->starts;return 0;
}
int elden_aw_poll(EldenAwChip *c){
    if(!c||!c->io.admit)return EldenAwArgument;
    if(c->held)return EldenAwCancelled;if(!c->busy)return 0;
    unsigned status;int rc=aw_reg_read(c,0x02,&status);if(rc)goto failed;
    if(status&0xe6u){rc=EldenAwCancelled;goto failed;}
    if(status&1u){unsigned state;rc=aw_reg_read(c,0x3f,&state);if(rc)goto failed;
        if(!(state&15)){c->busy=0;c->active_wave=0;c->active_gain=0;++c->completions;return 1;}}
    uint64_t now=c->io.now_us(c->io.context);
    if(now<c->last_clock){rc=EldenAwTimeout;goto failed;}c->last_clock=now;
    if(now>=c->deadline){rc=elden_aw_stop(c);return rc?rc:1;}
    return 0;
failed:
    c->held=1;c->ready=0;(void)elden_aw_stop(c);return rc;
}
