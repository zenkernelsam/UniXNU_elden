// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "ChargeRead.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;++n)p[n]=(uint8_t)(v>>(n*8));}
int main(void){
    const EldenChargeQuery suspend={0x60,27};uint8_t request[24];
    const uint8_t expected[24]={0x0a,0x80,0,0,1,0,0,0,0x60,0,0,0,0,0,0,0,27,0,0,0,0,0,0,0};
    assert(elden_charge_read_request(suspend,request)==0 && !memcmp(request,expected,24));
    // None of the SET/ship/update opcodes can be reached through this codec.
    for(uint32_t opcode=0;opcode<256;++opcode){
        if(opcode==0x30 || opcode==0x32 || opcode==0x34 || opcode==0x60)continue;
        memset(request,0xa5,24);assert(elden_charge_read_request((EldenChargeQuery){opcode,0},request)==EldenChargeArgument);
        for(unsigned n=0;n<24;++n)assert(request[n]==0xa5);
    }
    uint8_t response[24];memcpy(response,expected,24);put32(response+12,27);put32(response+16,1);
    EldenChargeValue value={0x55,77};assert(elden_charge_read_reply(suspend,response,24,&value)==0 && value.raw==1 && value.signed_value==1);
    const EldenChargeValue sentinel={0x55,77};
    for(unsigned size=0;size<=25;++size){
        if(size==24)continue;value=sentinel;assert(elden_charge_read_reply(suspend,response,size,&value)==EldenChargeLength);
        assert(value.raw==sentinel.raw && value.signed_value==sentinel.signed_value);
    }
    for(unsigned offset=0;offset<=12;offset+=4){
        uint8_t changed[24];memcpy(changed,response,24);changed[offset]^=1;value=sentinel;
        assert(elden_charge_read_reply(suspend,changed,24,&value)==EldenChargeMismatch);
        assert(value.raw==sentinel.raw && value.signed_value==sentinel.signed_value);
    }
    put32(response+20,5);value=sentinel;assert(elden_charge_read_reply(suspend,response,24,&value)==EldenChargeFirmwareError && value.raw==sentinel.raw && value.signed_value==sentinel.signed_value);
    put32(response+20,0);put32(response+16,0x80000000u);assert(elden_charge_read_reply(suspend,response,24,&value)==0 && value.signed_value==INT64_C(-2147483648));
    puts("PASS: stock Lenovo GET bytes, 252 non-GET refusals, reply identity/length/error and signed-current boundaries");return 0;
}
