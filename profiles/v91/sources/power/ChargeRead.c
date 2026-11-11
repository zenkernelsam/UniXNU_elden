// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "ChargeRead.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;++n)p[n]=(uint8_t)(v>>(n*8));}
static int allowed(EldenChargeQuery q){
    switch(q.opcode){
    case ELDEN_CHARGE_BATTERY_GET:return q.property<=26;
    case ELDEN_CHARGE_USB_GET:return q.property<=11;
    case ELDEN_CHARGE_WLS_GET:return q.property<=10;
    case ELDEN_CHARGE_LENOVO_GET:
        return q.property<=9 || (q.property>=12 && q.property<=17) ||
               q.property==ELDEN_LENOVO_INPUT_SUSPEND || q.property==ELDEN_LENOVO_CHARGING_ENABLE ||
               (q.property>=44 && q.property<=50) || q.property==ELDEN_LENOVO_PPS_STATUS ||
               q.property==ELDEN_LENOVO_SC7603_LOWPOWER;
    default:return 0;
    }
}
int elden_charge_read_request(EldenChargeQuery q,uint8_t out[24]){
    if(!out || !allowed(q))return EldenChargeArgument;
    uint8_t message[24]={0};put32(message,ELDEN_CHARGE_OWNER);put32(message+4,1);
    put32(message+8,q.opcode);put32(message+16,q.property);
    memcpy(out,message,sizeof(message));return EldenChargeOk;
}
int elden_charge_read_reply(EldenChargeQuery q,const uint8_t *data,size_t size,EldenChargeValue *value){
    if(!data || !value || !allowed(q))return EldenChargeArgument;
    if(size!=24)return EldenChargeLength;
    if(le32(data)!=ELDEN_CHARGE_OWNER || le32(data+4)!=1 ||
       le32(data+8)!=q.opcode || le32(data+12)!=q.property)return EldenChargeMismatch;
    if(le32(data+20))return EldenChargeFirmwareError;
    const uint32_t raw=le32(data+16);
    EldenChargeValue result={raw,(raw&0x80000000u)?(int64_t)raw-INT64_C(0x100000000):(int64_t)raw};
    *value=result;return EldenChargeOk;
}
