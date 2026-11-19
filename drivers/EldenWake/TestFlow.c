/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual wake flow + original Protocol + Gesture + owned IRQ route. Only
 * hardware/OS input/power/resume are modeled, not replaced by a production stub. */
#define main prior_irq_main
#include "TestIrq.c"
#undef main
#include "Flow.h"
#include "../EldenNvt36536/Protocol.h"
typedef struct {
    Model irq;EldenWakeFlow flow;EldenNvtDevice device;
    uint8_t tx[ELDEN_NVT_FW_CHUNK+1],rx[110],packet[109];unsigned paused,pending,power_ready;
    unsigned sequence[64],steps,fail_step,spi,command,restore,resume,requests;
} FlowPeer;
enum {GateNormal=1,Pause=2,GatePaused=3,Release=4,Acquire=5,Disarm=6,Command=7,Arm=8,Take=9,Packet=10,Request=11,GateResume=12,Restore=13,Resume=14,GateCleanup=15,Stop=16};
static int step(FlowPeer *p,unsigned code){CHECK(p->steps<64);p->sequence[p->steps++]=code;return p->steps==p->fail_step?-5:0;}
static int gate(void *v,uint64_t owner,uint64_t epoch,unsigned phase){FlowPeer *p=v;CHECK(owner==11&&epoch==7);
    int rc=step(p,phase==EldenWakeAdmissionNormal?GateNormal:phase==EldenWakeAdmissionPaused?GatePaused:phase==EldenWakeAdmissionResume?GateResume:GateCleanup);
    if(rc)return rc;if(phase==EldenWakeAdmissionPaused&&!p->paused)return -13;
    if(phase==EldenWakeAdmissionResume&&(!p->paused||!p->power_ready))return -19;return 0;}
static int pause(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;CHECK(owner==11&&epoch==7);int rc=step(p,Pause);if(!rc)p->paused=1;return rc;}
static int release(void *v){FlowPeer *p=v;CHECK(p->paused);return step(p,Release);}
static int irq_acquire(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;CHECK(owner==11&&epoch==7&&p->paused);int rc=step(p,Acquire);return rc?rc:acquire(&p->irq);}
static int irq_arm(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;CHECK(p->paused);int rc=step(p,Arm);return rc?rc:elden_wake_irq_arm(&p->irq.route,owner,epoch);}
static int irq_disarm(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;int rc=step(p,Disarm);return rc?rc:elden_wake_irq_disarm(&p->irq.route,owner,epoch);}
static int irq_stop(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;int rc=step(p,Stop);return rc?rc:elden_wake_irq_stop(&p->irq.route,owner,epoch);}
static int irq_take(void *v,uint64_t owner,uint64_t epoch,uint32_t *n){FlowPeer *p=v;CHECK(owner==11&&epoch==7&&p->paused);int rc=step(p,Take);if(!rc){*n=p->pending;p->pending=0;}return rc;}
static int transfer(void *v,const uint8_t *tx,uint8_t *rx,size_t n){FlowPeer *p=v;CHECK(p->paused);p->spi++;
    if(tx[0]==0xff){CHECK(!rx&&n==3&&tx[1]==(ELDEN_NVT_EVENT>>15)&&tx[2]==((ELDEN_NVT_EVENT>>7)&255));return 0;}
    if(!rx){CHECK(n==2&&tx[0]==0xd0&&(tx[1]==0x13||tx[1]==0x11));p->command=tx[1];return 0;}
    CHECK(n==110&&tx[0]==0);memset(rx,0,n);memcpy(rx+2,p->packet+1,108);return 0;
}
static void delay(void *v,unsigned us){(void)v;(void)us;CHECK(0);}
static int command(void *v,int enabled){FlowPeer *p=v;int rc=step(p,Command);return rc?rc:elden_nvt_low_power_command(&p->device,enabled!=0);}
static int packet(void *v,uint8_t out[109]){FlowPeer *p=v;CHECK(p->paused);int rc=step(p,Packet);return rc?rc:elden_nvt_points(&p->device,out);}
static int request(void *v,uint64_t owner,uint64_t epoch,unsigned source){FlowPeer *p=v;CHECK(owner==11&&epoch==7&&source==15&&p->paused);int rc=step(p,Request);if(!rc)p->requests++;return rc;}
static int restore(void *v){FlowPeer *p=v;CHECK(p->paused&&p->power_ready);int rc=step(p,Restore);if(!rc)p->restore++;return rc;}
static int resume(void *v,uint64_t owner,uint64_t epoch){FlowPeer *p=v;CHECK(owner==11&&epoch==7&&p->restore&&p->paused);int rc=step(p,Resume);if(!rc){p->paused=0;p->resume++;}return rc;}
static FlowPeer *create(void){FlowPeer *p=calloc(1,sizeof(*p));CHECK(p);setup(&p->irq,0x30200,1);
    p->device.context=p;p->device.transfer=transfer;p->device.delay_us=delay;p->device.tx=p->tx;p->device.rx=p->rx;
    p->device.tx_bytes=sizeof(p->tx);p->device.rx_bytes=sizeof(p->rx);p->device.identified=p->device.ready=true;p->device.pid=0x6097;
    EldenWakeFlowIo io={p,gate,pause,release,irq_acquire,irq_arm,irq_disarm,irq_stop,irq_take,command,packet,request,restore,resume};
    CHECK(!elden_wake_flow_init(&p->flow,&io,11,7));p->steps=0;return p;}
