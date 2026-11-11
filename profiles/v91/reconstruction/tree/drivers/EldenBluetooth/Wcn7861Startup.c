/* SPDX-License-Identifier: GPL-2.0-only */
/* Sequence adapted from the exact TB322FC hci_qca peripheral patch0227.
 * H4 framing uses canonical PaganiBt; board clocks/power stay with its owner.
 */
#include "Wcn7861Startup.h"
#include <string.h>
enum {StagePrepared=1,StageWake=2,StageVersion=3,StageBaud=4,StageArbitrate=5,
      StagePeriPatch=6,StagePeriNvm=7,StagePeriReset=8,StageTmel=9,StageTmelVersion=10,
      StageTmelPatch=11,StageActivate=12,StageReady=13};
static int fail(EldenWcnStartup *s,int error){
    s->error=error;s->poisoned=true;s->ready=false;
    if(s->io.report)s->io.report(s->io.context,s->stage,error);
    return error;
}
static void stage(EldenWcnStartup *s,unsigned value){s->stage=value;if(s->io.report)s->io.report(s->io.context,value,0);}
static int send(EldenWcnStartup *s,const uint8_t *bytes,size_t count){
    if(s->poisoned)return s->error?s->error:EldenWcnStartupProtocol;
    int error=s->io.write(s->io.context,bytes,count,2000);if(error)return fail(s,error);s->writes++;return 0;
}
static int frame(EldenWcnStartup *s){
    if(s->position==s->received){
        s->received=s->io.read(s->io.context,s->receive,sizeof(s->receive));s->position=0;
        if(s->received>sizeof(s->receive))return fail(s,EldenWcnStartupProtocol);
        if(!s->received)return 0;
    }
    while(s->position<s->received){
        uint8_t b=s->receive[s->position++];int result=pb_h4_rx_byte(&s->h4,b);
        if(result==PbH4Oversize)return fail(s,EldenWcnStartupProtocol);
        if(result==PbH4Ibs){
            if(b==0xfc){s->wake_ack=true;s->sleeping=false;}
            else if(b==0xfd){s->sleeping=false;uint8_t ack=0xfc;if(send(s,&ack,1))return s->error;}
            else if(b==0xfe)s->sleeping=true;
        }else if(result==PbH4Frame){
            s->frames++;
            unsigned notify=elden_wcn_peripheral_notification(s->h4.buf,s->h4.have);
            if(notify){if(notify==s->expected_notification)s->notifications|=1u<<notify;continue;}
            if(s->h4.buf[0]==4&&s->h4.have>=3&&s->h4.buf[1]==0x10)return fail(s,EldenWcnStartupProtocol);
            return 1;
        }else if(result==PbH4Junk){if(++s->ignored>512)return fail(s,EldenWcnStartupProtocol);}
    }
    return 0;
}
/* Consume already queued input before a new command: a late command reply
 * cannot be reused as the next segment's acknowledgement. Notifications/IBS
 * are handled, any unawaited peripheral completion stops this boot instance.
 */
