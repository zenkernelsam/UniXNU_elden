/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UcsiQuery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void notification(EldenUcsiQuery *q,uint32_t cci){uint8_t p[24]={0};put(p,32779);put(p+4,2);put(p+8,0x13);put(p+12,cci);CHECK(elden_ucsi_receive(q,p,24)==EldenUcsiNotification);}
static void write_done(EldenUcsiQuery *q){uint8_t p[16]={0};put(p,32779);put(p+4,1);put(p+8,0x12);CHECK(elden_ucsi_receive(q,p,16)==EldenUcsiProgress);}
static void read_reply(uint8_t p[64],uint32_t cci){memset(p,0,64);put(p,32779);put(p+4,1);put(p+8,0x11);put(p+12,0x110);put(p+16,cci);}
typedef struct {uint64_t command;uint32_t cci;unsigned active,full,basic,reset,acks,status[2],new_event;} Ppm;
static void drive(EldenUcsiQuery *q,Ppm *m){
    uint8_t request[544],reply[64];
    for(unsigned limit=0;q->phase!=EldenUcsiReady&&q->phase!=EldenUcsiFailed&&limit<300;limit++){
        int n=elden_ucsi_next(q,request,sizeof(request));CHECK(n>0);
        if(get(request+8)==0x12){
            uint64_t command=get(request+20)|(uint64_t)get(request+24)<<32;unsigned op=command&255,port=(command>>16)&127;
            CHECK(n==64&&(op==1||op==4||op==5||op==6||op==7||op==0x12||op==0x10));
            if(op==1){m->cci=1u<<27;++m->reset;}
            else if(op==5){
                CHECK(command==0x80010005u||command==elden_ucsi_event_control(q));
                if(command!=0x80010005u){CHECK(m->status[0]&&m->status[1]&&q->cycles==0);CHECK(q->notification_control==command);++m->full;m->active=2;notification(q,4);}
                else ++m->basic;
                m->cci=1u<<31;
            }else if(op==4){
                CHECK(command==0x20004u||command==0x30004u);
                if(command==0x30004u){
                    CHECK((m->command&255)==0x12&&((m->command>>16)&127)==m->active&&m->active);
                    ++m->acks;m->active=0;
                    if(!m->new_event++){m->active=1;notification(q,2);} /* new event while old ACK is in flight */
                }
                m->cci=1u<<29;
            }else {
                if(op==0x12){CHECK(port==1||port==2);++m->status[port-1];}
                m->cci=(1u<<31)|((op==6?16u:op==7?2u:op==0x10?8u:9u)<<8);
            }
            m->command=command;write_done(q);
        }else{
            CHECK(n==12&&get(request+8)==0x11);read_reply(reply,m->cci|(m->active<<1));
            unsigned op=m->command&255,port=(m->command>>16)&127;
            if(op==6){reply[32]=2;} /* features0: exact stock quirk */
            if(op==7)put(reply+28,0x3fe7);
            if(op==0x12){put(reply+28,(1u<<19)|(port==1?(1u<<16)|(1u<<21):(3u<<16)));put(reply+32,port==1?0:0x200378de);}
            if(op==0x10){put(reply+28,0x0a01912c);put(reply+32,0x0002d0de);}
            int result=elden_ucsi_receive(q,reply,64);CHECK(result==EldenUcsiProgress||result==EldenUcsiCycle);
        }
    }
    CHECK(q->phase==EldenUcsiReady);
}
static EldenUcsiQuery status_query(unsigned query,unsigned active){
    EldenUcsiQuery q;elden_ucsi_init(&q);elden_ucsi_initialize_ppm(&q,true);elden_ucsi_enable_events(&q,true);elden_ucsi_link(&q,true);
    q.phase=EldenUcsiQueryRead;q.pending=true;q.buffer_bytes=48;q.working.version=0x110;q.events_ready=true;q.query=query;q.active_connector=(uint8_t)active;return q;
}
int main(void){
    EldenUcsiQuery mask={0};CHECK(elden_ucsi_event_control(&mask)==0xda050005u);
    mask.query_pdos=true;mask.working.version=0x110;mask.working.connectors=2;
    CHECK(elden_ucsi_event_control(&mask)==0xda650005u&&mask.working.features==0);
    mask.query_pdos=false;CHECK(elden_ucsi_event_control(&mask)==0xda050005u);
    mask.query_pdos=true;mask.working.version=0x200;CHECK(elden_ucsi_event_control(&mask)==0xda050005u);
    mask.working.features=1u<<4;CHECK(elden_ucsi_event_control(&mask)==0xda650005u);
    mask.working.features=0;mask.working.version=0x110;mask.working.connectors=1;CHECK(elden_ucsi_event_control(&mask)==0xda050005u);
    EldenUcsiQuery q;Ppm m={0};elden_ucsi_init(&q);elden_ucsi_initialize_ppm(&q,true);elden_ucsi_enable_events(&q,true);elden_ucsi_enable_pdos(&q,true);elden_ucsi_link(&q,true);
    drive(&q,&m);CHECK(m.full==1&&m.basic==1&&m.reset==1&&m.acks==2&&q.connector_acks==2&&q.events_ready);
    CHECK(!q.committed.port[1].pdos_consistent); /* connector events during initial cycle */
    elden_ucsi_refresh(&q);drive(&q,&m);CHECK(m.full==1&&m.reset==1&&q.committed.port[1].pdos_consistent);
    q.refresh_wanted=false;
    for(unsigned i=0;i<64;i++)notification(&q,(i&1)?1u<<31:1u<<29);
    CHECK(!q.refresh_wanted&&!q.active_connector); /* no completion feedback loop */
    uint8_t reply[64],request[544];
    q=status_query(2,1);read_reply(reply,(1u<<31)|(9u<<8)|4);put(reply+28,1u<<19);
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&!q.ack_connector&&q.active_connector==2);
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==64&&get(request+20)==0x20004u); /* never ACK other connector */
    q=status_query(2,1);read_reply(reply,(1u<<31)|(9u<<8)|2);put(reply+28,1u<<19);
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&q.ack_connector==1);
    notification(&q,4); /* different event between status capture and send */
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==64&&get(request+20)==0x20004u&&q.active_connector==2&&!q.ack_connector);
    q=status_query(2,1);read_reply(reply,(1u<<31)|(9u<<8));put(reply+28,1u<<19);
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&q.ack_connector==1);
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==64&&get(request+20)==0x30004u&&!q.active_connector);
    elden_ucsi_unsent(&q);CHECK(q.active_connector==1&&!q.pending);
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==64&&!q.active_connector);
    notification(&q,4);write_done(&q);CHECK(elden_ucsi_next(&q,request,sizeof(request))==12);read_reply(reply,1u<<29);
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&q.active_connector==2&&q.connector_acks==1);
    for(unsigned bad=0;bad<2;bad++){
        q=status_query(2,1);read_reply(reply,(1u<<31)|((bad?9u:8u)<<8)|(bad?1u<<30:0));
        CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&q.command_error&&!q.ack_connector);
        CHECK(elden_ucsi_next(&q,request,sizeof(request))==64&&get(request+20)==0x20004u);
    }
    /* Full-notification failure at query index5 is not an optional PDO failure. */
    q=status_query(5,0);q.events_ready=false;q.phase=EldenUcsiEventsRead;read_reply(reply,(1u<<31)|(1u<<25));
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiProgress&&q.events_ack);
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==64);write_done(&q);
    CHECK(elden_ucsi_next(&q,request,sizeof(request))==12);read_reply(reply,1u<<29);
    CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiStopped&&!q.events_ready);
    elden_ucsi_link(&q,true);CHECK(q.phase==EldenUcsiFailed);elden_ucsi_link(&q,false);elden_ucsi_link(&q,true);CHECK(q.phase==EldenUcsiVersion&&q.connector_events&&!q.events_ready);
    /* Notification after the last PDO/status check still invalidates publication. */
    q=status_query(10,0);q.phase=EldenUcsiAckRead;q.working.port[0].pdos_consistent=q.working.port[1].pdos_consistent=true;
    notification(&q,2);read_reply(reply,1u<<29);CHECK(elden_ucsi_receive(&q,reply,64)==EldenUcsiCycle);
    CHECK(!q.committed.port[0].pdos_consistent&&!q.committed.port[1].pdos_consistent);
    q=status_query(2,1);notification(&q,(1u<<28)|4);CHECK(q.active_connector==1);
    printf("UcsiEvents: %u checks; mandatory notify, matching ACK, queued events, late invalidation, no role/voltage SET\n",checks);
}
