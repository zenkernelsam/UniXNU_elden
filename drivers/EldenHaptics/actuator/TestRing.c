/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    void *memory=calloc(1,ELDEN_ACTUATOR_BYTES);CHECK(memory);EldenActuatorRing kernel={0},user={0};
    CHECK(!elden_actuator_ring_init(&kernel,memory,ELDEN_ACTUATOR_BYTES,1000,1,1));
    CHECK(!elden_actuator_ring_bind(&user,memory,ELDEN_ACTUATOR_BYTES,1,1));int16_t input[1024],value=99;
    for(unsigned i=0;i<1024;i++)input[i]=(int16_t)((int)i-512);
    CHECK(!elden_actuator_ring_write(&user,100,input,1024));
    for(unsigned i=0;i<1024;i++){CHECK(!elden_actuator_ring_read(&kernel,100+i,&value));CHECK(value==input[i]);}
    CHECK(!elden_actuator_ring_write(&user,100,input,1024));
    for(unsigned i=0;i<1024;i++){CHECK(!elden_actuator_ring_read(&kernel,100+i,&value));CHECK(value==input[i]*2);}
    elden_actuator_ring_taken(&kernel,1124);CHECK(elden_actuator_ring_write(&user,100,input,1)==-116);
    CHECK(!elden_actuator_ring_write(&user,16484,input,1));CHECK(!elden_actuator_ring_read(&kernel,16484,&value)&&value==input[0]);
    value=99;CHECK(elden_actuator_ring_read(&kernel,16485,&value)==-61&&value==99);
    EldenActuatorSlot *slot=&kernel.slots[16484&16383];slot->seq|=1;CHECK(elden_actuator_ring_read(&kernel,16484,&value)==-16);slot->seq++;
    slot->sample_q15=40000;CHECK(elden_actuator_ring_read(&kernel,16484,&value)==-84);
    CHECK(elden_actuator_ring_write(&user,16484,input,1)==-84&&!(slot->seq&1));
    uint32_t rate=kernel.page->rate;kernel.page->rate=24000;CHECK(elden_actuator_ring_read(&kernel,16484,&value)==-84);kernel.page->rate=rate;
    CHECK(elden_actuator_ring_write(&user,UINT64_MAX,input,1)==-22);CHECK(elden_actuator_ring_write(&user,1124+16384,input,1)==-116);
    slot->sample_q15=100;uint64_t prior=kernel.generation;
    CHECK(!elden_actuator_ring_new_run(&kernel,16484)&&kernel.generation==prior+1);
    CHECK(elden_actuator_ring_read(&kernel,16484,&value)==-61); // old samples same absolute frame, previous run
    CHECK(elden_actuator_ring_write(&user,16484,input,1)==-116); // stale user generation
    user.generation=kernel.generation;CHECK(!elden_actuator_ring_write(&user,16484,input,1));
    CHECK(!elden_actuator_ring_read(&kernel,16484,&value)&&value==input[0]);
    CHECK(elden_actuator_frame(&kernel,1000+19200000)==48000);free(memory);
    printf("PASS: %u actual48k ring checks; absolute frames, mix, stale/drop/bounds/sequence/identity and output preservation\n",checks);return 0;
}
