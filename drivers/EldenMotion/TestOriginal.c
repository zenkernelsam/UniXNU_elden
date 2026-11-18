/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "T970Ssc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void){
    uint32_t data[3];int32_t status;
    const uint8_t extra[]={10,16,0,0,0,0,0,0,128,63,0,0,0,0,0,0,0,0,16,3};
    const uint8_t tail[]={10,13,0,0,0,0,0,0,128,63,0,0,0,0,0,16,3};
    assert(t970_sns_sensor_event(extra,sizeof(extra),data,3,&status)==3);
    assert(t970_sns_sensor_event(tail,sizeof(tail),data,3,&status)==3);
    puts("PASS: original donor reproduces silent fourth-float and packed-tail acceptance; fixed public decoder rejects both");
}
