/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual power functions; independent register/owner/failure peer, no device. */
#include "Power.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,scenarios;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}} while(0)
typedef struct {
    uint8_t reg[32],original[32];unsigned steps,fail_step,writes,voltage_writes,delays;
    unsigned fail_after_write,deny,deny_cleanup,inactive[2],reenter,mutate_read,rail_busy;
    EldenCameraPower *core;unsigned mutated;
} Peer;
static int fail(Peer *p){return ++p->steps==p->fail_step?-5:0;}
static int gate(void *v,uint64_t owner,uint64_t epoch,int cleanup){Peer *p=v;CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));
    if(cleanup?p->deny_cleanup:p->deny)return -13;return fail(p);}
static int read_byte(void *v,uint8_t reg,uint8_t *out,int cleanup){Peer *p=v;CHECK(out&&(reg==0||reg==14||(reg>=3&&reg<=9)));CHECK(cleanup==0||cleanup==1);
    int rc=fail(p);if(rc)return rc;
    if(p->mutate_read&&reg==3&&!p->mutated){p->reg[3]++;p->mutated=1;}
    *out=p->reg[reg];return 0;}
static int write_byte(void *v,uint8_t reg,uint8_t value,int cleanup){Peer *p=v;CHECK(reg==14||(reg>=3&&reg<=9));CHECK(cleanup==0||cleanup==1);
    int rc=fail(p);if(rc&&!p->fail_after_write)return rc;
    if(reg!=14){CHECK(!(p->reg[14]&(1u<<(reg-3))));p->voltage_writes++;}
    if(p->reenter){EldenCameraPowerStatus status;CHECK(elden_camera_power_status(p->core,&status)==-16);}
    p->reg[reg]=value;p->writes++;return rc;}
static int delay(void *v,unsigned us){Peer *p=v;CHECK(us==150);p->delays++;return fail(p);}
static int inactive(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){Peer *p=v;CHECK(owner==7&&epoch==9&&sensor<2);
    int rc=fail(p);return rc?rc:p->inactive[sensor]?0:-16;}
static void setup(Peer *p,EldenCameraPower *s){memset(p,0,sizeof(*p));memset(s,0,sizeof(*s));p->core=s;p->reg[0]=0x82;
    for(unsigned i=0;i<7;i++)p->reg[i+3]=(uint8_t)(i<2?0x80|20u:140u+i);
    p->inactive[0]=p->inactive[1]=1;memcpy(p->original,p->reg,32);}
static int unused(void *v,uint64_t owner,uint64_t epoch,unsigned ldo){Peer *p=v;CHECK(owner==7&&epoch==9&&ldo>=1&&ldo<=7);int rc=fail(p);return rc?rc:p->rail_busy&(1u<<(ldo-1))?-16:0;}
static int open(Peer *p,EldenCameraPower *s){EldenCameraPowerIo io={p,7,9,gate,read_byte,write_byte,delay,inactive,unused};return elden_camera_power_open(s,&io);}
static EldenCameraPowerStatus status(EldenCameraPower *s){EldenCameraPowerStatus v={0};CHECK(!elden_camera_power_status(s,&v));return v;}
static int resource(EldenCameraPower *s,unsigned sensor,unsigned supply,int on){return elden_camera_power_resource(s,7,9,sensor,supply,on);}
static void unchanged_peer(Peer *p){CHECK(p->reg[7]==p->original[7]&&p->reg[8]==p->original[8]);}
static void restored(Peer *p,EldenCameraPower *s){EldenCameraPowerStatus v=status(s);CHECK(v.stopped&&!v.ready&&!v.changed);
    for(unsigned i=0;i<7;i++)CHECK(!v.users[i]&&!v.active_users[i]);CHECK(!memcmp(p->original,p->reg,32));unchanged_peer(p);}

