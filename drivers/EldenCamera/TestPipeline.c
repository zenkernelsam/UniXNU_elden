/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual pipeline code; independent register/FIFO/frame-DMA/owner peer.
 * Test data are HOST ONLY and never production camera pixels/leases. */
#include "Pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,scenarios;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"line %u: %s\n",__LINE__,#v);exit(1);}}while(0)
typedef struct {
    EldenCameraPipeline core;EldenCameraPipelineConfig cfg;
    uint32_t phy[0x2000/4],csid[0x2b00/4],vfe[0x10000/4];uint64_t clock;
    unsigned reads,writes,ops,fail_at,reset,drains,buffers,completions;int deny,stall_reset,fail_drain,fail_buffer,clock_back;
    uint32_t addresses[32];unsigned address_count;
} Peer;
#ifdef ELDEN_CAMERA_PIPELINE_PEER_API
void *elden_camera_pipeline_peer_create(unsigned sensor,unsigned decoder);
int elden_camera_pipeline_peer_read(void *,unsigned,uint32_t,uint32_t *);
int elden_camera_pipeline_peer_write(void *,unsigned,uint32_t,uint32_t);
void elden_camera_pipeline_peer_event(void *,int,int,uint64_t,uint64_t);
uint64_t elden_camera_pipeline_peer_now(void *);
void elden_camera_pipeline_peer_delay(void *,unsigned);
void elden_camera_pipeline_peer_destroy(void *);
#endif
static uint32_t csid_top(Peer *p){return p->cfg.decoder>=3?0x7c:0x84;}
static uint32_t csid_clear(Peer *p){return p->cfg.decoder>=3?0x84:0x8c;}
static uint32_t buf_status(Peer *p){return p->cfg.decoder>=3?0x8c:0xa4;}
static uint32_t buf_clear(Peer *p){return p->cfg.decoder>=3?0x94:0xac;}
static uint32_t rdi_base(Peer *p){return p->cfg.decoder>=3?0x500+0x100*p->cfg.rdi:0xe00+0x200*p->cfg.rdi;}
static uint32_t rdi_irq(Peer *p){return (p->cfg.decoder>=3?0xec:0x114)+0x10*p->cfg.rdi;}
static uint32_t wm_cfg(Peer *p){return (p->cfg.decoder>=3?0x200:0x800)+0x500+0x100*(p->cfg.decoder>=3?p->cfg.rdi:23+p->cfg.rdi);}
static int gate(void *v,const EldenCameraPipelineConfig *c,unsigned op){Peer *p=v;CHECK(c->owner==7&&c->epoch==9&&c->route_epoch==11&&op<=3);return p->deny?-13:0;}
static int read_reg(void *v,unsigned block,uint32_t reg,uint32_t *out){Peer *p=v;if(++p->ops==p->fail_at)return -5;p->reads++;CHECK(block<3&&!(reg&3));
    uint32_t *space=block==0?p->phy:block==1?p->csid:p->vfe;CHECK(reg<(block==0?0x2000:block==1?0x2b00:0x10000));*out=space[reg/4];return 0;}
