/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Board-specific SDAM protocol, cross-checked against the original kernel's
 * 112-event SPMI trace. Bus transfer and PMIC command completion are separate. */
#include "Adc.h"
#include "EldenAdcResources.h"
#include <string.h>
static int fail(EldenAdc *a,int error,bool quarantine){a->error=error;a->failed=true;a->quarantined|=quarantine;return error;}
static int read(EldenAdc *a,uint8_t off,uint8_t *b,size_t n){
    int rc=a->io.read(a->io.context,off,b,n);return rc?fail(a,rc,a->dirty||a->inflight):0;
}
static int byte(EldenAdc *a,uint8_t off,uint8_t *v){return read(a,off,v,1);}
static int write(EldenAdc *a,uint8_t off,const uint8_t *b,size_t n){
    if(!((off==EldenAdcConfig&&n==7)||((off==EldenAdcEocClear||off==EldenAdcChannel||off==EldenAdcRequest)&&n==1)))return fail(a,EldenAdcArgument,true);
    int rc=a->io.write(a->io.context,off,b,n);return rc?fail(a,rc,true):0;
}
static int put(EldenAdc *a,uint8_t off,uint8_t value){return write(a,off,&value,1);}
static int idle(EldenAdc *a){
    uint64_t start=a->io.now_us(a->io.context);
    for(unsigned count=0;count<150;count++){
        uint8_t hs,req;
        if(byte(a,EldenAdcHs,&hs)||byte(a,EldenAdcRequest,&req))return a->error;
        if(hs==1&&req==0)return 0;
        if(a->io.now_us(a->io.context)-start>=15000)break;
        a->io.delay_us(a->io.context,100);
    }
    return fail(a,EldenAdcTimeout,a->dirty||a->inflight);
}
int elden_adc_inspect(EldenAdc *a,EldenAdcView *out){
    if(!a||!out||!a->io.read)return EldenAdcArgument;
    EldenAdcView v;memset(&v,0,sizeof(v));uint8_t raw[2];
    if(byte(a,EldenAdcHs,&v.handshake)||byte(a,EldenAdcStatus,&v.status)||byte(a,EldenAdcEoc,&v.eoc)||
       byte(a,EldenAdcRequest,&v.request)||read(a,EldenAdcConfig,v.config,7)||read(a,EldenAdcData,raw,2))return a->error;
    v.previous_code=(uint16_t)(raw[0]|(uint16_t)raw[1]<<8);*out=v;return 0;
}
int elden_adc_acquire(EldenAdc *a){
    if(!a||!a->io.read||!a->io.write||!a->io.now_us||!a->io.delay_us||a->acquired||a->dirty||a->failed||a->quarantined)return EldenAdcArgument;
    a->stage=1;
    if(idle(a)||elden_adc_inspect(a,&a->initial))return a->error;
    if((a->initial.status&0x80)||(a->initial.eoc&1))return fail(a,EldenAdcBusy,false);
    a->acquired=true;a->stage=2;return 0;
}
static int wait_result(EldenAdc *a){
    uint64_t start=a->io.now_us(a->io.context);
    for(unsigned count=0;count<502;count++){
        uint8_t status,eoc;
        if(byte(a,EldenAdcEoc,&eoc)||byte(a,EldenAdcStatus,&status))return a->error;
        if(status&0x80)return fail(a,EldenAdcFault,true);
        if(eoc&1)return 0;
        if(a->io.now_us(a->io.context)-start>=501000)break;
        a->io.delay_us(a->io.context,1000);
    }
    return fail(a,EldenAdcTimeout,true);
}
int elden_adc_sample(EldenAdc *a,unsigned port,uint16_t *raw,int *mc){
    if(!a||!raw||!mc||port<1||port>2||!a->acquired||a->failed||a->quarantined||a->inflight)return EldenAdcArgument;
    a->stage=3;uint8_t config[7],status,eoc;
    if(idle(a)||byte(a,EldenAdcStatus,&status)||byte(a,EldenAdcEoc,&eoc))return a->error;
    if((status&0x80)||(eoc&1))return fail(a,EldenAdcBusy,true);
    if(read(a,EldenAdcConfig,config,7))return a->error;
    config[0]=port==1?ELDEN_ADC_USB1_SID:ELDEN_ADC_USB2_SID;
    config[1]=0x80;config[2]=1;
    config[3]=(uint8_t)((config[3]&~0x3cu)|0x10u|((port==1?ELDEN_ADC_USB1_DECIMATION:ELDEN_ADC_USB2_DECIMATION)<<2));
    config[4]=(uint8_t)((config[4]&~7u)|0x80u);
    config[5]=ELDEN_ADC_USB_AMUX;
    config[6]=(uint8_t)((config[6]&~15u)|(port==1?ELDEN_ADC_USB1_SETTLE:ELDEN_ADC_USB2_SETTLE));
    a->dirty=true;
    if(write(a,EldenAdcConfig,config,7))return a->error;
    uint8_t seen[7];if(read(a,EldenAdcConfig,seen,7))return a->error;
    if(memcmp(config,seen,7))return fail(a,EldenAdcIoError,true);
    a->stage=4;a->inflight=true;
    if(put(a,EldenAdcRequest,1)||wait_result(a))return a->error;
    a->stage=5;uint8_t bytes[2];if(read(a,EldenAdcData,bytes,2))return a->error;
    uint16_t result=(uint16_t)(bytes[0]|(uint16_t)bytes[1]<<8);
    /* Clear only our immediate EOC, then wait for PBS to consume that clear.
     * In particular, do not clear the thermal-monitor flags at48/49/6e. */
    a->stage=6;
    if(put(a,EldenAdcEocClear,1)||put(a,EldenAdcChannel,0)||put(a,EldenAdcRequest,1)||idle(a)||byte(a,EldenAdcEoc,&eoc))return a->error;
    if(eoc&1)return fail(a,EldenAdcIoError,true);
    a->inflight=false;
    int value,rc=elden_adc_therm_scale(port,result,&value);
    if(rc)return fail(a,rc,false);
    a->raw=result;++a->samples;a->stage=7;*raw=result;*mc=value;return 0;
}
int elden_adc_release(EldenAdc *a){
    if(!a||!a->io.read||!a->io.write)return EldenAdcArgument;
    if(a->quarantined||a->inflight)return a->error?a->error:EldenAdcBusy;
    if(a->dirty){
        uint8_t eoc;if(idle(a)||byte(a,EldenAdcEoc,&eoc))return a->error;
        if(eoc&1)return fail(a,EldenAdcBusy,true);
        /* Configuration restoration is not followed by a conversion request. */
        if(write(a,EldenAdcConfig,a->initial.config,7))return a->error;
        uint8_t seen[7];if(read(a,EldenAdcConfig,seen,7))return a->error;
        if(memcmp(seen,a->initial.config,7))return fail(a,EldenAdcIoError,true);
        a->dirty=false;
    }
    a->acquired=false;return 0;
}