static void basic(void){Peer p;EldenCameraPower s;setup(&p,&s);CHECK(!open(&p,&s));scenarios++;
    CHECK(!resource(&s,0,EldenCameraDvdd,1));CHECK(p.reg[3]==(0x80|79)&&p.reg[14]==0x81);
    unsigned writes=p.writes;CHECK(!resource(&s,0,EldenCameraDvdd,1)&&p.writes==writes);
    EldenCameraPowerStatus v=status(&s);CHECK(v.users[0]==1&&v.active_users[0]==1&&v.observed_enable==0x81);
    CHECK(elden_camera_power_resource(&s,7,10,0,EldenCameraDvdd,0)==-116);
    CHECK(elden_camera_power_stop(&s)==-16);p.inactive[0]=0;CHECK(resource(&s,0,EldenCameraDvdd,0)==-16);CHECK(p.reg[14]==0x81);
    p.inactive[0]=1;CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(p.reg[14]==0x80);
    CHECK(!elden_camera_power_stop(&s));restored(&p,&s);CHECK(!elden_camera_power_stop(&s));CHECK(open(&p,&s)==-22);
}
static void both(void){Peer p;EldenCameraPower s;setup(&p,&s);CHECK(!open(&p,&s));scenarios++;
    const unsigned supply[]={EldenCameraDovdd,EldenCameraDvdd,EldenCameraAvdd};
    for(unsigned sensor=0;sensor<2;sensor++)for(unsigned n=0;n<3;n++)CHECK(!resource(&s,sensor,supply[n],1));
    CHECK(p.reg[14]==0xcf&&p.reg[3]==(0x80|79)&&p.reg[4]==(0x80|99)&&p.reg[5]==164&&p.reg[6]==165&&p.reg[9]==37);
    CHECK(status(&s).users[6]==3);p.inactive[1]=0;
    for(unsigned n=0;n<3;n++)CHECK(!resource(&s,0,supply[n],0));
    CHECK(status(&s).users[6]==2&&(p.reg[14]&0x40));CHECK(resource(&s,1,EldenCameraDovdd,0)==-16);
    p.inactive[1]=1;for(unsigned n=0;n<3;n++)CHECK(!resource(&s,1,supply[n],0));
    CHECK(!elden_camera_power_stop(&s));restored(&p,&s);
}
static void enabled(void){for(unsigned sensor=0;sensor<2;sensor++){Peer p;EldenCameraPower s;setup(&p,&s);unsigned bit=1u<<sensor;
    p.reg[14]=(uint8_t)(0x80|0x10|bit);p.reg[sensor+3]=(uint8_t)(0x80|(sensor?99:79));memcpy(p.original,p.reg,32);
    CHECK(!open(&p,&s));unsigned writes=p.writes;CHECK(!resource(&s,sensor,EldenCameraDvdd,1));CHECK(!resource(&s,sensor,EldenCameraDvdd,0));
    CHECK(!elden_camera_power_stop(&s)&&p.writes==writes);restored(&p,&s);scenarios++;}
    Peer p;EldenCameraPower s;setup(&p,&s);p.reg[14]=0x81;memcpy(p.original,p.reg,32);CHECK(!open(&p,&s));
    CHECK(resource(&s,0,EldenCameraDvdd,1)==-34&&!p.writes);CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
}
static void external(void){Peer p;EldenCameraPower s;setup(&p,&s);CHECK(!open(&p,&s));CHECK(!resource(&s,0,EldenCameraDvdd,1));
    p.reg[14]|=0x10;CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(!elden_camera_power_stop(&s));
    CHECK(p.reg[14]==0x90&&status(&s).system_retained_for_peer&&!status(&s).changed);unchanged_peer(&p);scenarios++;
    setup(&p,&s);CHECK(!open(&p,&s));p.reg[14]=0x80;CHECK(!elden_camera_power_stop(&s)&&p.reg[14]==0x80);scenarios++;
    setup(&p,&s);CHECK(!open(&p,&s));p.reg[3]++;unsigned changed_steps=p.writes;
    CHECK(resource(&s,0,EldenCameraDvdd,1)==-116&&p.writes==changed_steps);CHECK(!elden_camera_power_stop(&s));scenarios++;
    setup(&p,&s);CHECK(!open(&p,&s));CHECK(!resource(&s,0,EldenCameraDvdd,1));p.reg[3]++;
    unsigned writes=p.writes;CHECK(resource(&s,0,EldenCameraDvdd,1)==-116&&p.writes==writes);CHECK(resource(&s,0,EldenCameraDvdd,0)==-116&&p.writes==writes);
    CHECK(elden_camera_power_stop(&s)==-16&&status(&s).users[0]);scenarios++;
    p.reg[3]=(uint8_t)(0x80|79);CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(!elden_camera_power_stop(&s));restored(&p,&s);
    setup(&p,&s);CHECK(!open(&p,&s));CHECK(!resource(&s,0,EldenCameraDvdd,1));p.reg[14]&=(uint8_t)~1u;
    CHECK(resource(&s,0,EldenCameraDvdd,1)==-116);CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
}
static void injections(void){const unsigned supplies[]={EldenCameraDovdd,EldenCameraDvdd,EldenCameraAvdd};
    // Every actual admission/read/write/delay point in open, enable, disable,
    // restore and SYS withdrawal, including writes that executed before error.
    for(unsigned stage=0;stage<4;stage++)for(unsigned sensor=0;sensor<2;sensor++)for(unsigned n=0;n<3;n++)for(unsigned after=0;after<2;after++){
        Peer reference;EldenCameraPower ref;setup(&reference,&ref);CHECK(!open(&reference,&ref));
        if(stage>=2){CHECK(!resource(&ref,sensor,supplies[n],1));if(stage==3)CHECK(!resource(&ref,sensor,supplies[n],0));}
        reference.steps=0;
        if(stage==0){setup(&reference,&ref);CHECK(!open(&reference,&ref));}
        else if(stage==1)CHECK(!resource(&ref,sensor,supplies[n],1));
        else if(stage==2)CHECK(!resource(&ref,sensor,supplies[n],0));
        else CHECK(!elden_camera_power_stop(&ref));
        unsigned length=reference.steps;
        for(unsigned at=1;at<=length;at++){
            Peer p;EldenCameraPower s;setup(&p,&s);
            if(stage){CHECK(!open(&p,&s));if(stage>=2){CHECK(!resource(&s,sensor,supplies[n],1));if(stage==3)CHECK(!resource(&s,sensor,supplies[n],0));}}
            p.steps=0;p.fail_step=at;p.fail_after_write=after;
            int rc=stage==0?open(&p,&s):stage==1?resource(&s,sensor,supplies[n],1):stage==2?resource(&s,sensor,supplies[n],0):elden_camera_power_stop(&s);
            CHECK(rc<0);EldenCameraPowerStatus v={0};int lifetime=elden_camera_power_status(&s,&v);
            if(lifetime){CHECK(stage==0&&at==1&&lifetime==-22&&!p.writes);scenarios++;continue;}
            if(v.ready&&stage==1)CHECK(!p.writes&&!v.users[sensor]&&!v.users[6]);
            p.fail_step=0;p.fail_after_write=0;
            if(stage==1||stage==2)CHECK(!resource(&s,sensor,supplies[n],0));
            CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
        }
    }
}
static void bounds(void){Peer p;EldenCameraPower s;setup(&p,&s);CHECK(!open(&p,&s));unsigned steps=p.steps;
    CHECK(elden_camera_power_resource(&s,7,9,2,EldenCameraDvdd,1)==-22);
    CHECK(resource(&s,0,EldenCameraClock,1)==-22&&resource(&s,0,EldenCameraReset,1)==-22&&resource(&s,0,99,1)==-22);
    CHECK(elden_camera_power_resource(&s,7,9,0,EldenCameraDvdd,2)==-22&&p.steps==steps);scenarios++;
    p.reenter=1;CHECK(!resource(&s,0,EldenCameraDvdd,1));CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
    setup(&p,&s);p.reg[0]=0x81;CHECK(open(&p,&s)==-19&&!p.writes);CHECK(!elden_camera_power_stop(&s));scenarios++;
    setup(&p,&s);p.reg[14]=1;CHECK(open(&p,&s)==-71&&!p.writes);CHECK(!elden_camera_power_stop(&s));scenarios++;
    setup(&p,&s);p.deny=1;CHECK(open(&p,&s)==-13&&!p.writes);scenarios++;
    setup(&p,&s);CHECK(!open(&p,&s));CHECK(!resource(&s,0,EldenCameraDvdd,1));p.deny_cleanup=1;CHECK(resource(&s,0,EldenCameraDvdd,0)==-13);CHECK(status(&s).users[0]);
    p.deny_cleanup=0;CHECK(!resource(&s,0,EldenCameraDvdd,0));CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
    setup(&p,&s);CHECK(!open(&p,&s));p.rail_busy=0x40;CHECK(resource(&s,0,EldenCameraDovdd,1)==-16&&!p.writes);p.rail_busy=0;
    CHECK(!resource(&s,0,EldenCameraDovdd,1));p.rail_busy=0x40;unsigned before=p.writes;
    CHECK(resource(&s,0,EldenCameraDovdd,0)==-16&&p.writes==before&&status(&s).users[6]);p.rail_busy=0;
    CHECK(!resource(&s,0,EldenCameraDovdd,0));CHECK(!elden_camera_power_stop(&s));restored(&p,&s);scenarios++;
}
int main(void){basic();both();enabled();external();injections();bounds();printf("PASS camera WL2868C: %u scenarios / %u checks; physical register/owner peers modeled\n",scenarios,checks);return 0;}