static void gesture(FlowPeer *p,unsigned id,int wrapped){memset(p->packet,0,sizeof(p->packet));p->packet[1]=(uint8_t)((wrapped?30:id)<<3);
    if(wrapped){p->packet[2]=1;p->packet[3]=(uint8_t)id;}uint8_t sum=0;for(unsigned i=1;i<65;i++)sum=(uint8_t)(sum+p->packet[i]);p->packet[65]=(uint8_t)(0-sum);p->pending=1;}
int main(void){
    FlowPeer *p=create();CHECK(elden_wake_flow_normal_input(&p->flow));CHECK(!elden_wake_flow_suspend(&p->flow,1));
    unsigned expected[]={GateNormal,Pause,GatePaused,Release,Acquire,Disarm,Command,Arm};
    CHECK(p->steps==8&&!memcmp(p->sequence,expected,sizeof(expected))&&p->command==0x13&&p->spi==2);
    CHECK(p->paused&&p->flow.state==EldenWakeFlowGesture&&!elden_wake_flow_normal_input(&p->flow));
    CHECK(!elden_wake_flow_irq(&p->flow)&&!p->requests);gesture(p,14,0);CHECK(!elden_wake_flow_irq(&p->flow)&&!p->requests&&p->flow.state==EldenWakeFlowGesture);
    gesture(p,15,1);CHECK(!elden_wake_flow_irq(&p->flow)&&p->requests==1&&p->flow.state==EldenWakeFlowRequested&&p->paused&&!p->restore&&!p->resume);
    CHECK(elden_wake_flow_resume(&p->flow)==-19&&p->paused);p->power_ready=1;
    CHECK(!elden_wake_flow_resume(&p->flow)&&elden_wake_flow_normal_input(&p->flow)&&p->restore==1&&p->resume==1);
    CHECK(!elden_wake_flow_stop(&p->flow)&&p->paused&&p->flow.state==EldenWakeFlowStopped&&!elden_wake_flow_normal_input(&p->flow));free(p);
    p=create();CHECK(!elden_wake_flow_suspend(&p->flow,0)&&p->command==0x11&&!p->irq.attach_calls&&p->flow.state==EldenWakeFlowDeep);
    p->power_ready=1;CHECK(!elden_wake_flow_resume(&p->flow)&&p->resume==1);CHECK(!elden_wake_flow_stop(&p->flow));free(p);
    for(unsigned fail=1;fail<=8;fail++){
        p=create();p->fail_step=fail;CHECK(elden_wake_flow_suspend(&p->flow,1)==-5);
        CHECK(fail==1?elden_wake_flow_normal_input(&p->flow):p->flow.state==EldenWakeFlowHeld);
        if(fail>1)CHECK(!elden_wake_flow_normal_input(&p->flow));free(p);
    }
    p=create();CHECK(!elden_wake_flow_suspend(&p->flow,1));gesture(p,15,0);p->packet[10]^=1;
    CHECK(elden_wake_flow_irq(&p->flow)==-84&&p->flow.state==EldenWakeFlowHeld&&!p->requests&&p->paused);free(p);
    for(unsigned fail=1;fail<=6;fail++){
        p=create();CHECK(!elden_wake_flow_suspend(&p->flow,1));gesture(p,15,0);CHECK(!elden_wake_flow_irq(&p->flow));
        p->power_ready=1;p->steps=0;p->fail_step=fail;CHECK(elden_wake_flow_resume(&p->flow)==-5&&p->paused&&!p->resume);
        CHECK(fail==1?p->flow.state==EldenWakeFlowRequested:p->flow.state==EldenWakeFlowHeld);free(p);
    }
    // Failed real wake submission retains paused state and does not fabricate
    // a power-ready event or resume normal input.
    p=create();CHECK(!elden_wake_flow_suspend(&p->flow,1));gesture(p,15,0);p->steps=0;p->fail_step=5;
    CHECK(elden_wake_flow_irq(&p->flow)==-5&&p->flow.state==EldenWakeFlowHeld&&!p->requests&&!p->resume);free(p);
    printf("PASS: %u actual flow/route/Protocol/Gesture checks; pause-release-command-IRQ-power-ready-restore order and fault refusal; power/HID/MMIO modeled\n",checks);return 0;
}
