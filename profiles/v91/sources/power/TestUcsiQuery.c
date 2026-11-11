/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UcsiQuery.h"
#include "ChargePoll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
struct ppm {unsigned size;uint16_t version;uint32_t cci,flags[2],rdo[2];unsigned commands,acks,resets,notifies;uint64_t last;};
static void response(struct ppm *p,uint8_t *out){
    memset(out,0,16+p->size);put(out,32779);put(out+4,1);put(out+8,0x11);
    uint8_t *buf=out+12;put(buf,p->version);put(buf+4,p->cci);
    unsigned op=p->last&255,port=(p->last>>16)&127;
    if(op==6){put(buf+16,0x4006);buf[20]=2;buf[21]=0x10;}
    if(op==7){put(buf+16,0x6003);}
    if(op==0x12){
        CHECK(port>=1&&port<=2);put(buf+16,p->flags[port-1]);put(buf+20,p->rdo[port-1]);buf[24]=1;
        if(p->version>=0x200)buf[26]=port==1?0x40:0x80;
    }
}
static void run(EldenUcsiQuery *q,struct ppm *p){
    uint8_t out[544],in[544];
    for(unsigned guard=0;q->phase!=EldenUcsiReady&&q->phase!=EldenUcsiFailed&&guard<100;guard++){
        int n=elden_ucsi_next(q,out,sizeof(out));CHECK(n>0);CHECK(get(out)==32779&&get(out+4)==1);
        if(get(out+8)==0x12){
            CHECK(n==(int)p->size+16);p->last=get(out+20)|(uint64_t)get(out+24)<<32;
            unsigned op=p->last&255,port=(p->last>>16)&127;
            CHECK(op==4||op==6||op==7||op==0x12||(q->initialize_ppm&&(op==1||op==5)));
            if(op==1){CHECK(p->last==1);p->cci=UINT32_C(1)<<27;p->resets++;}
            else if(op==5){CHECK(p->last==5||p->last==(UINT64_C(5)|(UINT64_C(1)<<16)|(UINT64_C(1)<<31)));p->cci=UINT32_C(1)<<31;p->notifies++;}
            else if(op==4){CHECK(p->last==(UINT64_C(4)|(UINT64_C(1)<<17)));p->cci=UINT32_C(1)<<29;p->acks++;}
            else {p->commands++;if(op!=6)CHECK(port==1||port==2);p->cci=(UINT32_C(1)<<31)|((op==6?16:op==7?2:p->version>=0x200?11:9)<<8);}
            memset(in,0,16);put(in,32779);put(in+4,1);put(in+8,0x12);
            CHECK(elden_ucsi_receive(q,in,16)==EldenUcsiProgress);
        }else{
            CHECK(get(out+8)==0x11&&n==12);response(p,in);int result=elden_ucsi_receive(q,in,16+p->size);
            CHECK(result==EldenUcsiProgress||result==EldenUcsiCycle);
        }
    }
    CHECK(q->phase==EldenUcsiReady);
}
static void valid(uint16_t version,unsigned size,uint32_t initial){
    struct ppm p={0};p.size=size;p.version=version;p.cci=initial;
    p.flags[0]=(1u<<19)|(1u<<21)|(1u<<29)|(1u<<16);p.flags[1]=(1u<<19)|(4u<<29)|(3u<<16);
    p.rdo[0]=0x12345678;p.rdo[1]=0x23456789;
    EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_link(&q,true);run(&q,&p);
    CHECK(q.cycles==1&&q.committed.connectors==2&&q.committed.version==version&&q.committed.epoch==1);
    CHECK(q.committed.port[0].status_valid&&q.committed.port[1].status_valid);
    CHECK(q.committed.port[0].flags!=q.committed.port[1].flags&&q.committed.port[1].rdo==0x23456789);
    CHECK(q.committed.port[0].usb&&!q.committed.port[1].usb&&q.committed.port[1].power_mode==3);
    CHECK(q.committed.port[0].sink_path_known==(version>=0x200));
    if(version>=0x200)CHECK(q.committed.port[0].reversed&&q.committed.port[1].sink_path_enabled);
    CHECK(p.commands==5&&p.acks==5+(initial!=0));
    p.flags[1]=0;elden_ucsi_refresh(&q);run(&q,&p);CHECK(q.cycles==2&&!q.committed.port[1].connected);
    uint8_t out[544];elden_ucsi_refresh(&q);CHECK(elden_ucsi_next(&q,out,sizeof(out))>0);
    elden_ucsi_timeout(&q);CHECK(q.phase==EldenUcsiFailed&&!q.committed.connectors);
    elden_ucsi_link(&q,true);CHECK(q.phase==EldenUcsiFailed);
    elden_ucsi_link(&q,false);elden_ucsi_link(&q,true);CHECK(q.phase==EldenUcsiVersion&&q.epoch==2);
}
static void initialized(uint32_t initial){
    struct ppm p={0};p.size=48;p.version=0x110;p.cci=initial;
    p.flags[0]=(1u<<19)|(1u<<21)|(1u<<16);p.flags[1]=(1u<<19)|(3u<<16);
    EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_initialize_ppm(&q,true);elden_ucsi_link(&q,true);
    run(&q,&p);
    CHECK(p.resets==1&&p.notifies==(initial?2u:1u)&&p.acks==6&&p.commands==5);
    CHECK(q.committed.connectors==2&&q.committed.port[1].power_mode==3);
    elden_ucsi_refresh(&q);run(&q,&p);CHECK(p.resets==1&&p.acks==11);
    elden_ucsi_link(&q,false);elden_ucsi_link(&q,true);p.cci=0;run(&q,&p);CHECK(p.resets==2);
}
int main(void){
    initialized(0);initialized(UINT32_C(1)<<27);
    valid(0x110,48,0);valid(0x120,48,UINT32_C(1)<<31);valid(0x200,528,0);valid(0x210,528,0);
    struct ppm p={0};p.size=48;p.version=0x110;uint8_t in[544],out[544];response(&p,in);
    for(unsigned n=12;n<64;n++){
        EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_link(&q,true);CHECK(elden_ucsi_next(&q,out,sizeof(out))==12);
        CHECK(elden_ucsi_receive(&q,in,n)==EldenUcsiStopped&&q.phase==EldenUcsiFailed);
    }
    EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_link(&q,true);memset(out,0xa5,sizeof(out));
    CHECK(elden_ucsi_next(&q,out,1)==-2&&!q.pending&&out[0]==0xa5);
    CHECK(elden_ucsi_next(&q,out,sizeof(out))==12);put(in+60,5);
    CHECK(elden_ucsi_receive(&q,in,64)==EldenUcsiStopped&&q.firmware_error==5);
    elden_ucsi_link(&q,false);elden_ucsi_link(&q,true);CHECK(elden_ucsi_next(&q,out,sizeof(out))==12);
    response(&p,in);put(in+4,2);CHECK(elden_ucsi_receive(&q,in,64)==0&&q.pending);
    put(in+4,1);put(in+12,0x200);CHECK(elden_ucsi_receive(&q,in,64)==EldenUcsiStopped);
    elden_ucsi_link(&q,false);elden_ucsi_link(&q,true);CHECK(elden_ucsi_next(&q,out,sizeof(out))==12);
    response(&p,in);put(in+16,(UINT32_C(1)<<28)|(UINT32_C(1)<<31)|(2u<<1));
    CHECK(elden_ucsi_receive(&q,in,64)==EldenUcsiProgress&&q.polling&&!q.committed.connectors);
    for(unsigned i=0;i<251;i++){q.polling=true;elden_ucsi_tick(&q);}CHECK(q.phase==EldenUcsiFailed);
    /* The two owners share one GLINK channel; foreign replies cannot finish
     * the other owner's pending query, and a UCSI failure does not poison BMS. */
    EldenChargePoll battery;elden_charge_poll_init(&battery,false);elden_charge_poll_link(&battery,true);
    CHECK(elden_charge_poll_next(&battery,out)==24);elden_ucsi_init(&q);elden_ucsi_link(&q,true);
    uint8_t ucsi_out[544];CHECK(elden_ucsi_next(&q,ucsi_out,sizeof(ucsi_out))==12);
    response(&p,in);CHECK(elden_charge_poll_receive(&battery,in,64)==0&&battery.pending);
    uint8_t batt_reply[24]={0};put(batt_reply,32778);put(batt_reply+4,1);put(batt_reply+8,0x30);put(batt_reply+16,1);
    CHECK(elden_ucsi_receive(&q,batt_reply,24)==0&&q.pending);
    put(in+60,7);CHECK(elden_ucsi_receive(&q,in,64)==EldenUcsiStopped);
    CHECK(elden_charge_poll_receive(&battery,batt_reply,24)==EldenChargePollAnswer&&!battery.quarantined);
    elden_ucsi_init(&q);elden_ucsi_initialize_ppm(&q,true);elden_ucsi_link(&q,true);
    CHECK(elden_ucsi_next(&q,out,sizeof(out))==12);response(&p,in);
    CHECK(elden_ucsi_receive(&q,in,64)==EldenUcsiProgress&&q.phase==EldenUcsiResetWrite);
    CHECK(elden_ucsi_next(&q,out,sizeof(out))==64&&get(out+20)==1);
    elden_ucsi_timeout(&q);CHECK(q.failure==EldenUcsiFailureTimeout&&q.failed_phase==EldenUcsiResetWrite);
    elden_ucsi_link(&q,true);CHECK(q.phase==EldenUcsiFailed); /* no blind reset retry */
    elden_ucsi_init(&q);elden_ucsi_link(&q,true);
    uint8_t event[24]={0};put(event,32779);put(event+4,2);put(event+8,0x13);
    for(unsigned i=0;i<32;++i) {
        put(event+12,(i&1)?UINT32_C(1)<<31:UINT32_C(1)<<29);
        CHECK(elden_ucsi_receive(&q,event,sizeof(event))==EldenUcsiNotification);
        CHECK(!q.refresh_wanted&&q.phase==EldenUcsiVersion&&!q.pending);
    }
    put(event+12,(UINT32_C(1)<<28)|2);elden_ucsi_receive(&q,event,sizeof(event));CHECK(!q.refresh_wanted);
    put(event+12,6);elden_ucsi_receive(&q,event,sizeof(event));CHECK(!q.refresh_wanted);
    put(event+12,4);elden_ucsi_receive(&q,event,sizeof(event));CHECK(q.refresh_wanted);
    printf("PASS: %u checks; v1 opt-in PPM init, legacy/v2 queries, exact errors/epochs, no role/sink writes\n",checks);
    return 0;
}