static int write_reg(void *v,unsigned block,uint32_t reg,uint32_t value){Peer *p=v;if(++p->ops==p->fail_at)return -5;p->writes++;CHECK(block<3&&!(reg&3));
    uint32_t *space=block==0?p->phy:block==1?p->csid:p->vfe;CHECK(reg<(block==0?0x2000:block==1?0x2b00:0x10000));
    if(block==1&&reg==csid_clear(p)){p->csid[csid_top(p)/4]&=~value;return 0;}
    if(block==1&&reg==buf_clear(p)){p->csid[buf_status(p)/4]&=~value;return 0;}
    if(block==1&&reg==rdi_irq(p)+8){p->csid[rdi_irq(p)/4]&=~value;return 0;}
    space[reg/4]=value;
    if(block==1&&reg==0x10){p->reset++;if(!p->stall_reset)p->csid[csid_top(p)/4]|=1;}
    if(block==2&&reg==wm_cfg(p)+4){CHECK(p->address_count<32);p->addresses[p->address_count++]=value;}
    return 0;
}
static uint64_t now(void *v){Peer *p=v;return p->clock_back&&p->clock>5100?1:p->clock;}
static int delay(void *v,unsigned us){Peer *p=v;CHECK(us==10||us==5000||us==0xd1);p->clock+=us;return 0;}
static int buffer(void *v,const EldenCameraPipelineConfig *c,const EldenCameraBuffer *b,int complete){Peer *p=v;CHECK(c->owner==b->owner&&c->epoch==b->epoch&&b->route_epoch==11&&b->mapping);p->buffers++;if(complete)p->completions++;return p->fail_buffer?-71:0;}
static int drain(void *v,const EldenCameraPipelineConfig *c){Peer *p=v;CHECK(c->owner==7&&c->epoch==9);p->drains++;return p->fail_drain?-16:0;}
static void setup(Peer *p,unsigned sensor,unsigned decoder){memset(p,0,sizeof(*p));scenarios++;p->clock=100;
    p->cfg.owner=7;p->cfg.epoch=9;p->cfg.route_epoch=11;p->cfg.sensor=sensor;p->cfg.decoder=decoder;p->cfg.rdi=0;p->cfg.timer_hz=400000000;
    p->cfg.mode=(EldenCameraMode){sensor?3264:4080,sensor?2448:3072,sensor?336000000:672000000,sensor?281600000:558933333,sensor?0x08a8:0x38ee,sensor?270:90,
        sensor,sensor?0x31:0x56,sensor?7:11,sensor?4:3,4,10,sensor?EldenCameraRggb:EldenCameraGrbg,0};
    p->csid[0]=0x90080000;p->vfe[0]=0x90010000;
}
static int open(Peer *p){EldenCameraPipelineIo io={p,gate,read_reg,write_reg,now,delay,buffer,drain};return elden_camera_pipeline_open(&p->core,&p->cfg,&io);}
static EldenCameraPipelineStatus status(Peer *p){EldenCameraPipelineStatus s;CHECK(!elden_camera_pipeline_status(&p->core,&s));return s;}
static EldenCameraBuffer view(Peer *p,unsigned n){EldenCameraPipelineStatus s=status(p);return (EldenCameraBuffer){n+1,7,9,11,0x20000000+0x2000000*n,s.frame_bytes,(void *)(uintptr_t)(n+1)};}
static void queued(Peer *p,unsigned count){for(unsigned i=0;i<count;i++){EldenCameraBuffer b=view(p,i);CHECK(!elden_camera_pipeline_queue(&p->core,&b));}}
static void event(Peer *p,int sof,int done,uint64_t sofstamp,uint64_t eofstamp){uint32_t off=rdi_base(p);uint32_t ts=p->cfg.decoder>=3?0x594+0x100*p->cfg.rdi:0xea0+0x200*p->cfg.rdi;
    (void)off;if(sof)p->csid[rdi_irq(p)/4]|=1u<<4;if(done)p->csid[buf_status(p)/4]|=1u<<((p->cfg.decoder>=3?1:16)+p->cfg.rdi);
    p->csid[ts/4]=(uint32_t)sofstamp;p->csid[(ts+4)/4]=(uint32_t)(sofstamp>>32);p->csid[(ts+16)/4]=(uint32_t)eofstamp;p->csid[(ts+20)/4]=(uint32_t)(eofstamp>>32);p->clock+=33333;
}
#ifndef ELDEN_CAMERA_PIPELINE_PEER_API
int main(void){
    for(unsigned sensor=0;sensor<2;sensor++)for(unsigned decoder=0;decoder<5;decoder++){
        Peer p;setup(&p,sensor,decoder);uint64_t physical[3];uint32_t bytes[3],irq[3];CHECK(!elden_camera_pipeline_resources(&p.cfg,physical,bytes,irq));
        CHECK(physical[0]==(sensor?0xadb1000:0xadaf000)&&irq[0]==(sensor?154:480));CHECK(!open(&p));queued(&p,3);CHECK(!elden_camera_pipeline_start(&p.core));
        CHECK(p.address_count==2&&p.addresses[0]==0x200000&&p.addresses[1]==0x220000);CHECK(p.phy[0x8/4]==(sensor?31u:29u));
        event(&p,1,1,100,110);CHECK(!elden_camera_pipeline_service(&p.core));EldenCameraFrame f;CHECK(!elden_camera_pipeline_frame(&p.core,&f)&&f.buffer.id==1&&f.sequence==0&&!f.error);
        CHECK(p.address_count==3&&p.addresses[2]==0x240000&&p.completions==1);CHECK(!elden_camera_pipeline_queue(&p.core,&f.buffer));
        p.fail_drain=1;CHECK(elden_camera_pipeline_stop(&p.core)==-16&&status(&p).state==EldenPipeHeld);p.fail_drain=0;CHECK(!elden_camera_pipeline_stop(&p.core)&&status(&p).stopped);CHECK(!elden_camera_pipeline_stop(&p.core));
    }
    {Peer p;setup(&p,0,0);CHECK(!open(&p));CHECK(elden_camera_pipeline_start(&p.core)==-19);EldenCameraBuffer b=view(&p,0);b.iova++;CHECK(elden_camera_pipeline_queue(&p.core,&b)==-22);b=view(&p,0);b.route_epoch++;CHECK(elden_camera_pipeline_queue(&p.core,&b)==-22);}
    {Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,2);p.stall_reset=1;CHECK(elden_camera_pipeline_start(&p.core)==-110&&status(&p).state==EldenPipeHeld);}
    {Peer p;setup(&p,1,3);CHECK(!open(&p));queued(&p,2);p.deny=1;CHECK(elden_camera_pipeline_start(&p.core)==-13&&!p.reset);CHECK(elden_camera_pipeline_stop(&p.core)==-13);}
    {Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,3);CHECK(!elden_camera_pipeline_start(&p.core));event(&p,0,1,0,1);CHECK(elden_camera_pipeline_service(&p.core)==-71);}
    {Peer p;setup(&p,0,0);p.csid[0xe00/4]=1u<<31;CHECK(open(&p)==-16&&p.writes==0);CHECK(elden_camera_pipeline_stop(&p.core)==-16&&p.drains==0);}
    {Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,2);CHECK(!elden_camera_pipeline_start(&p.core));p.fail_buffer=1;event(&p,1,1,100,110);CHECK(elden_camera_pipeline_service(&p.core)==-71);}
    {Peer p;setup(&p,1,3);CHECK(!open(&p));queued(&p,2);CHECK(!elden_camera_pipeline_start(&p.core));p.csid[buf_status(&p)/4]=1u<<3;CHECK(elden_camera_pipeline_service(&p.core)==-71);CHECK(p.csid[buf_status(&p)/4]==(1u<<3));}
    {Peer p;setup(&p,0,0);p.vfe[(0x800+0x500)/4]=1;CHECK(open(&p)==-16&&p.writes==0);}
    {Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,2);CHECK(!elden_camera_pipeline_start(&p.core));event(&p,1,1,100,110);CHECK(!elden_camera_pipeline_service(&p.core));p.deny=1;EldenCameraFrame f;memset(&f,0xa5,sizeof f);CHECK(elden_camera_pipeline_frame(&p.core,&f)==-13&&f.buffer.id==UINT64_C(0xa5a5a5a5a5a5a5a5));}
    {Peer baseline;setup(&baseline,0,0);CHECK(!open(&baseline));queued(&baseline,2);unsigned begin=baseline.ops;CHECK(!elden_camera_pipeline_start(&baseline.core));unsigned last=baseline.ops;
        /* Each original register/delay stage's failing physical read/write
         * must refuse readiness. No synthetic completion replaces it. */
        for(unsigned failure=begin+1;failure<=last;failure++){
            Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,2);p.fail_at=failure;
            CHECK(elden_camera_pipeline_start(&p.core)<0&&status(&p).state==EldenPipeHeld);
        }
    }
    {Peer p;setup(&p,0,0);CHECK(!open(&p));queued(&p,3);CHECK(!elden_camera_pipeline_start(&p.core));
        event(&p,1,0,100,0);CHECK(!elden_camera_pipeline_service(&p.core));
        for(unsigned frame=0;frame<3;frame++){
            event(&p,frame<2,1,200+100*frame,150+100*frame);CHECK(!elden_camera_pipeline_service(&p.core));
            EldenCameraFrame f;CHECK(!elden_camera_pipeline_frame(&p.core,&f)&&f.buffer.id==frame+1&&f.sequence==frame&&!f.error);
            CHECK(!elden_camera_pipeline_queue(&p.core,&f.buffer));
        }
        CHECK(status(&p).frames==3&&p.completions==3);CHECK(!elden_camera_pipeline_stop(&p.core));
    }
    {Peer baseline;setup(&baseline,1,3);CHECK(!open(&baseline));queued(&baseline,2);CHECK(!elden_camera_pipeline_start(&baseline.core));
        unsigned begin=baseline.ops;CHECK(!elden_camera_pipeline_stop(&baseline.core));unsigned last=baseline.ops;
        for(unsigned failure=begin+1;failure<=last;failure++){
            Peer p;setup(&p,1,3);CHECK(!open(&p));queued(&p,2);CHECK(!elden_camera_pipeline_start(&p.core));p.fail_at=failure;
            CHECK(elden_camera_pipeline_stop(&p.core)<0&&!status(&p).stopped);
        }
    }
    printf("PASS %u actual Gen4 PHY/CSID/VFE queue/DMA fence scenarios, %u checks; power/MMIO/DMA/IRQ peers modeled\n",scenarios,checks);return 0;
}
#else
void *elden_camera_pipeline_peer_create(unsigned sensor,unsigned decoder){Peer *p=malloc(sizeof(*p));CHECK(p);setup(p,sensor,decoder);return p;}
int elden_camera_pipeline_peer_read(void *p,unsigned b,uint32_t r,uint32_t *out){return read_reg(p,b,r,out);}
int elden_camera_pipeline_peer_write(void *p,unsigned b,uint32_t r,uint32_t v){return write_reg(p,b,r,v);}
void elden_camera_pipeline_peer_event(void *p,int sof,int done,uint64_t s,uint64_t e){event(p,sof,done,s,e);}
uint64_t elden_camera_pipeline_peer_now(void *p){return now(p);}
void elden_camera_pipeline_peer_delay(void *p,unsigned us){Peer *m=p;m->clock+=us;}
void elden_camera_pipeline_peer_destroy(void *p){free(p);}
#endif
