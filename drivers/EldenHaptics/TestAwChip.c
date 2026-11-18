/* SPDX-License-Identifier: GPL-2.0-only */
#include "AwChip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint8_t registers[129],ram[4096];uint16_t position;
    const uint8_t *firmware;size_t bytes;
    unsigned writes,reads,ops,fail_at,deny,corrupt,delay_fail,start_count,stop_count,readback,slow;
    uint64_t time;unsigned motor;unsigned changing[1024],values[1024];
} Chip;
static int failure(Chip *c){return ++c->ops==c->fail_at?-71:0;}
static int admit(void *p,uint8_t address,enum EldenAwOperation operation){
    Chip *c=p;CHECK(address==c->motor);CHECK(operation<=EldenAwStop);
    return c->deny&&operation!=EldenAwStop?-125:0;
}
static int read_chip(void *p,uint8_t address,uint8_t reg,uint8_t *out,size_t bytes){
    Chip *c=p;CHECK(address==c->motor&&bytes<=16);int rc=failure(c);if(rc)return rc;++c->reads;
    if(reg==0x42){CHECK(c->position+bytes<=4096);memcpy(out,c->ram+c->position,bytes);c->position+=(uint16_t)bytes;c->readback+=(unsigned)bytes;if(c->corrupt)out[0]^=1;}
    else{CHECK(reg+bytes<=129);memcpy(out,c->registers+reg,bytes);}
    return 0;
}
static int write_chip(void *p,uint8_t address,uint8_t reg,const uint8_t *data,size_t bytes){
    Chip *c=p;CHECK(address==c->motor&&bytes<=16);int rc=failure(c);if(rc)return rc;
    CHECK(c->writes<1024);c->changing[c->writes]=reg;c->values[c->writes++]=data[0];
    if(reg==0x42){CHECK(c->position+bytes<=4096);memcpy(c->ram+c->position,data,bytes);c->position+=(uint16_t)bytes;}
    else{CHECK(reg+bytes<=129);memcpy(c->registers+reg,data,bytes);}
    if(reg==0x40||reg==0x41)c->position=(uint16_t)(((uint16_t)c->registers[0x40]<<8)|c->registers[0x41]);
    if(reg==9&&data[0]==1){
        if((c->registers[8]&3)==3){c->stop_count++;c->registers[0x3f]=0;}
        else{c->start_count++;c->registers[0x3f]=1;}
    }
    if(c->slow)c->time+=100000;
    return 0;
}
static int firmware(void *p,const uint8_t *data,size_t bytes){Chip *c=p;return data==c->firmware&&bytes==c->bytes?0:-84;}
static uint64_t now(void *p){return ((Chip *)p)->time;}
static int delay(void *p,unsigned low,unsigned high){Chip *c=p;CHECK(low<=high);c->time+=low;return c->delay_fail?-71:0;}
static void setup(Chip *c,EldenAwChip *device,const uint8_t *data,size_t bytes,unsigned motor){
    memset(c,0,sizeof(*c));c->registers[0x57]=0x93;c->registers[0x58]=0x70;c->firmware=data;c->bytes=bytes;c->motor=motor;
    EldenAwIo io={c,read_chip,write_chip,admit,firmware,now,delay};CHECK(!elden_aw_bind(device,&io,(uint8_t)motor));
}
int main(int argc,char **argv){
    CHECK(argc==2);FILE *file=fopen(argv[1],"rb");CHECK(file);uint8_t data[4096];size_t size=fread(data,1,sizeof(data),file);CHECK(!ferror(file)&&!fclose(file));
    EldenAwImage image,sentinel={1,2,3,4};image=sentinel;CHECK(!elden_aw_image(data,size,&image));CHECK(image.base==0x800&&image.waves==5);
    uint8_t bad[4096];memcpy(bad,data,size);bad[100]^=1;CHECK(elden_aw_image(bad,size,&sentinel)==EldenAwIntegrity&&sentinel.base==1);
    Chip bus;EldenAwChip chip;setup(&bus,&chip,data,size,0x5a);uint16_t id=0;CHECK(!elden_aw_identity(&chip,&id)&&id==0x9370&&bus.writes==0);
    CHECK(!elden_aw_initialize(&chip,data,size,64));CHECK(chip.ready&&!chip.busy&&bus.start_count==0&&bus.readback==size-4);
    CHECK(!memcmp(bus.ram+0x800,data+4,size-4)&&!(bus.registers[0x45]&4));
    CHECK(bus.registers[0x06]%32==9&&bus.registers[0x5e]==0);
    unsigned initial_ops=bus.ops;CHECK(!elden_aw_ram_start(&chip,1,64,10000));CHECK(bus.registers[0x07]==64&&bus.registers[0x12]>>4==0);
    CHECK(chip.busy&&bus.start_count==1&&elden_aw_ram_start(&chip,1,64,10000)==EldenAwArgument);
    CHECK(elden_aw_poll(&chip)==0);bus.time=chip.deadline;CHECK(elden_aw_poll(&chip)==1&&!chip.busy);
    CHECK(!elden_aw_ram_start(&chip,1,1,1000));bus.registers[2]=1;bus.registers[0x3f]=0;CHECK(elden_aw_poll(&chip)==1&&!chip.busy&&chip.completions==1);
    CHECK(elden_aw_ram_start(&chip,1,65,1000)==EldenAwArgument);CHECK(elden_aw_ram_start(&chip,0,1,1000)==EldenAwArgument);
    CHECK(!elden_aw_ram_start(&chip,1,1,1000));bus.registers[2]=0x04;CHECK(elden_aw_poll(&chip)==EldenAwCancelled&&chip.held&&!chip.ready);
    /* Every upstream configuration/readback transaction can fail; ready never
     * becomes true after an error. Cleanup always uses the original stop mode. */
    for(unsigned at=1;at<=initial_ops;++at){
        setup(&bus,&chip,data,size,0x5b);bus.fail_at=at;int rc=elden_aw_initialize(&chip,data,size,64);
        CHECK(rc!=0&&!chip.ready&&bus.start_count==0);
    }
    setup(&bus,&chip,data,size,0x5a);bus.corrupt=1;CHECK(elden_aw_initialize(&chip,data,size,64)==EldenAwIntegrity&&!chip.ready);
    setup(&bus,&chip,data,size,0x5a);bus.delay_fail=1;CHECK(elden_aw_initialize(&chip,data,size,64)!=0&&!chip.ready);
    setup(&bus,&chip,data,size,0x5a);CHECK(!elden_aw_initialize(&chip,data,size,0));CHECK(elden_aw_ram_start(&chip,1,1,1000)==EldenAwArgument);
    setup(&bus,&chip,data,size,0x5a);bus.registers[0x58]=0x80;CHECK(elden_aw_initialize(&chip,data,size,64)==EldenAwNoDevice&&bus.writes==0);
    setup(&bus,&chip,data,size,0x5a);CHECK(!elden_aw_initialize(&chip,data,size,64));bus.slow=1;
    CHECK(elden_aw_ram_start(&chip,1,64,10000)!=0&&bus.start_count==0&&!chip.ready);
    setup(&bus,&chip,data,size,0x5a);CHECK(!elden_aw_initialize(&chip,data,size,64));CHECK(!elden_aw_ram_start(&chip,1,64,10000));
    bus.time=0;CHECK(elden_aw_poll(&chip)!=0&&chip.held); /* reversal after start */
    printf("PASS: %u actual Gen4 AW checks, SRAM full readback, single shots, limits/fault/cleanup; bus hardware modeled\n",checks);
}
