/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual CCI code with independently decoded command/byte/MMIO peers. */
#include "Cci.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {Reset=4,Start=8,Halt=0x34,Status=0xc0c,Mask=0xc04,Clear=0xc08,GlobalClear=0xc00};
typedef struct {
    EldenCci core;uint32_t reg[0x1000/4],queue[2][16];unsigned queue_count[2];
    uint8_t response[12],written[11];unsigned read_len,written_len,slave,read_at,ops,fail_at,writes,starts,reset_count,halt_count,masked,drains;
    uint32_t status_error;uint64_t now;unsigned controller;int deny,stall_reset,stall_halt,stall_queue,wrong_level,drop_write,reenter,deny_after,clock_back;
    unsigned sensor_model,sensor_register,sensor_reads,sensor_writes,stream_enabled,group_hold,data_port_writes;
} CciPeer;
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int admitted(void *v,uint64_t owner,uint64_t epoch,unsigned controller,unsigned operation){
    CciPeer *p=v;CHECK(owner==11&&epoch==7&&controller==p->controller&&operation<=EldenCciCleanup);
    if(operation!=EldenCciCleanup&&(p->deny||(p->deny_after&&p->starts)))return -13;return 0;
}
static int read_reg(void *v,uint32_t reg,uint32_t *out){CciPeer *p=v;if(++p->ops==p->fail_at)return -5;
    CHECK(!(reg&3)&&reg<0x1000);
    if(reg==0x118){
        unsigned offset=p->read_at*4;uint32_t value=0;
        for(unsigned i=0;i<4;i++){
            unsigned byte=offset+i;if(byte&&byte-1<p->read_len)value|=(uint32_t)p->response[byte-1]<<(i*8);
        }
        p->read_at++;if(p->reg[0x11c/4])p->reg[0x11c/4]--;*out=value;return 0;
    }
    *out=p->reg[reg/4];return 0;
}
static int run_queue(CciPeer *p,unsigned queue){
    unsigned n=p->queue_count[queue];CHECK(n==p->reg[(0x300+0x100*queue)/4]&&n>0&&n<=16);
    CHECK((p->queue[queue][0]&15)==1&&((p->queue[queue][0]>>4)&127)==p->slave&&(p->queue[queue][0]>>11)==0);
    if(!queue){
        CHECK((p->queue[0][1]&15)==9);unsigned len=(p->queue[0][1]>>4)&15;CHECK(len&&len<=11);
        CHECK(n==2+(len+1+3)/4&&p->queue[0][n-1]==0x108);
        for(unsigned i=0;i<len;i++){
            unsigned offset=i+1;p->written[i]=(uint8_t)(p->queue[0][1+offset/4]>>(8*(offset%4)));
        }
        p->written_len=len;
        if(p->sensor_model){
            CHECK(len==2||len==3||len==4);unsigned address=((unsigned)p->written[0]<<8)|p->written[1];p->sensor_register=address;
            if(len>2){unsigned value=len==4?((unsigned)p->written[2]<<8)|p->written[3]:p->written[2];p->sensor_writes++;
                if(address==0x0100)p->stream_enabled=value!=0;
                if(address==0x0104)p->group_hold=value;
                if(address==0x6f12)p->data_port_writes++;
            }
        }
    }else{CHECK(n==2&&(p->queue[1][1]&15)==10);p->read_len=p->queue[1][1]>>4;CHECK(p->read_len>=1&&p->read_len<=12);
        if(p->sensor_model){
            CHECK(p->read_len==2&&p->sensor_register==(p->controller?0x03f0:0));p->sensor_reads++;
            unsigned id=p->controller?0x08a8:0x38ee;p->response[0]=(uint8_t)(id>>8);p->response[1]=(uint8_t)id;
        }
        p->read_at=0;p->reg[0x11c/4]=p->wrong_level?0:p->read_len/4+1;
    }
    p->queue_count[queue]=0;p->reg[(0x304+0x100*queue)/4]=0;p->starts++;
    if(!p->stall_queue)p->reg[Status/4]|=p->status_error?p->status_error:queue?1:16;
    return 0;
}
static int write_reg(void *v,uint32_t reg,uint32_t value){CciPeer *p=v;if(++p->ops==p->fail_at)return -5;CHECK(!(reg&3)&&reg<0x1000);p->writes++;
    if(p->reenter){p->reenter=0;uint8_t b=1;CHECK(elden_camera_cci_write(&p->core,p->slave,&b,1)==-16);}
    if(p->drop_write)return 0;
    if(reg==Clear){p->reg[Status/4]&=~value;return 0;}
    if(reg==GlobalClear){CHECK(value==1);return 0;}
    if(reg==Reset){
        CHECK(value==0x0f73f3f7||value==0x3f1);p->reset_count++;
        for(unsigned q=0;q<2;q++){p->queue_count[q]=0;p->reg[(0x304+q*0x100)/4]=0;}
        p->reg[0x11c/4]=0;if(!p->stall_reset)p->reg[Status/4]|=1u<<24;return 0;
    }
    if(reg==Halt){CHECK(value==1);p->halt_count++;if(!p->stall_halt)p->reg[Status/4]|=1u<<25;return 0;}
    if(reg==0x310||reg==0x410){unsigned q=reg==0x410;CHECK(p->queue_count[q]<16);
        p->queue[q][p->queue_count[q]++]=value;p->reg[(0x304+q*0x100)/4]=p->queue_count[q];return 0;}
    if(reg==Start){CHECK(value==1||value==2);return run_queue(p,value==2);}
    CHECK(reg==Mask||reg==0x300||reg==0x400||(reg>=0x100&&reg<=0x110)||(reg>=0x200&&reg<=0x210));
    p->reg[reg/4]=value;return 0;
}
static uint64_t now(void *v){CciPeer *p=v;if(p->clock_back){p->clock_back=0;return p->now-1;}return p->now;}
static int delay(void *v,unsigned us){CHECK(us==10);((CciPeer *)v)->now+=us;return 0;}
static int mask(void *v,int on){CHECK(on==1);((CciPeer *)v)->masked=1;return 0;}
static int drain(void *v){CciPeer *p=v;CHECK(p->masked);p->drains++;return 0;}
static void setup(CciPeer *p,unsigned controller){cases++;memset(p,0,sizeof(*p));p->controller=controller;p->slave=controller?0x31:0x56;p->now=1000;
    p->reg[0]=0x20050000;for(unsigned m=0;m<2;m++)for(unsigned i=0;i<5;i++)p->reg[(0x100+0x100*m+4*i)/4]=0x12340000+m*256+i;
    for(unsigned i=0;i<12;i++)p->response[i]=(uint8_t)(i*19+131);
}
static int init(CciPeer *p){EldenCciIo io={p,11,7,admitted,read_reg,write_reg,now,delay,mask,drain};return elden_camera_cci_init(&p->core,&io,p->controller,37500000);}
int main(void){
    CciPeer p;uint8_t input[11]={0xff,0x80,0x7f,0xa5,0x55,0xfe,0x99,0x02,0x00,0xd3,0x81},out[12],old[12];
    for(unsigned controller=0;controller<2;controller++){
        setup(&p,controller);uint32_t saved[2][5];for(unsigned m=0;m<2;m++)for(unsigned i=0;i<5;i++)saved[m][i]=p.reg[(0x100+0x100*m+4*i)/4];
        CHECK(!init(&p)&&p.reset_count==1&&p.masked);CHECK(p.reg[0x100/4]==0x00260038&&p.reg[0x104/4]==0x00280028&&p.reg[0x110/4]==0x63);
        for(unsigned n=1;n<=11;n++){CHECK(!elden_camera_cci_write(&p.core,p.slave,input,n));CHECK(p.written_len==n&&!memcmp(input,p.written,n));}
        for(unsigned n=1;n<=12;n++){memset(out,0xa5,sizeof(out));CHECK(!elden_camera_cci_read(&p.core,p.slave,out,n));CHECK(!memcmp(p.response,out,n));for(unsigned i=n;i<12;i++)CHECK(out[i]==0xa5);}
        CHECK(!elden_camera_cci_write_register(&p.core,p.slave,0x6f12,2,0x80ff)&&!memcmp(p.written,(uint8_t[]){0x6f,0x12,0x80,0xff},4));
        CHECK(!elden_camera_cci_write_register(&p.core,p.slave,0x6f12,2,0x0192)&&p.written[0]==0x6f&&p.written[1]==0x12);
        uint32_t value=0;CHECK(!elden_camera_cci_read_register(&p.core,p.slave,0x03f0,2,&value)&&value==((uint32_t)p.response[0]<<8|p.response[1])&&p.written_len==2&&p.written[0]==3&&p.written[1]==0xf0);
        unsigned before=p.writes;CHECK(elden_camera_cci_write_register(&p.core,p.slave,0,1,256)==-22&&p.writes==before);
        CHECK(elden_camera_cci_write(&p.core,0x80,input,1)==-22&&p.writes==before);
        p.reenter=1;CHECK(!elden_camera_cci_write(&p.core,p.slave,input,4));
        CHECK(!elden_camera_cci_stop(&p.core)&&p.halt_count==1&&p.reset_count==2);
        for(unsigned m=0;m<2;m++)for(unsigned i=0;i<5;i++)CHECK(p.reg[(0x100+0x100*m+4*i)/4]==saved[m][i]);
        EldenCciStatus s;CHECK(!elden_camera_cci_status(&p.core,&s)&&s.state==EldenCciStopped);
        unsigned written=p.writes;CHECK(elden_camera_cci_write(&p.core,p.slave,input,1)<0&&p.writes==written);
        CHECK(!elden_camera_cci_stop(&p.core)&&p.writes==written);
        CHECK(!elden_camera_cci_status(&p.core,&s)&&s.state==EldenCciStopped);
    }
    for(unsigned fault=0;fault<7;fault++){
        setup(&p,0);if(fault==0)p.deny=1;if(fault==1)p.reg[0x304/4]=1;if(fault==2)p.reg[0x504/4]=1;
        if(fault==3)p.reg[0x21c/4]=1;if(fault==4)p.reg[Mask/4]=1u<<12;if(fault==5)p.reg[0]=UINT32_MAX;if(fault==6)p.reg[Status/4]=1u<<12;
        CHECK(init(&p)<0&&!p.writes);CHECK(elden_camera_cci_stop(&p.core)<0&&!p.writes);
    }
    for(unsigned fault=0;fault<6;fault++){
        setup(&p,0);CHECK(!init(&p));memset(out,0xa5,sizeof(out));memcpy(old,out,sizeof(out));
        if(fault==0)p.status_error=1u<<27;if(fault==1)p.status_error=1u<<1;if(fault==2)p.status_error=1u<<12;
        if(fault==3)p.wrong_level=1;if(fault==4)p.stall_queue=1;if(fault==5)p.deny_after=1;
        CHECK(elden_camera_cci_read(&p.core,p.slave,out,2)<0&&!memcmp(out,old,sizeof(out)));
        unsigned written=p.writes;CHECK(elden_camera_cci_write(&p.core,p.slave,input,1)<0&&p.writes==written);
    }
    setup(&p,0);p.stall_reset=1;CHECK(init(&p)==-110&&p.now>=100000);
    setup(&p,0);CHECK(!init(&p));p.stall_halt=1;CHECK(elden_camera_cci_stop(&p.core)==-110&&p.reset_count==1);
    setup(&p,0);CHECK(!init(&p));p.drop_write=1;CHECK(elden_camera_cci_stop(&p.core)<0);
    // Fail every actual read/write position of init/register-read/stop once.
    setup(&p,0);CHECK(!init(&p));uint32_t value=0;CHECK(!elden_camera_cci_read_register(&p.core,p.slave,0,2,&value));CHECK(!elden_camera_cci_stop(&p.core));unsigned total=p.ops;
    for(unsigned failure=1;failure<=total;failure++){
        setup(&p,0);p.fail_at=failure;int rc=init(&p);
        if(!rc)rc=elden_camera_cci_read_register(&p.core,p.slave,0,2,&value);
        if(!rc)rc=elden_camera_cci_stop(&p.core);CHECK(rc==-5);
    }
    printf("PASS: %u actual CCI controller cases/%u checks; source commands/packing/read levels/two controllers/reentry/faults/atomic output/HALT-reset-restore; MMIO/IRQ/power modeled\n",cases,checks);return 0;
}
