/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual clock/GDSC/pin methods; independent hardware/provider peer model. */
#include "Clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,scenarios;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"clock line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint32_t cam[0x20000/4],bist[0x6000/4],pins[0x78000/4];uint64_t now;
    unsigned writes,controllers,dependencies,lock,fail,steps,stall_root,stall_update,stall_branch,stall_domain,fail_after_write,wrong_parent;
    uint32_t root_busy,domain_busy,idle[2],pin_idle,deny;uint32_t active[39];} Peer;
static Peer peer;
static const uint32_t roots[]={0x112e8,0x10100,0x100dc,0x11018,0x11098,0x11100,0x11150,0x112c0,0x11068,
    0x10000,0x10024,0x10044,0x10064,0x10084,0x100a4,0x1117c,0x11364};
static const uint32_t branch_regs[]={0x11310,0x11300,0x113ac,0x1139c,0x11040,0x110c0,0x11128,0x11178,0x112d8,
    0x10020,0x1001c,0x10018,0x10040,0x1003c,0x10060,0x1005c,0x10080,0x1007c,0x100a0,0x1009c,0x100c0,0x100bc,
    0x11030,0x11060,0x110b0,0x110e0,0x11118,0x11148,0x11168,0x111a8,0x111a4,0x11194,0x11348,0x11284,0x112a0};
static const uint32_t branch_roots[]={0x112e8,0x112e8,0x10100,0x100dc,0x11018,0x11098,0x11100,0x11150,0x112c0,
    0x11068,0x11068,0x10000,0x11068,0x10024,0x11068,0x10044,0x11068,0x10064,0x11068,0x10084,0x11068,0x100a4,
    0x11018,0x100dc,0x11098,0x100dc,0x11100,0x100dc,0x11150,0x10100,0x11068,0x1117c,0x11364,0x1126c,0x11288};
static uint32_t *address(Peer *,unsigned,uint32_t);
static int hardware_root_on(Peer *p,unsigned space,uint32_t reg){if(*address(p,space,reg)&2)return 1;
    if(space==1)return *address(p,space,reg==0x4054?0x406c:0x4088)&1;
    for(unsigned i=0;i<sizeof(branch_regs)/sizeof(branch_regs[0]);i++)if(branch_roots[i]==reg&&p->cam[branch_regs[i]/4]&1)return 1;return 0;}
static int failing(Peer *p){return ++p->steps==p->fail?-5:0;}
static uint32_t *address(Peer *p,unsigned space,uint32_t reg){CHECK(!(reg&3));if(space==0){CHECK(reg<sizeof(p->cam));return &p->cam[reg/4];}
    if(space==1){CHECK(reg<sizeof(p->bist));return &p->bist[reg/4];}CHECK(space==2&&reg<sizeof(p->pins));return &p->pins[reg/4];}
static int admit(void *v,uint64_t owner,uint64_t epoch,unsigned op){Peer *p=v;CHECK(owner==7&&epoch==9&&op<=2);return p->deny?-13:failing(p);}
static int lock(void *v,uint64_t owner,uint64_t epoch,int on){Peer *p=v;CHECK(owner==7&&epoch==9);if(on){CHECK(!p->lock);p->lock=1;}else{CHECK(p->lock);p->lock=0;}return 0;}
static int read_reg(void *v,unsigned space,uint32_t reg,uint32_t *out){Peer *p=v;CHECK(p->controllers&&p->lock);int rc=failing(p);if(rc)return rc;*out=*address(p,space,reg);return 0;}
static unsigned root_reg(unsigned space,uint32_t reg){return space==1?(reg==0x4054||reg==0x4070):
    reg==0x1126c||reg==0x11288||reg==0x10100||reg==0x100dc||reg==0x112e8||reg==0x11018||reg==0x11098||reg==0x11100||reg==0x11150||
    reg==0x112c0||reg==0x11068||reg==0x10000||reg==0x10024||reg==0x10044||reg==0x10064||reg==0x10084||reg==0x100a4||reg==0x1117c||reg==0x11364;}
