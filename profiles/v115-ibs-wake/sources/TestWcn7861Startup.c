/* SPDX-License-Identifier: GPL-2.0-only */
#include "Wcn7861Startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"%d %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint8_t queue[1024];size_t used,pos;uint64_t time;
    const uint8_t *fw[3];size_t size[3],offset[3];EldenWcnPeripheralPlan plan[3];
    unsigned writes,versions,baud_calls,reset_calls,activate_calls,flow_calls,wakes;
    unsigned fault;bool patched,absent_tmel,stuck_clock;
} Model;
static void push(Model *m,const uint8_t *p,size_t n){
    if(m->used==m->pos)m->used=m->pos=0;
    CHECK(n<=sizeof(m->queue)-m->used);memcpy(m->queue+m->used,p,n);m->used+=n;
}
static void complete(Model *m,uint16_t opcode,uint8_t sub,uint8_t ss,uint8_t status){
    uint8_t p[25]={0x34,0,0xff,8,0xf0,1,0,(uint8_t)opcode,(uint8_t)(opcode>>8),status,sub,ss};
    size_t n=sub==5?25:12;p[3]=(uint8_t)(n-4);
    if(sub==5){p[12]=0x21;p[18]=0;p[19]=2;p[20]=0;p[21]=2;p[22]=0x21;p[23]=0x40;if(m->fault==2)p[23]^=1;}
    if(m->fault==3&&sub==2)p[9]=0;
    if(m->fault==6&&sub==6)p[11]^=1;
    push(m,p,n);
}
static void notify(Model *m,unsigned kind){
    uint8_t p[8]={0x34,0,0xff,4,0xf0,3,0,0};
    if(kind==2)p[7]=3;
    if(kind==3){p[5]=2;p[6]=1;p[7]=1;}
    push(m,p,sizeof(p));
}
static int write(void *context,const uint8_t *p,size_t n,unsigned timeout){
    Model *m=context;CHECK(timeout==2000);m->writes++;
    if(m->fault==8)return -5;
    if(n==1){CHECK(p[0]==0xfd||p[0]==0xfc);if(p[0]==0xfd){m->wakes++;if(m->fault!=1&&(m->fault<12||m->wakes>=m->fault-10)){uint8_t ack=0xfc;push(m,&ack,1);}}return 0;}
    CHECK(n>=6&&p[0]==0x31&&p[1]==0&&n==(size_t)p[4]+5);
    uint16_t opcode=p[2]|(uint16_t)p[3]<<8;CHECK(opcode==0xfff0||opcode==0xfff1);
    if(p[5]==6){
        CHECK(n>=9&&p[7]==n-8&&p[7]<=243);unsigned file=p[6]==3?2:0;
        if(file==0&&m->offset[0]==m->size[0])file=1;
        CHECK(m->offset[file]+n-8<=m->size[file]);
        for(size_t i=0;i<n-8;i++){
            size_t at=m->offset[file]+i;uint8_t expected=m->fw[file][at];
            if(file==1&&at==m->plan[1].baud_byte)expected=0x15;
            if(file==1&&at==m->plan[1].ibs_byte)expected|=1;
            CHECK(p[8+i]==expected);
        }
        m->offset[file]+=n-8;
        if(file==1||m->offset[file]==m->size[file])complete(m,opcode,6,p[6],0);
        if(file==2&&m->offset[file]==m->size[file]&&m->fault!=5)notify(m,2);
        return 0;
    }
    switch(p[5]){
      case 5:CHECK(n==7);m->versions++;complete(m,opcode,5,p[6],0);break;
      case 2:CHECK(n==7&&p[6]==0x15);complete(m,opcode,2,0,1);break;
      case 8:CHECK(n==8&&p[7]==0);complete(m,opcode,8,p[6],p[6]==3?(m->absent_tmel?5:m->patched?23:0):m->patched?23:0);break;
      case 3:CHECK(n==6);m->reset_calls++;complete(m,opcode,3,0,0);if(m->fault!=4)notify(m,1);break;
      case 0:{
        CHECK(n==8&&p[6]==1&&p[7]==1);m->activate_calls++;
        uint8_t r[10]={0x34,0,0xff,6,0xf0,0,0,0,0xf1,0xff};if(m->patched)r[6]=23;
        push(m,r,sizeof(r));if(!m->patched&&m->fault!=7)notify(m,3);break;
      }
      default:CHECK(false);
    }
    return 0;
}
static size_t read(void *context,uint8_t *out,size_t capacity){
    Model *m=context;size_t n=m->used-m->pos;if(n>3)n=3;if(n>capacity)n=capacity;
    memcpy(out,m->queue+m->pos,n);m->pos+=n;return n;
}
static uint64_t now(void *context){return ((Model *)context)->time;}
static void delay(void *context,unsigned ms){Model *m=context;if(!m->stuck_clock)m->time+=ms;}
static int flow(void *context,bool ready){Model *m=context;m->flow_calls++;CHECK(ready==(m->flow_calls==2));return 0;}
static int baud(void *context,uint32_t value){Model *m=context;m->baud_calls++;CHECK(value==8000000);return 0;}
static uint8_t *load(const char *path,size_t *n){FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>0);rewind(f);uint8_t *p=malloc(size);CHECK(p);CHECK(fread(p,1,size,f)==(size_t)size);fclose(f);*n=size;return p;}
static void prepare(Model *m,EldenWcnStartup *s,uint8_t *fw[3],size_t bytes[3]){
    for(unsigned i=0;i<3;i++){m->fw[i]=fw[i];m->size[i]=bytes[i];CHECK(!elden_wcn_peripheral_prepare((enum EldenWcnPeripheralFirmware)i,fw[i],bytes[i],&m->plan[i]));}
    EldenWcnStartupIo io={m,write,read,now,delay,flow,baud,NULL};
    CHECK(!elden_wcn_startup_prepare(s,&io,fw[0],bytes[0],fw[1],bytes[1],fw[2],bytes[2]));CHECK(!m->writes);
}
int main(int argc,char **argv){
    CHECK(argc==4);uint8_t *fw[3];size_t bytes[3];for(unsigned i=0;i<3;i++)fw[i]=load(argv[i+1],&bytes[i]);
    for(unsigned test=0;test<21;test++){
        Model m={0};EldenWcnStartup s={0};m.fault=test<=8||test>=12?test:0;m.patched=test==9;m.absent_tmel=test==10;m.stuck_clock=test==11;
        prepare(&m,&s,fw,bytes);
        if(test==11)m.fault=1;
        int result=elden_wcn_startup_run(&s);
        bool success=test==0||test==9||test==10||test>=12;
        if(success){CHECK(!result&&s.ready&&!s.poisoned);CHECK(m.baud_calls==1&&s.baud==8000000);CHECK(m.activate_calls==1);
            if(test==0)CHECK(m.offset[0]==bytes[0]&&m.offset[1]==bytes[1]&&m.offset[2]==bytes[2]);
            if(test==9)CHECK(!m.offset[0]&&!m.offset[1]&&!m.offset[2]);
            if(test==10)CHECK(!m.offset[2]&&m.versions==1);
            if(test>=12)CHECK(m.wakes>=test-10&&m.wakes<=test-8);
        }else{
            CHECK(result&&s.poisoned&&!s.ready);unsigned writes=m.writes;
            if(test==1)CHECK(m.wakes==10&&m.versions==0&&m.time==100);
            if(test==11)CHECK(m.wakes==1&&m.versions==0);
            CHECK(elden_wcn_startup_run(&s)<0&&m.writes==writes);
            EldenWcnStartupIo io=s.io;CHECK(elden_wcn_startup_prepare(&s,&io,fw[0],bytes[0],fw[1],bytes[1],fw[2],bytes[2])<0&&m.writes==writes);
        }
    }
    for(unsigned i=0;i<3;i++)free(fw[i]);
    printf("PASS %u actual three-firmware startup/framing/status/notify/timeout checks\n",checks);return 0;
}
