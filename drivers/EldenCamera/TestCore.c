/* SPDX-License-Identifier: GPL-2.0-only */
#include "Core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    EldenCameraCore core;unsigned target,resource_calls,read_calls,write_calls,drains,ready;
    unsigned denied,wrong_id,fail_resource,fail_write,drain_error,ready_error,reenter,reentered;
    unsigned rails,clock,reset,stream,last_reg,last_value,last_width,group_release;
    uint64_t us;
} Model;
static int admit(void *v,unsigned target,enum EldenCameraOperation op){Model *m=v;CHECK(target==m->target);return m->denied&&op!=EldenCameraCleanup?-13:0;}
static int resource(void *v,unsigned target,enum EldenCameraResource r,int on){
    Model *m=v;CHECK(target==m->target&&r<=EldenCameraAvdd);m->resource_calls++;
    // An error may arrive after the register action: production must retain it.
    if(r==EldenCameraReset)m->reset=(unsigned)on;
    else if(r==EldenCameraClock)m->clock=(unsigned)on;
    else if(on)m->rails|=1u<<(r-EldenCameraDovdd);else m->rails&=~(1u<<(r-EldenCameraDovdd));
    return m->resource_calls==m->fail_resource?-5:0;
}
static int read(void *v,unsigned master,uint8_t slave,uint16_t reg,unsigned width,uint32_t *out){
    Model *m=v;CHECK(master==m->target&&slave==(m->target?0x31:0x56)&&width==2);
    CHECK(reg==(m->target?0x03f0:0x0000)&&m->rails==7&&m->clock&&!m->reset);m->read_calls++;
    *out=m->wrong_id?0xffff:m->target?0x08a8:0x38ee;return 0;
}
static int write(void *v,unsigned master,uint8_t slave,uint16_t reg,unsigned width,uint32_t value){
    Model *m=v;CHECK(master==m->target&&slave==(m->target?0x31:0x56));
    CHECK(width==1||width==2);CHECK(value<=(width==1?255u:65535u));CHECK(m->clock&&m->rails==7&&!m->reset);
    m->write_calls++;m->last_reg=reg;m->last_value=value;m->last_width=width;
    if(reg==0x0100)m->stream=value?1:0;if(reg==0x0104&&!value)m->group_release++;
    if(m->reenter&&!m->reentered){m->reentered=1;CHECK(elden_camera_start(&m->core)==-16);}
    return m->write_calls==m->fail_write?-5:0;
}
static int delay(void *v,unsigned us){Model *m=v;CHECK(us>0&&us<=15000);m->us+=us;return 0;}
static int ready(void *v,unsigned target){Model *m=v;CHECK(target==m->target);m->ready++;return m->ready_error;}
static int drain(void *v,unsigned target){Model *m=v;CHECK(target==m->target);m->drains++;return m->drain_error;}
static Model *fresh(unsigned target){
    Model *m=calloc(1,sizeof(*m));CHECK(m);m->target=target;
    EldenCameraIo io={m,admit,resource,read,write,delay,ready,drain};CHECK(!elden_camera_bind(&m->core,&io,target));return m;
}
int main(void){
    unsigned start_writes[2]={0},probe_resources[2]={0};
    for(unsigned target=0;target<2;target++){
        Model *m=fresh(target);CHECK(elden_camera_terminal(&m->core));CHECK(!elden_camera_stop(&m->core)&&!m->resource_calls);
        CHECK(elden_camera_control(&m->core,1,1)==-19);
        CHECK(!elden_camera_probe(&m->core)&&m->read_calls==1&&!m->rails&&!m->clock&&m->reset);
        probe_resources[target]=m->resource_calls;
        CHECK(!elden_camera_start(&m->core)&&m->stream&&m->ready==1);start_writes[target]=m->write_calls;
        CHECK(m->last_reg==0x0100&&m->last_value==(target?1:0x100)&&m->last_width==(target?1:2));
        m->reenter=1;CHECK(!elden_camera_control(&m->core,target?1000:100,target?2048:64)&&m->reentered==1);
        EldenCameraMode mode;CHECK(!elden_camera_mode(&m->core,&mode)&&mode.expected_sensor_id==(target?0x08a8:0x38ee)&&mode.lanes==4&&mode.raw_bits==10);
        CHECK(!elden_camera_stop(&m->core)&&!m->clock&&!m->rails&&m->reset&&m->drains==1&&elden_camera_terminal(&m->core));free(m);cases++;
    }
    for(unsigned target=0;target<2;target++){
        Model *m=fresh(target);m->denied=1;CHECK(elden_camera_probe(&m->core)==-13&&!m->resource_calls&&!m->write_calls);free(m);cases++;
        m=fresh(target);m->wrong_id=1;CHECK(elden_camera_probe(&m->core)==-19&&!m->write_calls&&!m->rails&&!m->clock);free(m);cases++;
        m=fresh(target);CHECK(!elden_camera_probe(&m->core));m->ready_error=-19;CHECK(elden_camera_start(&m->core)==-19&&!m->write_calls);
        CHECK(!elden_camera_stop(&m->core)&&m->drains==1&&elden_camera_terminal(&m->core));free(m);cases++;
        for(unsigned fault=1;fault<=probe_resources[target];fault++){
            m=fresh(target);m->fail_resource=fault;CHECK(elden_camera_probe(&m->core)!=0);free(m);cases++;
        }
        // Every extracted-table/control/stream write failure is exercised.
        for(unsigned fault=1;fault<=start_writes[target];fault++){
            m=fresh(target);CHECK(!elden_camera_probe(&m->core));m->fail_write=fault;
            CHECK(elden_camera_start(&m->core)<0);CHECK(!elden_camera_terminal(&m->core));free(m);cases++;
        }
        m=fresh(target);CHECK(!elden_camera_probe(&m->core)&&!elden_camera_start(&m->core));
        m->drain_error=-110;unsigned resources=m->resource_calls;
        CHECK(elden_camera_stop(&m->core)==-110&&m->resource_calls==resources&&m->clock&&m->rails==7&&!elden_camera_terminal(&m->core));
        m->drain_error=0;CHECK(!elden_camera_stop(&m->core)&&!m->clock&&!m->rails&&m->reset&&elden_camera_terminal(&m->core));free(m);cases++;
        m=fresh(target);CHECK(!elden_camera_probe(&m->core)&&!elden_camera_start(&m->core));m->fail_write=m->write_calls+2;unsigned releases=m->group_release;
        CHECK(elden_camera_control(&m->core,target?1000:100,target?2048:64)<0&&m->group_release>releases);
        CHECK(!elden_camera_stop(&m->core));free(m);cases++;
    }
    printf("PASS: %u actual extracted Gen4 camera cases/%u checks; IDs/tables/power/control/group release/receiver drain; hardware modeled\n",cases,checks);
}
