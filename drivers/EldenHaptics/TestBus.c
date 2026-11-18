/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The actual GENI core/adapter and AW driver run against one serial-engine
 * peer, with TWO separately addressed chips. No direct register callbacks
 * bypass the production GENI packet path in the composed test. */
#include "Bus.h"
#include "AwChip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint8_t reg[129],ram[4096],rtp[256];unsigned position,rtp_count,starts;} Motor;
typedef struct {
    uint32_t se[0x1000/4];Motor motor[2];EldenHapticsBus bus;
    uint64_t now;
    uint8_t address,operation,hold,stage,tx[64];unsigned tx_used,commands,fail_command;
    unsigned cancelled,aborted,drop_cancel,drop_abort,stall,no_data,io_fault,ops,deny;
    unsigned bad_write,reg_writes,last_reg,last_value,reenter,reentered;
    uint8_t pointer[2];uint32_t received;unsigned rx_words;
    const uint8_t *firmware;size_t firmware_bytes;
} Model;
static unsigned motor_index(uint8_t address){CHECK(address==0x5a||address==0x5b);return address-0x5a;}
static void execute_write(Model *m){
    CHECK(m->tx_used>=m->se[GENI_TX_LEN/4]);unsigned which=motor_index(m->address);
    Motor *chip=&m->motor[which];unsigned reg=m->tx[0];m->pointer[which]=(uint8_t)reg;
    for(unsigned i=1;i<m->se[GENI_TX_LEN/4];++i){
        uint8_t value=m->tx[i];
        if(reg==0x42){CHECK(chip->position<4096);chip->ram[chip->position++]=value;}
        else if(reg==0x32){CHECK(chip->rtp_count<256);chip->rtp[chip->rtp_count++]=value;}
        else{
            unsigned at=reg+i-1;CHECK(at<=0x80);chip->reg[at]=value;
            if(at==0x40||at==0x41)chip->position=((unsigned)chip->reg[0x40]<<8)|chip->reg[0x41];
            if(at==0x09&&value==1){
                if((chip->reg[8]&3)==3)chip->reg[0x3f]=0;
                else{chip->reg[0x3f]=(chip->reg[8]&3)==1?8:1;chip->starts++;}
            }
        }
    }
}
static void execute_read(Model *m){
    unsigned which=motor_index(m->address),reg=m->pointer[which],count=m->se[GENI_RX_LEN/4];
    CHECK(count>=1&&count<=4);Motor *chip=&m->motor[which];m->received=0;
    for(unsigned i=0;i<count;++i){
        unsigned value;
        if(reg==0x42){CHECK(chip->position<4096);value=chip->ram[chip->position++];}
        else{CHECK(reg+i<=0x80);value=chip->reg[reg+i];}
        m->received|=value<<(i*8);
    }
    m->rx_words=m->no_data?0:1;
}
static int admit(void *p,int cleanup){Model *m=p;return m->deny&&!cleanup?-125:0;}
static int read_reg(void *p,uint32_t reg,uint32_t *out){
    Model *m=p;if(++m->ops==m->io_fault)return -71;CHECK(reg<0x1000&&!(reg&3));
    if(reg==GENI_M_IRQ_STATUS){
        if(m->stage&&m->commands==m->fail_command){m->stage=0;m->se[GENI_STATUS/4]=0;*out=GENI_M_CMD_DONE|(1u<<10);return 0;}
        if(m->stall&&m->stage&&m->stage<=3){*out=0;return 0;}
        if(m->stage==1){*out=GENI_M_TX_WATERMARK;return 0;}
        if(m->stage==2){execute_write(m);m->stage=0;m->se[GENI_STATUS/4]=0;*out=GENI_M_CMD_DONE;return 0;}
        if(m->stage==3){execute_read(m);m->stage=0;m->se[GENI_STATUS/4]=0;*out=GENI_M_CMD_DONE;return 0;}
        if(m->stage==4&&!m->drop_cancel){m->stage=0;m->se[GENI_STATUS/4]=0;*out=GENI_M_CMD_CANCEL;return 0;}
        if(m->stage==5&&!m->drop_abort){m->stage=0;m->se[GENI_STATUS/4]=0;*out=GENI_M_CMD_ABORT;return 0;}
        *out=0;return 0;
    }
    if(reg==GENI_RX_FIFO_STATUS){*out=m->rx_words;return 0;}
    if(reg==GENI_RX_FIFO){CHECK(m->rx_words);m->rx_words--;*out=m->received;return 0;}
    *out=m->se[reg/4];return 0;
}
static int write_reg(void *p,uint32_t reg,uint32_t value){
    Model *m=p;if(++m->ops==m->io_fault)return -71;CHECK(reg<0x1000&&!(reg&3));
    ++m->reg_writes;m->last_reg=reg;m->last_value=value;
    if(reg==GENI_TX_FIFO){
        CHECK(m->stage==1&&m->tx_used+4<=64);
        for(unsigned i=0;i<4;++i)m->tx[m->tx_used++]=(uint8_t)(value>>(i*8));return 0;
    }
    if(reg==GENI_M_CMD0){
        m->operation=(uint8_t)(value>>27);m->address=(uint8_t)((value>>9)&127);m->hold=!!(value&GENI_STOP_STRETCH);
        CHECK(m->se[GENI_STATUS/4]==0);m->se[GENI_STATUS/4]=1;m->commands++;
        if(m->operation==GENI_OP_WRITE){CHECK(m->se[GENI_TX_LEN/4]<=64);m->stage=1;m->tx_used=0;}
        else{CHECK(m->operation==GENI_OP_READ);m->stage=3;}
        if(m->reenter&&!m->reentered){
            m->reentered=1;uint8_t v=0;
            CHECK(elden_haptics_bus_read(&m->bus,0x5b,7,&v,1)==EldenHapticsBusBusy);
        }
    }
    if(reg==GENI_TX_WATERMARK&&!value&&m->stage==1&&m->tx_used)m->stage=2;
    if(reg==GENI_M_CMD_CTRL&&value==GENI_CMD_CTRL_CANCEL){m->cancelled++;m->stage=4;}
    if(reg==GENI_M_CMD_CTRL&&value==GENI_CMD_CTRL_ABORT){m->aborted++;m->stage=5;}
    if(reg!=GENI_M_IRQ_CLEAR)m->se[reg/4]=value;
    return 0;
}
static uint64_t now(void *p){return ((Model *)p)->now;}
static int delay(void *p,unsigned us){((Model *)p)->now+=us;return 0;}
static void initial(Model *m){
    memset(m,0,sizeof(*m));m->se[GENI_FW_REVISION/4]=GENI_PROTO_I2C<<8;m->se[GENI_CLK_CFG/4]=1;m->se[GENI_SCL_COUNTERS/4]=0x123456;
    m->se[GENI_TX_LEN/4]=17;m->se[GENI_RX_LEN/4]=18;m->se[GENI_SE_IRQ_EN/4]=7;
    m->se[0xe24/4]=(32u<<24)|(16u<<16);
    for(unsigned n=0;n<2;++n){m->motor[n].reg[0x57]=0x93;m->motor[n].reg[0x58]=0x70;}
}
static EldenHapticsBusIo callbacks(Model *m){EldenHapticsBusIo io={m,read_reg,write_reg,admit,now,delay};return io;}
static void acquire(Model *m){EldenHapticsBusIo io=callbacks(m);CHECK(!elden_haptics_bus_acquire(&m->bus,&io));CHECK(m->se[GENI_SE_IRQ_EN/4]==0);}
static int chip_read(void *p,uint8_t address,uint8_t reg,uint8_t *out,size_t bytes){Model *m=p;return elden_haptics_bus_read(&m->bus,address,reg,out,bytes);}
static int chip_write(void *p,uint8_t address,uint8_t reg,const uint8_t *in,size_t bytes){Model *m=p;return elden_haptics_bus_write(&m->bus,address,reg,in,bytes);}
static int chip_admit(void *p,uint8_t address,enum EldenAwOperation op){(void)p;CHECK(address==0x5a||address==0x5b);CHECK(op<=EldenAwStop);return 0;}
static int chip_firmware(void *p,const uint8_t *data,size_t size){Model *m=p;return data==m->firmware&&size==m->firmware_bytes?0:-84;}
static int chip_delay(void *p,unsigned low,unsigned high){CHECK(low<=high);return delay(p,low);}
static void basic(void){
    Model m;initial(&m);acquire(&m);unsigned writes=m.reg_writes;
    EldenHapticsBusIo io=callbacks(&m);CHECK(elden_haptics_bus_acquire(&m.bus,&io)==EldenHapticsBusBusy&&m.reg_writes==writes);
    for(unsigned motor=0;motor<2;++motor)for(unsigned n=1;n<=16;++n){
        uint8_t input[16],output[16];for(unsigned i=0;i<n;++i)input[i]=(uint8_t)(i+n+motor*30);
        CHECK(!elden_haptics_bus_write(&m.bus,(uint8_t)(0x5a+motor),0x60,input,n));
        memset(output,0xa5,sizeof(output));CHECK(!elden_haptics_bus_read(&m.bus,(uint8_t)(0x5a+motor),0x60,output,n));CHECK(!memcmp(input,output,n));
    }
    uint8_t data[16];for(unsigned i=0;i<16;++i)data[i]=(uint8_t)(255-i);uint8_t addr[2]={8,0};
    CHECK(!elden_haptics_bus_write(&m.bus,0x5a,0x40,addr,2));CHECK(!elden_haptics_bus_write(&m.bus,0x5a,0x42,data,16));
    CHECK(!memcmp(m.motor[0].ram+0x800,data,16));CHECK(m.motor[1].ram[0x800]==0);
    CHECK(!elden_haptics_bus_write(&m.bus,0x5a,0x40,addr,2));uint8_t output[16];CHECK(!elden_haptics_bus_read(&m.bus,0x5a,0x42,output,16));CHECK(!memcmp(data,output,16));
    CHECK(!elden_haptics_bus_write(&m.bus,0x5b,0x32,data,16)&&m.motor[1].rtp_count==16);
    m.reenter=1;CHECK(!elden_haptics_bus_read(&m.bus,0x5a,7,output,1)&&m.reentered==1);
    writes=m.reg_writes;CHECK(elden_haptics_bus_write(&m.bus,0x5a,0x80,data,2)==EldenHapticsBusArgument&&m.reg_writes==writes);
    CHECK(!elden_haptics_bus_release(&m.bus));CHECK(!m.bus.acquired&&m.last_reg==GENI_SE_IRQ_EN&&m.last_value==7);
    CHECK(m.se[GENI_TX_LEN/4]==17&&m.se[GENI_RX_LEN/4]==18);
}
static void faults(void){
    Model m;uint8_t output[16],before[16];
    initial(&m);m.se[GENI_DMA_MODE/4]=1;EldenHapticsBusIo io=callbacks(&m);
    CHECK(elden_haptics_bus_acquire(&m.bus,&io)==EldenHapticsBusError&&m.reg_writes==0);
    initial(&m);acquire(&m);m.fail_command=3;memset(output,0xa5,16);memcpy(before,output,16);
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,0x60,output,16)==EldenHapticsBusError&&!memcmp(before,output,16));
    CHECK(!m.bus.held&&!elden_haptics_bus_release(&m.bus));
    initial(&m);acquire(&m);m.stall=1;
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,7,output,1)==EldenHapticsBusTimeout&&m.cancelled==1&&!m.bus.held);
    CHECK(!elden_haptics_bus_release(&m.bus));
    initial(&m);acquire(&m);m.stall=1;m.drop_cancel=1;
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,7,output,1)==EldenHapticsBusTimeout&&m.cancelled==1&&m.aborted==1&&!m.bus.held);
    CHECK(!elden_haptics_bus_release(&m.bus));
    initial(&m);acquire(&m);m.stall=1;m.drop_cancel=m.drop_abort=1;
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,7,output,1)==EldenHapticsBusTimeout&&m.bus.held);
    unsigned commands=m.commands;CHECK(elden_haptics_bus_write(&m.bus,0x5b,7,before,1)==EldenHapticsBusHeld&&m.commands==commands);
    CHECK(elden_haptics_bus_release(&m.bus)==EldenHapticsBusHeld);
    initial(&m);acquire(&m);m.no_data=1;
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,7,output,1)==EldenHapticsBusError&&!m.bus.held);
    initial(&m);acquire(&m);m.io_fault=m.ops+7;
    CHECK(elden_haptics_bus_write(&m.bus,0x5a,7,before,1)==-71&&m.bus.held);
    initial(&m);acquire(&m);m.deny=1;commands=m.commands;
    CHECK(elden_haptics_bus_read(&m.bus,0x5a,7,output,1)==-125&&m.commands==commands);
}
static void composed(const uint8_t *firmware,size_t bytes){
    Model m;initial(&m);acquire(&m);m.firmware=firmware;m.firmware_bytes=bytes;
    EldenAwIo io={&m,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};EldenAwChip left,right;
    CHECK(!elden_aw_bind(&left,&io,0x5a)&&!elden_aw_bind(&right,&io,0x5b));
    CHECK(!elden_aw_initialize(&left,firmware,bytes,64));CHECK(!elden_aw_initialize(&right,firmware,bytes,64));
    CHECK(left.ready&&right.ready&&!m.motor[0].starts&&!m.motor[1].starts);
    CHECK(!memcmp(m.motor[0].ram+0x800,firmware+4,bytes-4)&&!memcmp(m.motor[1].ram+0x800,firmware+4,bytes-4));
    CHECK(!elden_aw_ram_start(&left,1,32,10000));CHECK(m.motor[0].starts==1&&m.motor[1].starts==0);
    CHECK(!elden_aw_stop(&left));CHECK(!elden_aw_ram_start(&right,1,16,10000));
    CHECK(m.motor[0].reg[7]==32&&m.motor[1].reg[7]==16);
    CHECK(!elden_aw_stop(&right));CHECK(!elden_haptics_bus_release(&m.bus));
}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t firmware[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);
    size_t bytes=fread(firmware,1,sizeof(firmware),f);CHECK(bytes==1436&&!ferror(f));CHECK(!fclose(f));
    basic();faults();composed(firmware,bytes);
    printf("PASS: %u GENI/AW checks; one actual bus for two chip instances, full readback, timeout acknowledgement and refusal; MMIO modeled\n",checks);
    return 0;
}
