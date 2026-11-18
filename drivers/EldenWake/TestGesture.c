/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Gesture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ELDEN_WAKE_OEM_FIXTURES
#include "OemWakeFixtures.h"
#endif
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void checksum(uint8_t *p){p[65]=0;uint8_t sum=0;for(unsigned i=1;i<65;i++)sum=(uint8_t)(sum+p[i]);p[65]=(uint8_t)(0-sum);}
int main(void){
    uint8_t p[109]={0};EldenWakeGesture out,old;
    for(unsigned field=0;field<256;field++){
        p[1]=(uint8_t)field;p[2]=0;p[3]=0;checksum(p);memset(&out,0xa5,sizeof(out));old=out;
        int r=elden_wake_gesture(p,sizeof(p),1,&out);
        CHECK(r==((field>>3)==15?1:0));
        if(r)CHECK(out.id==15&&out.linux_key==143&&out.raw_id==15);else CHECK(!memcmp(&old,&out,sizeof(out)));
        CHECK(elden_wake_gesture(p,sizeof(p),0,&out)==0);
    }
    for(unsigned f=0;f<3;f++)for(unsigned inner=0;inner<256;inner++){
        p[1]=30u<<3;p[2]=(uint8_t)f;p[3]=(uint8_t)inner;checksum(p);memset(&out,0xa5,sizeof(out));old=out;
        int r=elden_wake_gesture(p,sizeof(p),1,&out);CHECK(r==((f==1&&inner==15)?1:0));
        if(r)CHECK(out.raw_id==30&&out.id==15&&out.linux_key==143);else CHECK(!memcmp(&out,&old,sizeof(out)));
    }
    p[1]=15u<<3;p[2]=1;p[3]=15;checksum(p);
    for(unsigned i=1;i<=65;i++){p[i]^=1;out=old;CHECK(elden_wake_gesture(p,sizeof(p),1,&out)==-84&&!memcmp(&out,&old,sizeof(out)));p[i]^=1;}
    for(unsigned i=66;i<109;i++){p[i]^=1;CHECK(elden_wake_gesture(p,sizeof(p),1,&out)==1);p[i]^=1;}
    CHECK(elden_wake_gesture(p,65,1,&out)==-22);CHECK(elden_wake_gesture(p,110,1,&out)==-22);CHECK(elden_wake_gesture(p,109,2,&out)==-22);
#ifdef ELDEN_WAKE_OEM_FIXTURES
    for(unsigned i=0;i<sizeof(oem_wake)/sizeof(*oem_wake);i++){
        memset(p,0,sizeof(p));p[1]=oem_wake[i].header;p[2]=oem_wake[i].function;p[3]=oem_wake[i].inner;checksum(p);
        CHECK(elden_wake_gesture(p,sizeof(p),1,&out)==oem_wake[i].double_tap);
    }
#endif
    printf("PASS: %u actual wake gesture checks; fixed OEM double-tap, wrapper, envelope, disabled/no-key policy\n",checks);
}