static int quiet_input(EldenWcnStartup *s){
    for(unsigned n=0;n<32;n++){
        int result=frame(s);if(result<0)return result;
        if(result==1&&s->h4.buf[0]==0x34)return fail(s,EldenWcnStartupProtocol);
        if(!result&&s->position==s->received)return 0;
    }
    return fail(s,EldenWcnStartupProtocol);
}
static int reply(EldenWcnStartup *s,EldenWcnPeripheralExpected expected,EldenWcnPeripheralReply *out){
    uint64_t start=s->io.now_ms(s->io.context);
    for(unsigned spins=0;spins<6000;spins++){
        int result=frame(s);if(result<0)return result;
        if(result==1){
            if(s->h4.buf[0]!=0x34){s->ignored++;continue;}
            int error=elden_wcn_peripheral_reply(expected,s->h4.buf,s->h4.have,out);
            if(error)return fail(s,EldenWcnStartupProtocol);
            return 0;
        }
        if(s->io.now_ms(s->io.context)-start>=3000)return fail(s,EldenWcnStartupTimeout);
        s->io.delay_ms(s->io.context,1);
    }
    return fail(s,EldenWcnStartupTimeout);
}
static int control(EldenWcnStartup *s,enum EldenWcnPeripheralControl which,EldenWcnPeripheralReply *out){
    size_t bytes=0;int error=elden_wcn_peripheral_control(which,s->packet,sizeof(s->packet),&bytes);
    if(error)return fail(s,error);
    if((error=quiet_input(s)))return error;
    EldenWcnPeripheralExpected expected={(uint16_t)(s->packet[2]|(uint16_t)s->packet[3]<<8),s->packet[5],bytes>6?s->packet[6]:0};
    if(which==EldenWcnBaud8M){if((error=s->io.rfr(s->io.context,false)))return fail(s,error);}
    if((error=send(s,s->packet,bytes)))return error;
    if(which==EldenWcnBaud8M){
        s->io.delay_ms(s->io.context,20);
        if((error=s->io.baud(s->io.context,8000000)))return fail(s,error);
        s->baud=8000000;if((error=s->io.rfr(s->io.context,true)))return fail(s,error);
    }
    return reply(s,expected,out);
}
static int wake(EldenWcnStartup *s){
    if(quiet_input(s))return s->error;
    s->wake_ack=false;uint8_t b=0xfd;if(send(s,&b,1))return s->error;
    uint64_t start=s->io.now_ms(s->io.context);
    for(unsigned spins=0;spins<200;spins++){
        int result=frame(s);if(result<0)return result;
        if(s->wake_ack)return 0;
        if(result==1)return fail(s,EldenWcnStartupProtocol);
        if(s->io.now_ms(s->io.context)-start>=100)return fail(s,EldenWcnStartupTimeout);
        s->io.delay_ms(s->io.context,1);
    }
    return fail(s,EldenWcnStartupTimeout);
}
static int notification(EldenWcnStartup *s,unsigned which){
    uint64_t start=s->io.now_ms(s->io.context);
    for(unsigned spins=0;spins<6000;spins++){
        if(s->notifications&(1u<<which)){s->expected_notification=0;return 0;}
        int result=frame(s);if(result<0)return result;
        if(result==1)return fail(s,EldenWcnStartupProtocol);
        if(s->io.now_ms(s->io.context)-start>=3000)return fail(s,EldenWcnStartupTimeout);
        s->io.delay_ms(s->io.context,1);
    }
    return fail(s,EldenWcnStartupTimeout);
}
static int download(EldenWcnStartup *s,unsigned index){
    const EldenWcnPeripheralPlan *p=&s->firmware[index];
    for(size_t offset=0;offset<p->size;){
        if(quiet_input(s))return s->error;
        size_t bytes=0;bool wait=false;int error=elden_wcn_peripheral_segment(p,offset,s->packet,sizeof(s->packet),&bytes,&wait);
        if(error)return fail(s,error);
        if(send(s,s->packet,bytes))return s->error;
        if(wait){
            EldenWcnPeripheralReply out;EldenWcnPeripheralExpected expected={0xfff0,6,p->kind==EldenWcnTmel?3:0};
            if(reply(s,expected,&out))return s->error;
            if(out.status)return fail(s,EldenWcnStartupProtocol);
        }
        offset+=bytes-8;
    }
    return 0;
}
int elden_wcn_startup_prepare(EldenWcnStartup *out,const EldenWcnStartupIo *io,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes,const uint8_t *tmel,size_t tmel_bytes){
    if(!out||!io||!io->write||!io->read||!io->now_ms||!io->delay_ms||!io->rfr||!io->baud)return EldenWcnStartupArgument;
    EldenWcnPeripheralPlan plan[3];int error;
    if((error=elden_wcn_peripheral_prepare(EldenWcnPatch,patch,patch_bytes,&plan[0]))||
       (error=elden_wcn_peripheral_prepare(EldenWcnNvm,nvm,nvm_bytes,&plan[1]))||
       (error=elden_wcn_peripheral_prepare(EldenWcnTmel,tmel,tmel_bytes,&plan[2])))return error;
    if(out->poisoned)return EldenWcnStartupProtocol;
    memset(out,0,sizeof(*out));out->io=*io;memcpy(out->firmware,plan,sizeof(plan));out->baud=115200;
    pb_h4_rx_reset(&out->h4);stage(out,StagePrepared);return 0;
}
int elden_wcn_startup_run(EldenWcnStartup *s){
    if(!s||s->stage!=StagePrepared||s->poisoned)return EldenWcnStartupArgument;
    EldenWcnPeripheralReply r;stage(s,StageWake);if(wake(s))return s->error;
    stage(s,StageVersion);if(control(s,EldenWcnVersion,&r))return s->error;
    if(r.status||!r.supported_v2)return fail(s,EldenWcnStartupIdentity);
    s->identity=r;
    stage(s,StageBaud);if(control(s,EldenWcnBaud8M,&r))return s->error;if(r.status!=1)return fail(s,EldenWcnStartupProtocol);
    stage(s,StageArbitrate);if(control(s,EldenWcnArbitrate,&r))return s->error;
    if(r.status==0){
        stage(s,StagePeriPatch);if(download(s,0))return s->error;
        stage(s,StagePeriNvm);if(download(s,1))return s->error;
        stage(s,StagePeriReset);s->notifications&=~(1u<<1);s->expected_notification=1;
        if(control(s,EldenWcnReset,&r))return s->error;
        if(r.status)return fail(s,EldenWcnStartupProtocol);
        if(notification(s,1))return s->error;
    }else if(r.status!=23)return fail(s,EldenWcnStartupProtocol);
    stage(s,StageTmel);if(wake(s))return s->error;if(control(s,EldenWcnSelectTmel,&r))return s->error;
    if(r.status==0||r.status==23){
        bool patched=r.status==23;stage(s,StageTmelVersion);
        if(control(s,EldenWcnTmelVersion,&r))return s->error;
        if(r.status||!r.supported_v2)return fail(s,EldenWcnStartupIdentity);
        if(!patched){
            stage(s,StageTmelPatch);s->notifications&=~(1u<<2);s->expected_notification=2;
            if(download(s,2)||notification(s,2))return s->error;
        }
    }else if(r.status!=5&&r.status!=6)return fail(s,EldenWcnStartupProtocol);
    stage(s,StageActivate);s->notifications&=~(1u<<3);s->expected_notification=3;
    if(control(s,EldenWcnActivate,&r))return s->error;
    if(r.status==0){if(notification(s,3))return s->error;}else if(r.status!=23)return fail(s,EldenWcnStartupProtocol);
    s->ready=true;stage(s,StageReady);return 0;
}

/* Private continuation API. Preserve the live framer and queued bytes when
 * ordinary HCI takes ownership after peripheral activation. */
int elden_wcn_wire_fail(EldenWcnStartup *s,int error){
    return s?fail(s,error):EldenWcnStartupArgument;
}
int elden_wcn_wire_send(EldenWcnStartup *s,const uint8_t *p,size_t n){
    return s&&p&&n?send(s,p,n):EldenWcnStartupArgument;
}
int elden_wcn_wire_frame(EldenWcnStartup *s){
    return s&&!s->poisoned?frame(s):s&&s->error?s->error:EldenWcnStartupArgument;
}