static int write_reg(void *v,unsigned space,uint32_t reg,uint32_t value){Peer *p=v;CHECK(p->controllers&&p->lock);int rc=failing(p);if(rc&&!p->fail_after_write)return rc;p->writes++;
    uint32_t *target=address(p,space,reg);*target=value;
    if(space<2&&root_reg(space,reg)){if(!p->stall_update)*target&=~1u;if(hardware_root_on(p,space,reg)){if(!p->stall_root)*target&=~(1u<<31);}else *target|=1u<<31;}
    if(space==0&&(reg==0x1134c||reg==0x11004||reg==0x11084||reg==0x110ec)&&!p->stall_domain)
        *address(p,0,reg+4)=value&1?1u<<15:1u<<16;
    if(space==0)for(unsigned i=0;i<sizeof(branch_regs)/sizeof(branch_regs[0]);i++)if(reg==branch_regs[i]){
        if(!p->stall_branch)*target=value&1?value&~(1u<<31):value|(1u<<31);
        uint32_t *r=address(p,space,branch_roots[i]);if(hardware_root_on(p,space,branch_roots[i]))*r&=~(1u<<31);else *r|=1u<<31;}
    if(space==1&&(reg==0x406c||reg==0x4088)){if(!p->stall_branch)*target=value&1?value&~(1u<<31):value|(1u<<31);
        unsigned root=reg==0x406c?0x4054:0x4070;uint32_t *r=address(p,space,root);if(hardware_root_on(p,space,root))*r&=~(1u<<31);else *r|=1u<<31;}
    return rc;}
static int delay(void *v,unsigned us){Peer *p=v;p->now+=us;return failing(p);}
static uint64_t now(void *v){return ((Peer *)v)->now;}
static int controllers(void *v,uint64_t owner,uint64_t epoch,int action){Peer *p=v;CHECK(owner==7&&epoch==9&&p->lock);int rc=failing(p);if(rc)return rc;
    if(action==1){CHECK(!p->controllers);p->controllers=1;}else if(!action){CHECK(!p->dependencies);p->controllers=0;}return 0;}
static uint32_t parent_for(const EldenCameraClockRequest *r){if(r->root==UINT32_MAX)return r->hz;
    uint64_t rate=(uint64_t)r->hz*(r->hid+1)/2;if(r->n)rate=rate*r->n/r->m;return (uint32_t)rate;}
static int dependency(void *v,uint64_t owner,uint64_t epoch,const EldenCameraClockRequest *r,int action,uint32_t *hz){Peer *p=v;CHECK(owner==7&&epoch==9&&r->clock<39&&p->controllers);
    int rc=failing(p);if(rc)return rc;
    if(action==1){CHECK(!p->active[r->clock]);p->active[r->clock]=1;p->dependencies++;}else if(!action){if(p->active[r->clock]){p->active[r->clock]=0;p->dependencies--;}}
    else CHECK(p->active[r->clock]);*hz=parent_for(r);if(p->wrong_parent)*hz+=100000;return 0;}
