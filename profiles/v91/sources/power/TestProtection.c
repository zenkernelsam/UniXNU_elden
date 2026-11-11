/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Control.h"
#include "Pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
typedef struct {uint32_t cfg,io;uint8_t reg[256];unsigned writes;int fail_at;bool tlmm_bad,tlmm_write_bad;} Machine;
static uint32_t tr(void *p,uint32_t r){Machine *m=p;CHECK(r==0||r==4);if(m->tlmm_bad)return 0xffffffff;return r?m->io:m->cfg;}
static int tw(void *p,uint32_t r,uint32_t v){Machine *m=p;CHECK(r==4);if(m->tlmm_write_bad)return -5;m->io=v;return 0;}
static int pr(void *p,uint8_t r,uint8_t *v){*v=((Machine *)p)->reg[r];return 0;}
static int pw(void *p,uint8_t r,uint8_t v){Machine *m=p;CHECK(r==0x44||r==0x40);if(++m->writes==(unsigned)m->fail_at)return -5;
    /* Preload high while still an input, before switching direction. */
    if(r==0x40&&((v&3)==1)&&m->reg[0x40]==0)CHECK(m->reg[0x44]&0x80);
    m->reg[r]=v;return 0;
}
static EldenThermalPins setup(Machine *m){
    memset(m,0,sizeof(*m));m->cfg=0x200;m->reg[4]=0x10;m->reg[5]=0x10;
    m->reg[0x40]=0;m->reg[0x41]=0;m->reg[0x42]=4;m->reg[0x45]=1;m->reg[0x46]=0x80;
    EldenThermalPins p={.io={m,tr,tw,pr,pw}};return p;
}
static void pins(void){
    Machine m;EldenThermalPins p=setup(&m);EldenThermalPinView v;CHECK(!elden_thermal_pins_probe(&p,&v));
    CHECK(v.level[0]==0&&v.level[1]==0&&v.mode==0);
    CHECK(!elden_thermal_pin_set(&p,2,0)&&!m.writes&&m.reg[0x40]==0);
    CHECK(!elden_thermal_pin_set(&p,1,1)&&(m.io&2)&&m.cfg==0x200);
    CHECK(!elden_thermal_pin_set(&p,2,1)&&m.reg[0x40]==1&&m.reg[0x44]==0x80);
    CHECK(!elden_thermal_pin_set(&p,2,0)&&m.reg[0x40]==1&&m.reg[0x44]==0);
    CHECK(m.reg[0x41]==0&&m.reg[0x42]==4&&m.reg[0x45]==1&&m.reg[0x46]==0x80);
    p=setup(&m);CHECK(!elden_thermal_pins_probe(&p,&v));m.fail_at=2;
    CHECK(elden_thermal_pin_set(&p,2,1)!=0&&p.failed[1]);unsigned writes=m.writes;
    CHECK(elden_thermal_pin_set(&p,2,0)!=0&&m.writes==writes);
    CHECK(!elden_thermal_pin_set(&p,1,1)&&(m.io&2));
    p=setup(&m);CHECK(!elden_thermal_pins_probe(&p,&v));m.tlmm_bad=true;
    CHECK(elden_thermal_pin_set(&p,1,1)!=0);
    CHECK(!elden_thermal_pin_set(&p,2,1)&&m.reg[0x44]==0x80);
    p=setup(&m);m.cfg|=4;CHECK(elden_thermal_pins_probe(&p,&v)!=0&&!m.writes);
    p=setup(&m);m.reg[0x44]=1;CHECK(elden_thermal_pins_probe(&p,&v)!=0&&!m.writes);
}
static void policy(void){
    EldenThermalControl s={0};EldenThermalAction a;
    EldenThermalInput in={.sequence=1,.connector_valid={true,true},.battery_valid=true,.connector_mc={27000,28000},.battery_centi_c=2800};
    elden_thermal_control_step(&s,&in,&a);CHECK(!a.armed&&a.level[0]==-1&&a.level[1]==-1);
    in.sequence++;elden_thermal_control_step(&s,&in,&a);CHECK(a.armed&&a.level[0]==0&&a.level[1]==0);
    in.sequence++;in.connector_mc[0]=76000;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==1&&a.level[1]==0);
    in.sequence++;in.connector_mc[0]=55000;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==-1);
    in.sequence++;in.connector_valid[1]=false;elden_thermal_control_step(&s,&in,&a);CHECK(a.fault_latched&&a.level[0]==1&&a.level[1]==1);
    in.sequence++;in.connector_valid[1]=true;in.connector_mc[0]=30000;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==1&&a.level[1]==1);
    in.sequence++;elden_thermal_control_step(&s,&in,&a);CHECK(!a.fault_latched&&a.level[0]==0&&a.level[1]==0);
    /* A replayed sample cannot reopen a faulted input. */
    elden_thermal_control_step(&s,&in,&a);CHECK(a.fault_latched&&a.level[0]==1);
    in.sequence++;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==1);
    in.sequence++;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==0);
    in.sequence++;in.battery_valid=false;elden_thermal_control_step(&s,&in,&a);CHECK(a.level[0]==1&&a.level[1]==1);
    memset(&s,0,sizeof(s));in.sequence=1;in.connector_mc[0]=80000;
    elden_thermal_control_step(&s,&in,&a);CHECK(!a.armed&&a.level[0]==1&&a.level[1]==-1);
    elden_thermal_control_step(NULL,&in,&a);CHECK(a.level[0]==-1&&a.level[1]==-1);
}
int main(void){pins();policy();printf("Thermal GPIO and closed-loop policy: %u checks passed\n",checks);}
