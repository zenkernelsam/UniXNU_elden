/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Platform.h"
#define ROOT_EN 2u
#define ROOT_OFF (1u<<31)
#define UPDATE 1u
#define RATE_MASK 0x371fu
#define PIN_MASK 0x3ffu
#define SERIAL_MASK ((4095u<<4)|1u)
/* Fixed Linux i2c-qcom-geni.c 19p2 table, not computed ideal I2C periods. */
typedef struct {uint32_t hz;uint8_t div,high,low,cycle;} Timing;
static const Timing timings[]={{100000,7,10,12,26},{400000,2,5,11,22},{1000000,1,2,8,18}};
static const uint32_t serial_regs[]={GENI_CLK_CFG,GENI_CLK_SEL,GENI_SCL_COUNTERS};
static const uint32_t halts[]={0x2315c,0x23148,0x23140,0x23144,0x18644};
static int fail(EldenHapticsPlatform *p,int rc){p->error=rc;if(p->changed)p->held=1;return rc;}
static int read_reg(EldenHapticsPlatform *p,enum EldenHapticsSpace space,uint32_t reg,uint32_t *out){
    int rc=p->io.admit(p->io.context,p->cleanup);if(rc)return rc;
    return p->io.read(p->io.context,space,reg,out);
}
static int write_reg(EldenHapticsPlatform *p,enum EldenHapticsSpace space,uint32_t reg,uint32_t value){
    int rc=p->io.admit(p->io.context,p->cleanup);if(rc)return rc;
    return p->io.write(p->io.context,space,reg,value);
}
static int write_checked(EldenHapticsPlatform *p,enum EldenHapticsSpace space,uint32_t reg,uint32_t value,uint32_t mask){
    int rc=write_reg(p,space,reg,value);if(rc)return rc;uint32_t actual;
    rc=read_reg(p,space,reg,&actual);return rc?rc:(actual&mask)==(value&mask)?0:EldenHapticsBusError;
}
static int wait_bit(EldenHapticsPlatform *p,uint32_t reg,uint32_t mask,int on){
    for(unsigned n=0;n<1000;n++){
        uint32_t value;int rc=read_reg(p,EldenHapticsGcc,reg,&value);if(rc)return rc;
        if((value&mask)==(on?mask:0))return 0;
        rc=p->io.delay_us(p->io.context,10);if(rc)return rc;
    }
    return EldenHapticsBusTimeout;
}
static int vote(EldenHapticsPlatform *p,int restoring){
    int rc=p->io.gcc_lock(p->io.context,1);if(rc)return rc;
    uint32_t current=0,mask=ELDEN_HAPTICS_VOTE_MASK;
    rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_VOTE_REG,&current);
    if(!rc){
        if(restoring&&current&ELDEN_HAPTICS_OTHER_SE)mask&=~ELDEN_HAPTICS_WRAPPER_VOTES;
        if(!restoring){p->vote=current;p->changed|=EldenHapticsChangedVote;}
        uint32_t value=restoring?(current&~mask)|(p->vote&mask):current|mask;
        rc=write_checked(p,EldenHapticsGcc,ELDEN_HAPTICS_VOTE_REG,value,mask);
    }
    int released=p->io.gcc_lock(p->io.context,0);
    return rc?rc:released;
}
static int idle(EldenHapticsPlatform *p){
    const uint32_t regs[]={GENI_STATUS,GENI_GSI_EVENT_EN,GENI_DMA_MODE,0x800,GENI_RX_FIFO_STATUS};
    uint32_t values[5];for(unsigned i=0;i<5;i++){
        int rc=read_reg(p,EldenHapticsSe,regs[i],&values[i]);if(rc)return rc;
    }
    if(values[0]&((1u<<0)|(1u<<12))||values[1]||values[2]||values[3]||values[4])return EldenHapticsBusBusy;
    return 0;
}
int elden_haptics_platform_acquire(EldenHapticsPlatform *p,const EldenHapticsPlatformIo *io,uint32_t hz){
    if(!p||!io||!io->read||!io->write||!io->admit||!io->gcc_lock||!io->touch_gcc_contract||!io->now_us||!io->delay_us||
       p->owned||p->held||p->changed)return EldenHapticsBusArgument;
    const Timing *t=0;for(unsigned i=0;i<3;i++)if(timings[i].hz==hz)t=&timings[i];
    if(!t)return EldenHapticsBusArgument;
    int rc=io->admit(io->context,0);if(rc)return rc;
    if(io->touch_gcc_contract(io->context)!=1)return EldenHapticsBusHeld;
    EldenHapticsPlatform value={0};value.io=*io;value.bus_hz=hz;value.owned=1;*p=value;
    p->phase=1;
    uint32_t dfs=0;
    if((rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,&p->command))||
       (rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+4,&p->cfg))||
       (rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+0x14,&dfs)))return fail(p,rc);
    if(p->command&UPDATE||dfs&1)return fail(p,EldenHapticsBusBusy);
    /* Refuse a reset or completion pad owned by another function. Never pulse
     * either motor reset just to obtain an identity response. */
    for(unsigned m=0;m<2;m++){
        enum EldenHapticsSpace reset=m?EldenHapticsRightReset:EldenHapticsLeftReset;
        enum EldenHapticsSpace irq=m?EldenHapticsRightIrq:EldenHapticsLeftIrq;
        uint32_t cfg,data;
        if((rc=read_reg(p,reset,0,&cfg))||(rc=read_reg(p,reset,4,&data)))return fail(p,rc);
        if((cfg&(15u<<2))||!(cfg&(1u<<9))||!(data&2))return fail(p,EldenHapticsBusHeld);
        if((rc=read_reg(p,irq,0,&cfg)))return fail(p,rc);
        if(cfg&((15u<<2)|(1u<<9)))return fail(p,EldenHapticsBusHeld);
        if((rc=read_reg(p,EldenHapticsPins,m*ELDEN_HAPTICS_GPIO_STRIDE,&p->pins[m])))return fail(p,rc);
        if(((p->pins[m]>>2)&15)>ELDEN_HAPTICS_GPIO_FUNC)return fail(p,EldenHapticsBusHeld);
    }
    p->changed|=EldenHapticsChangedRoot;
    if((rc=write_checked(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,p->command|ROOT_EN,ROOT_EN))||
       (rc=wait_bit(p,ELDEN_HAPTICS_RCG,ROOT_OFF,0))||(rc=vote(p,0)))return fail(p,rc);
    for(unsigned i=0;i<5;i++)if((rc=wait_bit(p,halts[i],ROOT_OFF,0)))return fail(p,rc);
    p->phase=2;if((rc=idle(p)))return fail(p,rc);
    uint32_t revision,disabled;
    if((rc=read_reg(p,EldenHapticsSe,GENI_FW_REVISION,&revision))||
       (rc=read_reg(p,EldenHapticsSe,GENI_IF_DISABLE,&disabled)))return fail(p,rc);
    if(((revision>>8)&255)!=GENI_PROTO_I2C||disabled&1)return fail(p,EldenHapticsBusHeld);
    for(unsigned i=0;i<3;i++)if((rc=read_reg(p,EldenHapticsSe,serial_regs[i],&p->serial[i])))return fail(p,rc);
    p->changed|=EldenHapticsChangedRate;
    if((rc=write_checked(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+4,(p->cfg&~RATE_MASK)|1u,RATE_MASK))||
       (rc=write_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,p->command|ROOT_EN|UPDATE))||
       (rc=wait_bit(p,ELDEN_HAPTICS_RCG,UPDATE,0)))return fail(p,rc);
    uint32_t cfg;
    if((rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+4,&cfg)))return fail(p,rc);
    if((cfg&RATE_MASK)!=1)return fail(p,EldenHapticsBusError);
    p->changed|=EldenHapticsChangedPins;
    for(unsigned m=0;m<2;m++){
        uint32_t value=(p->pins[m]&~PIN_MASK)|(ELDEN_HAPTICS_GPIO_FUNC<<2)|3u; //2mA, input, pull-up
        if((rc=write_checked(p,EldenHapticsPins,m*ELDEN_HAPTICS_GPIO_STRIDE,value,PIN_MASK)))return fail(p,rc);
    }
    p->changed|=EldenHapticsChangedSerialClock;
    p->applied_serial[0]=(p->serial[0]&~SERIAL_MASK)|((uint32_t)t->div<<4)|1u;
    p->applied_serial[1]=0;p->applied_serial[2]=((uint32_t)t->high<<20)|((uint32_t)t->low<<10)|t->cycle;
    for(unsigned i=0;i<3;i++)if((rc=write_checked(p,EldenHapticsSe,serial_regs[i],p->applied_serial[i],UINT32_MAX)))return fail(p,rc);
    p->ready=1;p->phase=3;return 0;
}
static int bus_read(void *v,uint32_t reg,uint32_t *out){return read_reg(v,EldenHapticsSe,reg,out);}
static int bus_write(void *v,uint32_t reg,uint32_t value){return write_reg(v,EldenHapticsSe,reg,value);}
static int bus_admit(void *v,int cleanup){
    EldenHapticsPlatform *p=v;
    if(!p->ready||p->held)return EldenHapticsBusHeld;
    return p->io.admit(p->io.context,cleanup);
}
static uint64_t bus_now(void *v){EldenHapticsPlatform *p=v;return p->io.now_us(p->io.context);}
static int bus_delay(void *v,unsigned us){EldenHapticsPlatform *p=v;return p->io.delay_us(p->io.context,us);}
int elden_haptics_platform_bus_io(EldenHapticsPlatform *p,EldenHapticsBusIo *out){
    if(!p||!out||!p->ready||p->held)return EldenHapticsBusHeld;
    EldenHapticsBusIo value={p,bus_read,bus_write,bus_admit,bus_now,bus_delay};*out=value;return 0;
}
int elden_haptics_platform_release(EldenHapticsPlatform *p,int bus_restored){
    if(!p||!p->owned||!bus_restored||p->held)return EldenHapticsBusHeld;
    p->cleanup=1;int rc=0;
    if(p->changed&&(rc=idle(p)))return fail(p,rc);
    /* Refuse outside mutation instead of restoring over another owner. */
    if(p->changed&EldenHapticsChangedSerialClock){
        for(unsigned i=0;i<3;i++){
            uint32_t current;
            if((rc=read_reg(p,EldenHapticsSe,serial_regs[i],&current)))return fail(p,rc);
            if(current!=p->applied_serial[i])return fail(p,EldenHapticsBusHeld);
        }
        for(unsigned i=0;i<3;i++)if((rc=write_checked(p,EldenHapticsSe,serial_regs[i],p->serial[i],UINT32_MAX)))return fail(p,rc);
        p->changed&=~EldenHapticsChangedSerialClock;
    }
    if(p->changed&EldenHapticsChangedPins){
        for(unsigned m=0;m<2;m++){
            uint32_t current;
            if((rc=read_reg(p,EldenHapticsPins,m*ELDEN_HAPTICS_GPIO_STRIDE,&current)))return fail(p,rc);
            if((current&PIN_MASK)!=((ELDEN_HAPTICS_GPIO_FUNC<<2)|3u))return fail(p,EldenHapticsBusHeld);
            if((rc=write_checked(p,EldenHapticsPins,m*ELDEN_HAPTICS_GPIO_STRIDE,(current&~PIN_MASK)|(p->pins[m]&PIN_MASK),PIN_MASK)))return fail(p,rc);
        }
        p->changed&=~EldenHapticsChangedPins;
    }
    if(p->changed&EldenHapticsChangedRate){
        uint32_t current;
        if((rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+4,&current)))return fail(p,rc);
        if((current&RATE_MASK)!=1)return fail(p,EldenHapticsBusHeld);
        if((rc=write_checked(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG+4,(current&~RATE_MASK)|(p->cfg&RATE_MASK),RATE_MASK))||
           (rc=write_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,p->command|ROOT_EN|UPDATE))||
           (rc=wait_bit(p,ELDEN_HAPTICS_RCG,UPDATE,0)))return fail(p,rc);
        p->changed&=~EldenHapticsChangedRate;
    }
    if(p->changed&EldenHapticsChangedVote){if((rc=vote(p,1)))return fail(p,rc);p->changed&=~EldenHapticsChangedVote;}
    if(p->changed&EldenHapticsChangedRoot){
        uint32_t current;
        if((rc=read_reg(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,&current)))return fail(p,rc);
        if((rc=write_checked(p,EldenHapticsGcc,ELDEN_HAPTICS_RCG,(current&~ROOT_EN)|(p->command&ROOT_EN),ROOT_EN)))return fail(p,rc);
        p->changed&=~EldenHapticsChangedRoot;
    }
    p->cleanup=0;p->owned=0;p->ready=0;p->phase=4;return 0;
}