static int idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned clock){Peer *p=v;CHECK(owner==7&&epoch==9&&sensor<2&&clock<39);return p->idle[sensor]?failing(p):-16;}
static int sensor_idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){return idle(v,owner,epoch,sensor,0);}
static int root_idle(void *v,uint64_t owner,uint64_t epoch,unsigned root){Peer *p=v;CHECK(owner==7&&epoch==9&&root<21);return p->root_busy?-16:failing(p);}
static int domain_idle(void *v,uint64_t owner,uint64_t epoch,unsigned domain){Peer *p=v;CHECK(owner==7&&epoch==9&&domain<4);return p->domain_busy?-16:failing(p);}
static int pins_idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){Peer *p=v;CHECK(owner==7&&epoch==9&&sensor<2);return p->pin_idle?failing(p):-16;}
static void setup(void){memset(&peer,0,sizeof(peer));peer.idle[0]=peer.idle[1]=peer.pin_idle=1;
    for(unsigned r=0;r<sizeof(peer.cam)/4;r++)peer.cam[r]=1u<<31;for(unsigned r=0;r<sizeof(peer.bist)/4;r++)peer.bist[r]=1u<<31;
    const uint32_t domains[]={0x1134c,0x11004,0x11084,0x110ec};for(unsigned i=0;i<4;i++){peer.cam[domains[i]/4]=1;peer.cam[domains[i]/4+1]=1u<<15;}
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++)CHECK(root_reg(0,roots[i]));
    const unsigned pins[]={7,11,92,93,113,114,117,118};for(unsigned i=0;i<8;i++){peer.pins[pins[i]*0x1000/4]=0x840;peer.pins[(pins[i]*0x1000+4)/4]=0;}
}
static int open(EldenCameraClocks *s){EldenCameraClockIo io={&peer,7,9,admit,lock,read_reg,write_reg,delay,now,controllers,dependency,idle,sensor_idle,root_idle,domain_idle,pins_idle};return elden_camera_clocks_open(s,&io);}
static int clock(EldenCameraClocks *s,unsigned sensor,unsigned which,uint32_t hz,int on){return elden_camera_clock_resource(s,7,9,sensor,which,hz,on);}
#if defined(ELDEN_CAMERA_CLOCK_PEER_API)
void elden_camera_clock_peer_setup(void){setup();}
int elden_camera_clock_peer_read(unsigned space,uint32_t reg,uint32_t *out){return read_reg(&peer,space,reg,out);}
int elden_camera_clock_peer_write(unsigned space,uint32_t reg,uint32_t value){return write_reg(&peer,space,reg,value);}
void elden_camera_clock_peer_delay(unsigned us){(void)delay(&peer,us);}
uint64_t elden_camera_clock_peer_now(void){return peer.now;}
EldenCameraClockIo elden_camera_clock_peer_io(void){return (EldenCameraClockIo){&peer,7,9,admit,lock,read_reg,write_reg,delay,now,controllers,dependency,idle,sensor_idle,root_idle,domain_idle,pins_idle};}
int elden_camera_clock_peer_reset(unsigned sensor){CHECK(sensor<2);return !(peer.pins[((sensor?7u:11u)*0x1000+4)/4]&2);}
int elden_camera_clock_peer_mclk(unsigned sensor){CHECK(sensor<2);return !!(peer.bist[(sensor?0x4088u:0x406cu)/4]&1);}
#endif
static void basic(void){setup();EldenCameraClocks s={0};CHECK(!open(&s));CHECK(!elden_camera_pins_prepare(&s,7,9));
    unsigned before=peer.writes;CHECK(!elden_camera_clock_rate_supported(EldenCamClockCci0,400000000));
    CHECK(clock(&s,0,EldenCamClockCci0,400000000,1)==-34&&peer.writes==before&&!peer.dependencies);
    CHECK(!clock(&s,0,EldenCamClockCci0,400000000,0)&&peer.writes==before);
    CHECK(!elden_camera_sensor_reset(&s,7,9,0,0));CHECK(peer.pins[(11*0x1000+4)/4]&2);CHECK(!elden_camera_sensor_reset(&s,7,9,0,1));
    CHECK(!elden_camera_domain_resource(&s,7,9,0,0,1));CHECK(peer.cam[0x11350/4]&(1u<<16));
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));CHECK((peer.cam[0x11270/4]&0x10371f)==0x21f);
    CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,1));CHECK((peer.bist[0x4058/4]&0x10371f)==1);
    CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,1));CHECK(elden_camera_clocks_stop(&s)==-16);
    CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,0));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));
    CHECK(peer.cam[0x11270/4]==(1u<<31)&&peer.bist[0x4058/4]==(1u<<31));
    CHECK(!elden_camera_domain_resource(&s,7,9,0,0,0));CHECK(!elden_camera_clocks_stop(&s));CHECK(!peer.controllers&&!peer.dependencies);
    EldenCameraClockStatus status={0};CHECK(!elden_camera_clocks_status(&s,&status)&&status.stopped&&!status.changed_roots&&!status.changed_branches&&!status.changed_domains&&!status.changed_pins);
    scenarios++;}
