"""Wrap exact ADC/BT SPMI transactions with one shared, bounded bus gate."""
import hashlib
def apply(name,data):
    expected={'Spmi.c':'3b3ac07454eb5480119b2f00876cd230f1c3f803aba04dfab84250a44f007fd3',
              'BtPmic.c':'a7974966e3574c3c54f030249f941978003ecc8e40598b582f9a968a2304784f',
              'WcnPowerNative.cpp':'0fda8cc32f018d07d446486a666bc5419a6bde681112c665cbcf4cde5113abfc'}
    if hashlib.sha256(data).hexdigest()!=expected[name]:raise ValueError('Unknown SPMI input '+name)
    if name=='Spmi.c':
        for n in ('read','write'):
            old=('int elden_adc_spmi_'+n+'(').encode();new=('static int transaction_'+n+'(').encode()
            if data.count(old)!=1:raise ValueError('ADC transaction entry differs')
            data=data.replace(old,new)
        data+=b'''
#include "TransactionGate.h"
int elden_adc_spmi_read(void *p,uint8_t off,uint8_t *bytes,size_t count){
    EldenAdcSpmi *s=p;if(!s||!s->io.delay_us)return EldenAdcArgument;
    int rc=elden_spmi_transaction_acquire(EldenSpmiAdcOwner,s->io.context,s->io.delay_us);
    if(rc)return rc;
    rc=transaction_read(p,off,bytes,count);
    int release=elden_spmi_transaction_release(EldenSpmiAdcOwner,rc==EldenAdcTimeout);
    return rc?rc:release;
}
int elden_adc_spmi_write(void *p,uint8_t off,const uint8_t *bytes,size_t count){
    EldenAdcSpmi *s=p;if(!s||!s->io.delay_us)return EldenAdcArgument;
    int rc=elden_spmi_transaction_acquire(EldenSpmiAdcOwner,s->io.context,s->io.delay_us);
    if(rc)return rc;
    rc=transaction_write(p,off,bytes,count);
    int release=elden_spmi_transaction_release(EldenSpmiAdcOwner,rc==EldenAdcTimeout);
    return rc?rc:release;
}
'''
    elif name=='BtPmic.c':
        old=b'int pb_spmi_read8(';assert data.count(old)==1
        data=data.replace(old,b'static int transaction_read8(')
        at=data.index(b'\nint pb_pmic_rail(')
        wrapper=b'''
#include "TransactionGate.h"
int pb_spmi_read8(const PbSpmiIo *io,int apid,uint8_t reg,uint8_t *value){
    if(!io||!io->delay_us)return PbSpmiArgument;
    int rc=elden_spmi_transaction_acquire(EldenSpmiBtOwner,io->ctx,io->delay_us);
    if(rc)return rc;
    rc=transaction_read8(io,apid,reg,value);
    int release=elden_spmi_transaction_release(EldenSpmiBtOwner,rc==PbSpmiTimeout);
    return rc?rc:release;
}
'''
        data=data[:at]+wrapper+data[at:]
    else:
        marker=b'static int bt_set(void *p,bool high){';assert data.count(marker)==1
        helper=b'''
#include "TransactionGate.h"
static int bt_write8(Context *c,uint8_t reg,uint8_t value){
    int rc=elden_spmi_transaction_acquire(EldenSpmiBtOwner,c,delay);
    if(rc)return rc;
    c->writer.write(0x10,value);c->writer.write(0,(uint32_t)reg<<4);
    rc=-110;
    for(unsigned n=0;n<1000;n++){
        uint32_t status=c->writer.read(8);
        if(status&1u){rc=(status&0xeu)?-5:0;break;}
        IODelay(1);
    }
    int release=elden_spmi_transaction_release(EldenSpmiBtOwner,rc==-110);
    return rc?rc:release;
}
'''
        data=data.replace(marker,helper+marker)
        start=data.index(b'    for(unsigned i=0;i<6;i++){',data.index(marker));end=data.index(b'    uint8_t after[8];',start)
        data=data[:start]+b'''    for(unsigned i=0;i<6;i++){
        rc=bt_write8(c,regs[i],values[i]);
        if(rc){c->spmi_uncertain=true;return rc;}
    }
'''+data[end:]
    return data
