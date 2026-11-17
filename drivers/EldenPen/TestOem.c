/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PenReport.h"
#include "OemFixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    EldenPenLimits limits={19040,30400,4095,60};EldenPenFrame f;
    for(unsigned i=0;i<sizeof(oem_info)/sizeof(*oem_info);++i){
        uint8_t packet[109]={0};packet[66]=0xf1;memcpy(packet+67,oem_info[i].raw,9);
        assert(!elden_pen_decode(packet,sizeof(packet),&limits,&f));assert(f.kind==EldenPenInfo&&!f.tip&&!f.in_range);
        assert(f.serial==oem_info[i].serial&&f.vendor==oem_info[i].vendor&&f.product==oem_info[i].product);
        assert(!memcmp(f.info_raw,oem_info[i].raw,9));
    }
    for(unsigned i=0;i<sizeof(oem_envelope)/sizeof(*oem_envelope);++i){
        uint8_t packet[109];memcpy(packet,oem_envelope[i].packet,109);packet[66]=0xf1;
        assert(elden_pen_decode(packet,sizeof(packet),&limits,&f)==(oem_envelope[i].accepted?0:EldenPenEnvelope));
    }
    puts("PASS: production decoder matches original F1 bit packing and envelope decisions; synthetic instruction oracle, no hardware");
}
