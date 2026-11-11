/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Policy.h"
int elden_usb_thermal_decision(unsigned port,bool known,int mc,bool battery_known,int bc){
    if(port<1||port>2||!known||mc<-40960||mc>130048)return EldenThermalKeep;
    int deci=mc/100;
    /* The original kernel uses DIV_ROUND_CLOSEST on centidegrees. */
    bool battery=battery_known&&bc>=-4000&&bc<=9000;
    int batt=battery?(bc+(bc<0?-5:5))/10:0;
    if(deci<450||(battery&&deci<=499&&deci-batt<=99))return EldenThermalLow;
    if(deci>(port==1?750:700)||(battery&&deci>=(port==1?651:601)&&deci-batt>=(port==1?301:201)))return EldenThermalHigh;
    return EldenThermalKeep;
}
