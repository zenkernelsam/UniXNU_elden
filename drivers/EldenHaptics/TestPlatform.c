/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual platform methods; MMIO, oscillator and shared-owner peers modeled. */
#include "Platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint32_t gcc[0x60000/4],se[0x4000/4],pins[0x2000/4],reset[2][2],irq[2];
    uint64_t now;unsigned reads,writes,fail_read,fail_write,denied,lock,foreign,contract,stuck,changed_readback;
} Model;
static uint32_t *reg(Model *m,enum EldenHapticsSpace s,uint32_t r){
    CHECK(!(r&3));
    switch(s){
    case EldenHapticsGcc:CHECK(r<0x60000);return &m->gcc[r/4];
    case EldenHapticsSe:CHECK(r<0x4000);return &m->se[r/4];
    case EldenHapticsPins:CHECK(r==0||r==0x1000);return &m->pins[r/4];
    case EldenHapticsLeftReset:case EldenHapticsRightReset:CHECK(r==0||r==4);return &m->reset[s-EldenHapticsLeftReset][r/4];
    case EldenHapticsLeftIrq:case EldenHapticsRightIrq:CHECK(r==0);return &m->irq[s-EldenHapticsLeftIrq];
    }
    CHECK(0);return 0;
}
static int read_reg(void *v,enum EldenHapticsSpace s,uint32_t r,uint32_t *out){
    Model *m=v;if(++m->reads==m->fail_read)return -14;
    if(s==EldenHapticsGcc){
        const uint32_t halt[]={0x2315c,0x23148,0x23140,0x23144,0x18644};
        const unsigned bit[]={18,19,20,21,28};
        for(unsigned i=0;i<5;i++)if(r==halt[i]){*out=m->stuck||!(m->gcc[0x52008/4]&(1u<<bit[i]))?1u<<31:0;return 0;}
    }
    *out=*reg(m,s,r);return 0;
}
static int write_reg(void *v,enum EldenHapticsSpace s,uint32_t r,uint32_t value){
    Model *m=v;if(++m->writes==m->fail_write)return -14;
    CHECK(s<=EldenHapticsPins); // no reset/IRQ/output/camera/PMIC writer
    if(s==EldenHapticsGcc){
        CHECK(r==ELDEN_HAPTICS_VOTE_REG||r==ELDEN_HAPTICS_RCG||r==ELDEN_HAPTICS_RCG+4);
        if(r==ELDEN_HAPTICS_VOTE_REG)CHECK(m->lock==1);
        if(r==ELDEN_HAPTICS_RCG){value=value&2?value&~(1u<<31):value|(1u<<31);if(!m->stuck)value&=~1u;}
    }
    if(s==EldenHapticsSe)CHECK(r==GENI_CLK_CFG||r==GENI_CLK_SEL||r==GENI_SCL_COUNTERS);
    if(m->changed_readback&&s==EldenHapticsPins)value^=4;
    *reg(m,s,r)=value;return 0;
}
static int admit(void *v,int cleanup){Model *m=v;(void)cleanup;return m->denied?-125:0;}
static int gcc_lock(void *v,int acquire){Model *m=v;if(acquire){if(m->foreign||m->lock)return -16;m->lock=1;return 0;}CHECK(m->lock==1);m->lock=0;return 0;}
static uint32_t contract(void *v){return ((Model *)v)->contract;}
static uint64_t now(void *v){return ((Model *)v)->now;}
static int delay(void *v,unsigned us){((Model *)v)->now+=us;return 0;}
static Model *fresh(EldenHapticsPlatform *p){
    Model *m=calloc(1,sizeof(*m));CHECK(m);memset(p,0,sizeof(*p));m->contract=1;
    m->gcc[ELDEN_HAPTICS_VOTE_REG/4]=1u<<4;m->gcc[ELDEN_HAPTICS_RCG/4]=1u<<31;
    m->gcc[(ELDEN_HAPTICS_RCG+4)/4]=0x267;
    m->se[GENI_FW_REVISION/4]=GENI_PROTO_I2C<<8;m->se[GENI_CLK_CFG/4]=7;m->se[GENI_CLK_SEL/4]=3;m->se[GENI_SCL_COUNTERS/4]=0x121314;
    for(unsigned i=0;i<2;i++){m->reset[i][0]=1u<<9;m->reset[i][1]=2;m->pins[i*0x1000/4]=0x10000080;}
    return m;
}
static EldenHapticsPlatformIo io(Model *m){EldenHapticsPlatformIo r={m,read_reg,write_reg,admit,gcc_lock,contract,now,delay};return r;}
static int acquire(Model *m,EldenHapticsPlatform *p,uint32_t hz){EldenHapticsPlatformIo r=io(m);return elden_haptics_platform_acquire(p,&r,hz);}
int main(void){
    EldenHapticsPlatform p;Model *m;
    const uint32_t hz[]={100000,400000,1000000},div[]={7,2,1},high[]={10,5,2},low[]={12,11,8},cycle[]={26,22,18};
    unsigned acquire_writes=0,acquire_reads=0;
    for(unsigned i=0;i<3;i++){
        m=fresh(&p);CHECK(!acquire(m,&p,hz[i])&&p.ready&&!p.held);
        CHECK(m->se[GENI_CLK_CFG/4]==((7u&~0xfff1u)|(div[i]<<4)|1u));
        CHECK(m->se[GENI_SCL_COUNTERS/4]==((high[i]<<20)|(low[i]<<10)|cycle[i]));
        CHECK(m->gcc[ELDEN_HAPTICS_VOTE_REG/4]==((1u<<4)|ELDEN_HAPTICS_VOTE_MASK));
        EldenHapticsBusIo b={0};CHECK(!elden_haptics_platform_bus_io(&p,&b));uint32_t v;CHECK(!b.read(b.context,GENI_CLK_CFG,&v)&&v==m->se[GENI_CLK_CFG/4]);
        if(i==0){acquire_writes=m->writes;acquire_reads=m->reads-1;}
        m->pins[0]|=1u<<28;m->gcc[ELDEN_HAPTICS_VOTE_REG/4]|=1u<<8;
        CHECK(!elden_haptics_platform_release(&p,1)&&!p.owned&&!p.changed&&!m->lock);
        CHECK(m->pins[0]==0x10000080&&m->se[GENI_CLK_CFG/4]==7&&m->se[GENI_CLK_SEL/4]==3&&m->se[GENI_SCL_COUNTERS/4]==0x121314);
        CHECK(m->gcc[ELDEN_HAPTICS_VOTE_REG/4]==((1u<<4)|(1u<<8))&&m->gcc[(ELDEN_HAPTICS_RCG+4)/4]==0x267);
        free(m);++cases;
    }
    m=fresh(&p);CHECK(!acquire(m,&p,400000));m->gcc[ELDEN_HAPTICS_VOTE_REG/4]|=1u<<26;
    CHECK(!elden_haptics_platform_release(&p,1));
    CHECK((m->gcc[ELDEN_HAPTICS_VOTE_REG/4]&((15u<<18)|(1u<<26)|(1u<<28)))==((15u<<18)|(1u<<26)));free(m);++cases;
    // All early refusal modes produce no register write.
    for(unsigned i=0;i<7;i++){
        m=fresh(&p);if(i==0)m->contract=0;if(i==1)m->denied=1;if(i==2)m->gcc[(ELDEN_HAPTICS_RCG+0x14)/4]=1;
        if(i==3)m->reset[0][1]=0;if(i==4)m->reset[1][0]=4;if(i==5)m->irq[0]=1u<<9;if(i==6)m->pins[0]=8;
        CHECK(acquire(m,&p,400000)!=0&&!m->writes&&!m->lock);free(m);++cases;
    }
    for(unsigned i=0;i<6;i++){
        m=fresh(&p);if(i==0)m->se[GENI_STATUS/4]=1;if(i==1)m->se[GENI_GSI_EVENT_EN/4]=15;
        if(i==2)m->se[GENI_DMA_MODE/4]=1;if(i==3)m->se[GENI_FW_REVISION/4]=1u<<8;
        if(i==4)m->foreign=1;if(i==5)m->changed_readback=1;
        CHECK(acquire(m,&p,400000)!=0&&p.held);unsigned writes=m->writes;
        CHECK(elden_haptics_platform_release(&p,1)!=0&&m->writes==writes);free(m);++cases;
    }
    // Every failed prepare write/read is retained, never silently restored.
    for(unsigned i=1;i<=acquire_writes;i++){
        m=fresh(&p);m->fail_write=i;CHECK(acquire(m,&p,100000)!=0&&p.held&&!m->lock);
        unsigned writes=m->writes;CHECK(elden_haptics_platform_release(&p,1)!=0&&m->writes==writes);free(m);++cases;
    }
    for(unsigned i=1;i<=acquire_reads;i++){
        m=fresh(&p);m->fail_read=i;CHECK(acquire(m,&p,100000)!=0&&!m->lock);free(m);++cases;
    }
    m=fresh(&p);CHECK(!acquire(m,&p,400000));m->se[GENI_CLK_SEL/4]=2;
    unsigned writes=m->writes;CHECK(elden_haptics_platform_release(&p,1)!=0&&p.held&&m->writes==writes);free(m);++cases;
    m=fresh(&p);CHECK(!acquire(m,&p,400000));writes=m->writes;CHECK(elden_haptics_platform_release(&p,0)!=0&&m->writes==writes);
    CHECK(!elden_haptics_platform_release(&p,1));free(m);++cases;
    printf("PASS: %u actual haptics platform cases/%u checks, exact timing, shared vote, ownership, readback and retention; MMIO modeled\n",cases,checks);
}
