/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual pinned reader; v7 DONE semantics from the Gen4 Linux driver. */
#include "BtPmic.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct {
    uint32_t status[5]; unsigned count,at,commands,data_reads,delays;
} Model;
static uint32_t read_obs(void *p,uint32_t address){
    Model *m=p;
    if(address==7u*0x20u+0x18u){m->data_reads++;return 0x55;}
    CHECK(address==7u*0x20u+8u);
    unsigned i=m->at++;return m->status[i<m->count?i:m->count-1];
}
static void write_obs(void *p,uint32_t address,uint32_t value){
    Model *m=p;CHECK(address==7u*0x20u);CHECK(value==((1u<<27)|(4u<<4)));m->commands++;
}
static void delay(void *p,unsigned us){Model *m=p;CHECK(us==1);m->delays++;}
static void scenario(const uint32_t *values,unsigned count,int expected,bool data){
    Model m={0};for(unsigned i=0;i<count;i++)m.status[i]=values[i];m.count=count;
    PbSpmiIo io={0};io.ctx=&m;io.obs_read=read_obs;io.obs_write=write_obs;io.delay_us=delay;
    uint8_t output=0xa5;int rc=pb_spmi_read8(&io,7,4,&output);
    CHECK(rc==expected);CHECK(m.commands==1);CHECK(m.data_reads==(data?1u:0u));
    CHECK(output==(data?0x55:0xa5));CHECK(m.at<=1000);
}
int main(void){
    const uint32_t pending[]={4,8,2,0,1};scenario(pending,5,PbSpmiOk,true);
    const uint32_t done[]={1};scenario(done,1,PbSpmiOk,true);
    for(uint32_t error=2;error<=14;error+=2){
        const uint32_t completed[]={0,error|1u};scenario(completed,2,PbSpmiIoError,false);
        const uint32_t incomplete[]={error};scenario(incomplete,1,PbSpmiTimeout,false);
    }
    const uint32_t busy[]={0};scenario(busy,1,PbSpmiTimeout,false);
    printf("PASS 17 SPMI completion scenarios, %u assertions\n",checks);return 0;
}
