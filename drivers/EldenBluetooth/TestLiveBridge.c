/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BtBridge.h"
#include "BtTlv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"%d %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    PbBridge bridge;PbBridgeIo io;uint64_t now;
    uint8_t rx[128],socket[128];unsigned rx_bytes,rx_pos,socket_bytes,writes;
    uint16_t last_opcode;bool locked,overflow;
} Model;
static void lock(void *p,int take){Model *m=p;CHECK(take?!m->locked:m->locked);m->locked=take;}
static uint64_t now(void *p){return ((Model *)p)->now;}
static int write(void *p,const uint8_t *b,uint32_t n,unsigned timeout){
    Model *m=p;CHECK(!m->locked&&timeout==1000&&n>=1);m->writes++;
    if(b[0]==1){CHECK(n>=4);m->last_opcode=b[1]|(uint16_t)b[2]<<8;}
    return 0;
}
static uint32_t read(void *p,uint8_t *out,uint32_t max){
    Model *m=p;CHECK(!m->locked);
    if(m->overflow)return max+1;
    unsigned n=m->rx_bytes-m->rx_pos;if(n>max)n=max;
    memcpy(out,m->rx+m->rx_pos,n);m->rx_pos+=n;return n;
}
static int put(void *p,uint32_t unit,const uint8_t *b,uint32_t n){
    Model *m=p;CHECK(m->locked&&unit==1&&m->socket_bytes+n<=sizeof(m->socket));
    memcpy(m->socket+m->socket_bytes,b,n);m->socket_bytes+=n;return 0;
}
static void kick(void *p){CHECK(((Model *)p)->locked);}
static void room(void *p,unsigned ms){Model *m=p;CHECK(m->locked);m->now+=ms;}
static void io_log(void *p,const char *line){CHECK(!((Model *)p)->locked&&line);}
static void response(Model *m,uint16_t opcode,uint8_t status){
    const uint8_t reply[]={4,14,4,1,(uint8_t)opcode,(uint8_t)(opcode>>8),status};
    memcpy(m->rx,reply,sizeof(reply));m->rx_bytes=sizeof(reply);m->rx_pos=0;
}
static void command(Model *m,uint16_t opcode){
    const uint8_t packet[]={1,(uint8_t)opcode,(uint8_t)(opcode>>8),0};
    pb_bridge_write(&m->bridge,1,packet,sizeof(packet));
}
static void open(Model *m){
    m->io=(PbBridgeIo){m,lock,now,write,read,put,kick,room,kick,io_log};
    const uint8_t address[]={1,2,3,4,5,6};pb_bridge_init(&m->bridge,&m->io,address,0);
    pb_bridge_up(&m->bridge,NULL,NULL,0);CHECK(!pb_bridge_connect(&m->bridge,1,42));
}
int main(void){
    {
        Model m={0};open(&m);command(&m,0x0c5b);CHECK(pb_bridge_step(&m.bridge)>=0);
        CHECK(m.bridge.pending==1&&!m.bridge.ready);
        response(&m,0x1009,0);CHECK(pb_bridge_step(&m.bridge)>=0&&m.bridge.pending==1&&!m.bridge.ready&&!m.socket_bytes);
        response(&m,0x0c5b,1);CHECK(pb_bridge_step(&m.bridge)>=0&&!m.bridge.pending&&!m.bridge.ready&&m.socket_bytes==7);
        command(&m,0x0c5b);CHECK(pb_bridge_step(&m.bridge)>=0);
        response(&m,0x0c5b,0);CHECK(pb_bridge_step(&m.bridge)>=0&&m.bridge.ready);
    }
    {
        Model m={0};open(&m);command(&m,0x1001);command(&m,0x1009);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.writes==1&&m.last_opcode==0x1001);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.writes==1);
        response(&m,0x1001,0);CHECK(pb_bridge_step(&m.bridge)>=0);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.writes==2&&m.last_opcode==0x1009);
    }
    {
        Model m={0};open(&m);pb_bridge_disconnect(&m.bridge,1);
        CHECK(pb_bridge_connect(&m.bridge,1,43)==PbBridgeBusy);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.last_opcode==0x0c03);
        response(&m,0x0c03,0);CHECK(pb_bridge_step(&m.bridge)>=0);
        CHECK(!pb_bridge_connect(&m.bridge,1,43));
    }
    {
        Model m={0};open(&m);m.overflow=true;
        CHECK(pb_bridge_step(&m.bridge)==PbBridgeStop&&m.bridge.guard==PbGuardWrite);
    }
    {
        Model m={0};open(&m);command(&m,0xfc14);
        CHECK(!m.writes&&m.socket_bytes==7&&m.socket[6]==1);
    }
    {
        Model m={0};open(&m);command(&m,0x1001);CHECK(pb_bridge_step(&m.bridge)>=0);
        m.now=4000;CHECK(pb_bridge_step(&m.bridge)==PbBridgeStop&&m.bridge.guard==PbGuardCommand);
    }
    /* A complete H4 frame can still contain a truncated HCI response. Missing
     * status must not become a successful initialization/reset completion. */
    for(unsigned test=0;test<4;test++){
        Model m={0};open(&m);command(&m,test==1?0x0c03:0x0c5b);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.bridge.pending==1);
        const uint8_t missing_status[]={4,14,3,1,test==1?3:0x5b,0x0c};
        const uint8_t short_complete[]={4,14,2,1,0x5b};
        const uint8_t short_status[]={4,15,3,0,1,0x5b};
        const uint8_t *reply=test==2?short_complete:test==3?short_status:missing_status;
        unsigned bytes=test==2?sizeof(short_complete):test==3?sizeof(short_status):sizeof(missing_status);
        memcpy(m.rx,reply,bytes);m.rx_bytes=bytes;
        int result=pb_bridge_step(&m.bridge);
        if(result!=PbBridgeStop)fprintf(stderr,"short response case=%u result=%d ready=%u pending=%u guard=%u\n",
            test,result,m.bridge.ready,m.bridge.pending,m.bridge.guard);
        CHECK(result==PbBridgeStop&&m.bridge.guard==PbGuardCommand);
        CHECK(!m.bridge.ready&&!m.socket_bytes);
    }
    {
        Model m={0};open(&m);command(&m,0x0c5b);CHECK(pb_bridge_step(&m.bridge)>=0);
        /* Opcode zero has no command result: valid credit-only notification. */
        const uint8_t credit[]={4,14,3,1,0,0};memcpy(m.rx,credit,sizeof(credit));m.rx_bytes=sizeof(credit);
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.bridge.pending==1&&!m.bridge.ready);
        CHECK(m.socket_bytes==sizeof(credit)&&!memcmp(m.socket,credit,sizeof(credit)));
        /* Ordinary fragmentation is not a truncated completed H4 frame. */
        const uint8_t complete[]={4,14,4,1,0x5b,0x0c,0};
        memcpy(m.rx,complete,4);m.rx_pos=0;m.rx_bytes=4;
        CHECK(pb_bridge_step(&m.bridge)>=0&&m.bridge.pending==1&&!m.bridge.ready);
        memcpy(m.rx,complete+4,3);m.rx_pos=0;m.rx_bytes=3;
        CHECK(pb_bridge_step(&m.bridge)>=0&&!m.bridge.pending&&m.bridge.ready);
        CHECK(m.socket_bytes==sizeof(credit)+sizeof(complete));
    }
    printf("PASS 11 live bridge scenarios, %u command/reply/generation/bounds assertions\n",checks);
}