static void sharing(void){setup();EldenCameraClocks s={0};CHECK(!open(&s));
    CHECK(!clock(&s,0,EldenCamClockCsidCsiphyRx,400000000,1));CHECK(!clock(&s,1,EldenCamClockCsiphy4,400000000,1));
    CHECK(clock(&s,1,EldenCamClockCsiphy3,480000000,1)==-16); // root-rate conflict retains all
    // Root-rate conflict refuses BEFORE requesting any parent change/vote;
    // no new dependency can perturb another camera's still-active clock.
    CHECK(!clock(&s,1,EldenCamClockCsiphy3,480000000,0));
    CHECK(!clock(&s,0,EldenCamClockCsidCsiphyRx,400000000,0));CHECK(peer.cam[0x1106c/4]&0x700);
    CHECK(!clock(&s,1,EldenCamClockCsiphy4,400000000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));
    CHECK(!clock(&s,1,EldenCamClockCci0,37500000,1));CHECK(peer.dependencies==1);
    peer.idle[0]=0;CHECK(clock(&s,0,EldenCamClockCci0,37500000,0)==-16);peer.idle[0]=1;
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));CHECK(peer.cam[0x11284/4]&1);
    CHECK(!clock(&s,1,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
}
static void inherited(void){setup();EldenCameraClocks s={0};peer.cam[0x11270/4]=0x21f;peer.cam[0x11284/4]=1;peer.cam[0x1126c/4]=0;
    CHECK(!open(&s));unsigned before=peer.writes;CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));CHECK(peer.writes==before);
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0)&&peer.cam[0x11284/4]&1);CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));peer.cam[0x1134c/4]=0x22f800;peer.cam[0x11350/4]=1u<<16;
    CHECK(!open(&s));before=peer.writes;CHECK(!elden_camera_domain_resource(&s,7,9,0,0,1));
    CHECK(!elden_camera_domain_resource(&s,7,9,0,0,0));CHECK(peer.writes==before);CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockMclk3,24000000,1));
    CHECK((peer.bist[0x4058/4]&0x371f)==0x2313);CHECK(peer.bist[0x405c/4]&255);CHECK(!clock(&s,0,EldenCamClockMclk3,24000000,0));
    CHECK(!elden_camera_clocks_stop(&s));scenarios++;
}
static void observation(void){setup();EldenCameraClocks s={0};CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));uint32_t hz=0;
    CHECK(!elden_camera_clock_observe(&s,7,9,EldenCamClockCci0,&hz)&&hz==37500000);peer.cam[0x11284/4]&=~1u;
    CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockCci0,&hz)==-116&&!hz);
    CHECK(clock(&s,0,EldenCamClockCci0,37500000,0)==-71);peer.cam[0x11284/4]|=1u<<31;
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,1));peer.wrong_parent=1;
    CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockMclk3,&hz)==-34);peer.wrong_parent=0;
    CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));peer.cam[0x11270/4]^=0x400;
    unsigned writes=peer.writes;CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockCci0,&hz)==-116);
    CHECK(clock(&s,0,EldenCamClockCci0,37500000,0)==-116&&peer.writes>=writes); // branch can stop; foreign root retained
    peer.cam[0x11270/4]^=0x400;CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));
    CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,1));peer.bist[0x4058/4]^=0x400;
    CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockMclk3,&hz)==-116);
    CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockCci0,&hz)==-125);
    CHECK(!elden_camera_clock_observe_cleanup(&s,7,9,EldenCamClockCci0,&hz)&&hz==37500000);
    peer.bist[0x4058/4]^=0x400;CHECK(!clock(&s,0,EldenCamClockMclk3,19200000,0));
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
    setup();memset(&s,0,sizeof(s));peer.cam[0x11270/4]=0x88000000;CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));
    peer.cam[0x11270/4]^=1u<<25;CHECK(elden_camera_clock_observe(&s,7,9,EldenCamClockCci0,&hz)==-116);
    CHECK(clock(&s,0,EldenCamClockCci0,37500000,0)==-116);peer.cam[0x11270/4]^=1u<<25;
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0)&&peer.cam[0x11270/4]==0x88000000);CHECK(!elden_camera_clocks_stop(&s));scenarios++;
}
static void domains(void){setup();EldenCameraClocks s={0};CHECK(!open(&s));CHECK(elden_camera_domain_resource(&s,7,9,0,1,1)==-19);
    CHECK(!elden_camera_domain_resource(&s,7,9,0,0,1));CHECK(!elden_camera_domain_resource(&s,7,9,1,0,1));
    CHECK(!elden_camera_domain_resource(&s,7,9,0,1,1));CHECK(!elden_camera_domain_resource(&s,7,9,0,0,0));
    CHECK(elden_camera_domain_resource(&s,7,9,1,0,0)==-16);CHECK(!elden_camera_domain_resource(&s,7,9,0,1,0));
    CHECK(!elden_camera_domain_resource(&s,7,9,1,0,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
}
static void failures(void){
    // Exhaust every real admission/read/write/delay/parent gate in representative
    // CCI and MCLK enable/disable, domain on/off and pin prepare/final restore.
    for(unsigned phase=0;phase<6;phase++)for(unsigned after=0;after<2;after++){
        setup();EldenCameraClocks probe={0};CHECK(!open(&probe));
        if(phase==1)CHECK(!clock(&probe,0,EldenCamClockCci0,37500000,1));
        if(phase==3)CHECK(!elden_camera_domain_resource(&probe,7,9,0,0,1));
        if(phase==5)CHECK(!elden_camera_pins_prepare(&probe,7,9));peer.steps=0;
        if(phase==0)CHECK(!clock(&probe,0,EldenCamClockCci0,37500000,1));
        if(phase==1)CHECK(!clock(&probe,0,EldenCamClockCci0,37500000,0));
        if(phase==2)CHECK(!elden_camera_domain_resource(&probe,7,9,0,0,1));
        if(phase==3)CHECK(!elden_camera_domain_resource(&probe,7,9,0,0,0));
        if(phase==4)CHECK(!elden_camera_pins_prepare(&probe,7,9));
        if(phase==5)CHECK(!elden_camera_clocks_stop(&probe));unsigned steps=peer.steps;
        for(unsigned at=1;at<=steps;at++){
            setup();EldenCameraClocks s={0};CHECK(!open(&s));if(phase==1)CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));
            if(phase==3)CHECK(!elden_camera_domain_resource(&s,7,9,0,0,1));if(phase==5)CHECK(!elden_camera_pins_prepare(&s,7,9));
            peer.steps=0;peer.fail=at;peer.fail_after_write=after;int rc=0;
            if(phase==0)rc=clock(&s,0,EldenCamClockCci0,37500000,1);if(phase==1)rc=clock(&s,0,EldenCamClockCci0,37500000,0);
            if(phase==2)rc=elden_camera_domain_resource(&s,7,9,0,0,1);if(phase==3)rc=elden_camera_domain_resource(&s,7,9,0,0,0);
            if(phase==4)rc=elden_camera_pins_prepare(&s,7,9);if(phase==5)rc=elden_camera_clocks_stop(&s);
            CHECK(rc<0);peer.fail=0;peer.fail_after_write=0;
            if(phase<2){rc=clock(&s,0,EldenCamClockCci0,37500000,0);if(rc<0){ // stuck update is physical uncertainty, not failed cleanly
                CHECK(peer.cam[0x1126c/4]&1);peer.cam[0x1126c/4]&=~1u;CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));}}
            if(phase==2||phase==3)CHECK(!elden_camera_domain_resource(&s,7,9,0,0,0));
            int done=elden_camera_clocks_stop(&s);if(done)fprintf(stderr,"failed cleanup phase%u after%u at%u/%u: %d\n",phase,after,at,steps,done);
            CHECK(!done);CHECK(!peer.controllers&&!peer.dependencies);scenarios++;
        }
    }
}
static void polling(void){for(unsigned which=0;which<4;which++){setup();EldenCameraClocks s={0};CHECK(!open(&s));peer.now=0;
    int rc;if(which==3){peer.stall_domain=1;rc=elden_camera_domain_resource(&s,7,9,0,0,1);CHECK(rc==-110&&peer.now==2000);peer.stall_domain=0;
        CHECK(!elden_camera_domain_resource(&s,7,9,0,0,0));}
    else{if(which==0)peer.stall_root=1;if(which==1)peer.stall_update=1;if(which==2)peer.stall_branch=1;
        rc=clock(&s,0,EldenCamClockCci0,37500000,1);CHECK(rc<0);CHECK(peer.now==(which==2?200u:500u));
        peer.stall_root=peer.stall_update=peer.stall_branch=0;peer.cam[0x1126c/4]&=~1u;peer.cam[0x11284/4]|=1u<<31;
        CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));}
    CHECK(!elden_camera_clocks_stop(&s));scenarios++;}
    setup();EldenCameraClocks s={0};CHECK(!open(&s));CHECK(!clock(&s,0,EldenCamClockCci0,37500000,1));peer.stall_branch=1;
    CHECK(clock(&s,0,EldenCamClockCci0,37500000,0)==-71);EldenCameraClockStatus status;CHECK(!elden_camera_clocks_status(&s,&status)&&status.clock_users[EldenCamClockCci0]);
    CHECK(peer.dependencies&&peer.controllers);peer.stall_branch=0;peer.cam[0x11284/4]|=1u<<31;
    CHECK(!clock(&s,0,EldenCamClockCci0,37500000,0));CHECK(!elden_camera_clocks_stop(&s));scenarios++;
}
static void open_failures(void){setup();EldenCameraClocks ref={0};CHECK(!open(&ref));unsigned count=peer.steps;
    for(unsigned at=1;at<=count;at++){setup();EldenCameraClocks s={0};peer.fail=at;CHECK(open(&s)<0);peer.fail=0;
        if(at==1){CHECK(!peer.controllers&&!peer.writes);scenarios++;continue;}
        CHECK(!elden_camera_clocks_stop(&s)&&!peer.controllers);scenarios++;}
}
int main(void){basic();sharing();inherited();observation();domains();failures();polling();open_failures();printf("PASS actual Gen4 camera clock/domain/pin core: %u scenarios/%u checks; physical provider/MMIO peers modeled\n",scenarios,checks);return 0;}
