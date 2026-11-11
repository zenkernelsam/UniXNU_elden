/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UcsiQuery.h"
#include "PdContract.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
typedef struct {uint32_t pdo[7],rdo;unsigned count,pdo_requests,status_reads[2];bool unsupported,changed;} Ppm;
static void run(EldenUcsiQuery *q,Ppm *m){
    uint8_t out[544],response[64];uint64_t control=0;uint32_t cci=0;
    for(unsigned guard=0;q->phase!=EldenUcsiReady&&q->phase!=EldenUcsiFailed&&guard<120;guard++){
        int n=elden_ucsi_next(q,out,sizeof(out));CHECK(n>0);CHECK(get(out)==32779&&get(out+4)==1);
        memset(response,0,sizeof(response));put(response,32779);put(response+4,1);
        if(get(out+8)==0x12){
            CHECK(n==64);control=get(out+20)|(uint64_t)get(out+24)<<32;
            unsigned op=control&255;CHECK(op==4||op==6||op==7||op==0x12||op==0x10);
            if(op==4)cci=1u<<29;
            else if(op==0x10){
                unsigned port=(control>>16)&127,offset=(control>>24)&255,count=((control>>32)&3)+1;
                CHECK(port==2&&offset<=4&&(control&(1ull<<23))&&(control&(1ull<<34)));
                CHECK(count==(offset?3u:4u));++m->pdo_requests;
                unsigned remaining=offset<m->count?m->count-offset:0;if(remaining>count)remaining=count;
                cci=(1u<<31)|(remaining*4<<8);if(m->unsupported)cci|=1u<<25;
            }else cci=(1u<<31)|((op==6?16u:op==7?2u:9u)<<8);
            put(response+8,0x12);CHECK(elden_ucsi_receive(q,response,16)==EldenUcsiProgress);
        }else{
            CHECK(n==12&&get(out+8)==0x11);put(response+8,0x11);put(response+12,0x110);put(response+16,cci);
            unsigned op=control&255,port=(control>>16)&127;uint8_t *data=response+28;
            if(op==6){put(data,0x4046);data[4]=2;data[5]=0;} /* actual PPM quirk */
            if(op==7)put(data,0x3fe7);
            if(op==0x12){
                CHECK(port>=1&&port<=2);++m->status_reads[port-1];
                put(data,(1u<<19)|(port==1?(1u<<16)|(1u<<21):(3u<<16)));
                put(data+4,port==1?0:(m->changed&&m->status_reads[1]>1?m->rdo^(1u<<28):m->rdo));
            }
            if(op==0x10&&!m->unsupported){
                unsigned off=(control>>24)&255,n=(cci>>8)&255;
                for(unsigned i=0;i<n/4;i++)put(data+4*i,m->pdo[off+i]);
            }
            int rc=elden_ucsi_receive(q,response,64);CHECK(rc==EldenUcsiProgress||rc==EldenUcsiCycle);
        }
    }
    CHECK(q->phase==EldenUcsiReady);
}
static Ppm supply(unsigned count){
    Ppm m={.rdo=0x1204b12c,.count=count};m.pdo[0]=0x0a01912c;m.pdo[1]=0x0002d0de;
    for(unsigned i=2;i<count;i++)m.pdo[i]=((200+i*20u)<<10)|200u;
    return m;
}
static void protocol(void){
    CHECK(elden_ucsi_pdo_control(2,0,4)==0x0000000700820010ull);
    CHECK(!elden_ucsi_pdo_control(3,0,4)&&!elden_ucsi_pdo_control(2,5,4));
    for(unsigned count=2;count<=7;count+=5){
        Ppm m=supply(count);EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_enable_pdos(&q,true);elden_ucsi_link(&q,true);run(&q,&m);
        CHECK(m.pdo_requests==(count==2?1u:2u));CHECK(q.committed.port[0].pdo_count==0);
        EldenUcsiPort *p=&q.committed.port[1];CHECK(p->pdo_count==count&&p->pdos_complete&&p->pdos_consistent&&!p->pdos_error);
        EldenPdContract c;CHECK(!elden_pd_contract(p->source_pdos,p->pdo_count,p->rdo,&c));
        CHECK(c.position==1&&c.requested_mv==5000&&c.requested_ma==3000&&c.requested_mw==15000);
    }
    Ppm m=supply(2);m.changed=true;EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_enable_pdos(&q,true);elden_ucsi_link(&q,true);run(&q,&m);
    CHECK(q.committed.port[1].pdo_count==2&&!q.committed.port[1].pdos_consistent);
    m=supply(2);m.unsupported=true;elden_ucsi_init(&q);elden_ucsi_enable_pdos(&q,true);elden_ucsi_link(&q,true);run(&q,&m);
    CHECK(q.committed.port[1].pdos_error&&q.pdo_disabled==2&&q.committed.port[1].status_valid);
    unsigned requests=m.pdo_requests;elden_ucsi_refresh(&q);run(&q,&m);CHECK(m.pdo_requests==requests);
    elden_ucsi_refresh(&q);uint8_t out[544];CHECK(elden_ucsi_next(&q,out,sizeof(out))>0);elden_ucsi_timeout(&q);CHECK(q.phase==EldenUcsiFailed);
}
static void contracts(void){
    uint32_t pdo[2]={(100u<<10)|300u,(180u<<10)|222u};EldenPdContract c,before;
    CHECK(!elden_pd_contract(pdo,2,0x220378de,&c)&&c.requested_mv==9000&&c.requested_ma==2220);
    memset(&before,0xa5,sizeof(before));c=before;CHECK(elden_pd_contract(pdo,2,0x3204b12c,&c)!=0&&!memcmp(&before,&c,sizeof(c)));
    CHECK(elden_pd_contract(pdo,2,0x2204b12c,&c)!=0); /* 3A request exceeds9V source */
    uint32_t pps=0xc0000000u|(110u<<17)|(33u<<8)|60u;
    CHECK(!elden_pd_contract(&pps,1,(1u<<28)|(450u<<9)|40u,&c)&&c.requested_mv==9000&&c.requested_ma==2000);
    CHECK(elden_pd_contract(&pps,1,(1u<<28)|(600u<<9)|40u,&c)!=0);
    pps|=1u<<28;CHECK(elden_pd_contract(&pps,1,1u<<28,&c)==-95);
    uint32_t variable=0x80000000u|(240u<<20)|(100u<<10)|300u;
    CHECK(!elden_pd_contract(&variable,1,(1u<<28)|(200u<<10)|200u,&c)&&!c.exact_voltage&&c.min_mv==5000&&c.max_mv==12000);
    uint32_t battery=0x40000000u|(240u<<20)|(100u<<10)|120u;
    CHECK(!elden_pd_contract(&battery,1,(1u<<28)|(80u<<10)|80u,&c)&&!c.exact_voltage&&c.requested_mw==20000);
}
int main(void){protocol();contracts();printf("UCSI PDO pagination/stability and PD contracts: %u checks passed\n",checks);}
