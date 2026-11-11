/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Register contract: h0cheung AW99706 driver, validated against the original
 * TB322FC I2C trace (address76, chip ID07, brightness04/05, enable0d bit7).
 * Byte-pair ordering follows the original driver. Other bits are retained.
 */
#include "Aw99706.h"
#include <string.h>
static int fail(EldenAw99706 *s,int error){s->ready=false;s->failed=true;s->error=error;return error;}
static int read(EldenAw99706 *s,uint8_t reg,uint8_t *value){
    if(reg!=0&&reg!=4&&reg!=5&&reg!=7&&reg!=0x0d&&reg!=0x10&&reg!=0x11&&reg!=0x12&&reg!=0x13)return fail(s,EldenBlArgument);
    return s->io.read(s->io.context,reg,value)?fail(s,EldenBlIo):0;
}
static int write(EldenAw99706 *s,uint8_t reg,uint8_t value){
    if(reg!=4&&reg!=5&&reg!=0x0d)return fail(s,EldenBlArgument);
    if(s->io.write(s->io.context,reg,value))return fail(s,EldenBlIo);
    ++s->writes;return 0;
}
int elden_aw99706_inspect(EldenAw99706 *s,const EldenBacklightIo *io){
    if(!s||!io||!io->read||!io->write)return EldenBlArgument;
    memset(s,0,sizeof(*s));s->io=*io;
    uint8_t id,mode,hi,lo,config,open,shorted,flags,cfg7;
    if(read(s,0x11,&id))return s->error;
    s->observed_id=id;
    if(id!=7)return fail(s,EldenBlIdentity);
    if(read(s,0,&mode)||read(s,4,&hi)||read(s,5,&lo)||read(s,0x0d,&config)||read(s,0x12,&open)||read(s,0x13,&shorted))return s->error;
    if(read(s,0x10,&flags))return s->error;
    if(read(s,7,&cfg7))return s->error;
    s->observed_mode=mode;s->observed_hi=hi;s->observed_lo=lo;s->observed_config=config;
    s->observed_open=open;s->observed_short=shorted;s->observed_flags=flags;
    s->observed_cfg7=cfg7;
    /* This SKU enables LED1..4 (AW99706 V1.2 p34); preserve that exact setup.
     * A reset/off chip with zero configuration requires the board init path. */
    if((mode&3)!=1||(config&0x7f)!=0x78||(cfg7&3)!=1)return fail(s,EldenBlConfiguration);
    if(open||shorted||flags)return fail(s,EldenBlFault);
    s->cfg4_reserved=hi&0xf0;s->led_config=config&0x7f;s->enabled=(config&0x80)!=0;
    s->level=s->enabled?(uint16_t)(((hi&15)<<8)|lo):0;s->ready=true;return 0;
}
int elden_aw99706_check_health(EldenAw99706 *s){
    if(!s||!s->ready||s->failed)return EldenBlConfiguration;
    uint8_t open,shorted,flags,cfg7,hi,lo,config;
    if(read(s,0x12,&open)||read(s,0x13,&shorted)||read(s,0x10,&flags))return s->error;
    s->observed_open=open;s->observed_short=shorted;s->observed_flags=flags;
    if(open||shorted||flags)return fail(s,EldenBlFault);
    if(read(s,7,&cfg7)||read(s,4,&hi)||read(s,5,&lo)||read(s,0x0d,&config))return s->error;
    if(cfg7!=s->observed_cfg7||hi!=(s->cfg4_reserved|(s->level>>8))||lo!=(s->level&255)||
       config!=(s->led_config|(s->enabled?0x80:0)))return fail(s,EldenBlVerify);
    return 0;
}
int elden_aw99706_set_level(EldenAw99706 *s,unsigned level){
    if(!s||level>4095)return EldenBlArgument;
    if(!s->ready||s->failed)return EldenBlConfiguration;
    if(level==s->level)return 0;
    if(elden_aw99706_check_health(s))return s->error;
    uint8_t before;
    if(read(s,0x0d,&before))return s->error;
    if((before&0x7f)!=s->led_config)return fail(s,EldenBlConfiguration);
    if(write(s,4,(uint8_t)(s->cfg4_reserved|(level>>8)))||write(s,5,(uint8_t)level))return s->error;
    uint8_t wanted=(uint8_t)(s->led_config|(level?0x80:0));
    if(before!=wanted&&write(s,0x0d,wanted))return s->error;
    uint8_t hi,lo,config;
    if(read(s,4,&hi)||read(s,5,&lo)||read(s,0x0d,&config))return s->error;
    if(hi!=(uint8_t)(s->cfg4_reserved|(level>>8))||lo!=(uint8_t)level||config!=wanted)return fail(s,EldenBlVerify);
    s->level=(uint16_t)level;s->enabled=level!=0;return 0;
}
