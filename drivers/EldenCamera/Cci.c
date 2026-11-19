/* SPDX-License-Identifier: GPL-2.0-only */
#include "Cci.h"
#include <stdbool.h>
#define BIT(n) (1u<<(n))
#include "CciRegisters.inc"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
enum {QUEUE_0=0,QUEUE_1=1,I2C_MODE_STANDARD=0,I2C_MODE_FAST=1,I2C_MODE_FAST_PLUS=2};
enum {EINVAL=22,EIO=5,ETIMEDOUT=110};
struct hw_params {u16 thigh,tlow,tsu_sto,tsu_sta,thd_dat,thd_sta,tbuf;u8 scl_stretch_en;u16 trdhld,tsp;};
struct cci_data {unsigned num_masters;u16 queue_size[2];struct {u16 max_write_len,max_read_len;} quirks;struct hw_params params[3];};
#include "CciV2Timing.inc"
struct cci {
    uint32_t magic,busy,state,version,controller,transactions,reads,writes,last_status;
    EldenCciIo io;const struct cci_data *data;
    uint32_t saved_timing[2][5],saved_mask;uint64_t last_clock;
    int error;uint8_t has_clock,changed,active,cleanup;
};
_Static_assert(sizeof(struct cci)<=sizeof(EldenCci),"CCI opaque storage");
enum {Magic=0x43434934};
static struct cci *core(EldenCci *s){return (struct cci *)(void *)s;}
static int admit(struct cci *s,unsigned op){return s->io.admit(s->io.context,s->io.owner,s->io.epoch,s->controller,op);}
static int failure(struct cci *s,int error){if(!s->error)s->error=error;s->state=EldenCciHeld;(void)s->io.mask(s->io.context,1);return error;}
static int read_checked(struct cci *s,uint32_t reg,uint32_t *out){return s->io.read(s->io.context,reg,out);}
static uint32_t cci_read_reg(struct cci *s,uint32_t reg){
    uint32_t value=0;if(s->error&&!s->cleanup)return 0;
    int rc=read_checked(s,reg,&value);s->reads++;if(rc){failure(s,rc);return 0;}return value;
}
static void cci_write_reg(struct cci *s,uint32_t reg,uint32_t value){
    if(s->error&&!s->cleanup)return;int rc=s->io.write(s->io.context,reg,value);s->writes++;if(rc)failure(s,rc);
}
static int write_check(struct cci *s,uint32_t reg,uint32_t value){
    int rc=s->io.write(s->io.context,reg,value);s->writes++;if(rc)return rc;uint32_t observed;
    rc=read_checked(s,reg,&observed);s->reads++;return rc?rc:observed!=value?-71:0;
}
static int clock_check(struct cci *s,uint64_t *now){
    uint64_t t=s->io.now_us(s->io.context);if(s->has_clock&&t<s->last_clock)return -110;s->has_clock=1;s->last_clock=t;*now=t;return 0;
}
static int acknowledge(struct cci *s,uint32_t observed){
    int rc=s->io.write(s->io.context,CCI_IRQ_CLEAR_0,observed);if(!rc)rc=s->io.write(s->io.context,CCI_IRQ_GLOBAL_CLEAR_CMD,1);return rc;
}
static int wait_status(struct cci *s,uint32_t wanted){
    uint64_t start,now;int rc=clock_check(s,&start);if(rc)return rc;uint32_t seen=0;
    for(unsigned n=0;n<10000;n++){
        if((rc=admit(s,s->cleanup?EldenCciCleanup:EldenCciTransfer)))return rc;
        uint32_t status=0;if((rc=read_checked(s,CCI_IRQ_STATUS_0,&status)))return rc;s->last_status=status;
        // This is an exclusive WHOLE controller. A master1 or unknown source
        // is unexpected, not our command completion. Preserve it and refuse.
        uint32_t own=CCI_IRQ_STATUS_0_I2C_M0_RD_DONE|CCI_IRQ_STATUS_0_I2C_M0_Q0_REPORT|CCI_IRQ_STATUS_0_I2C_M0_Q1_REPORT|
            CCI_IRQ_STATUS_0_RST_DONE_ACK|CCI_IRQ_STATUS_0_I2C_M0_Q0Q1_HALT_ACK|CCI_IRQ_STATUS_0_I2C_M0_ERROR;
        if(status&~own)return -71;
        if(status&&(rc=acknowledge(s,status)))return rc;seen|=status;
        if(seen&CCI_IRQ_STATUS_0_I2C_M0_ERROR)return seen&(CCI_IRQ_STATUS_0_I2C_M0_Q0_NACK_ERR|CCI_IRQ_STATUS_0_I2C_M0_Q1_NACK_ERR)?-6:-5;
        if((seen&wanted)==wanted)return 0;
        if((rc=clock_check(s,&now)))return rc;if(now-start>=100000)return -110;
        if((rc=s->io.delay_us(s->io.context,10)))return rc;
    }
    return -110;
}
static int idle(struct cci *s){
    uint32_t value;
    for(unsigned m=0;m<2;m++){
        for(unsigned q=0;q<2;q++){
            int rc=read_checked(s,CCI_I2C_Mm_Qn_CUR_WORD_CNT(m,q),&value);if(rc)return rc;if(value)return -16;
        }
        int rc=read_checked(s,CCI_I2C_Mm_READ_BUF_LEVEL(m),&value);if(rc)return rc;if(value)return -16;
    }
    return 0;
}
static int same_version(struct cci *s){uint32_t value;int rc=read_checked(s,CCI_HW_VERSION,&value);return rc?rc:value!=s->version?-116:0;}
static int configure(struct cci *s){
    const struct hw_params *h=&s->data->params[I2C_MODE_FAST];
    uint32_t timing[5]={(uint32_t)h->thigh<<16|h->tlow,(uint32_t)h->tsu_sto<<16|h->tsu_sta,
        (uint32_t)h->thd_dat<<16|h->thd_sta,h->tbuf,(uint32_t)h->scl_stretch_en<<8|(uint32_t)h->trdhld<<4|h->tsp};
    for(unsigned i=0;i<5;i++){int rc=write_check(s,0x100+4*i,timing[i]);if(rc)return rc;}
    return 0;
}
static int cci_run_queue(struct cci *s,u8 master,u8 queue){
    if(s->error)return s->error;if(master||queue>1)return -22;uint32_t count=0;
    int rc=admit(s,EldenCciTransfer);if(!rc)rc=read_checked(s,CCI_I2C_Mm_Qn_CUR_WORD_CNT(master,queue),&count);
    if(rc)return failure(s,rc);if(!count||count>=s->data->queue_size[queue])return failure(s,-71);
    // Own stale completion is rejected before a new queue starts; IRQ status
    // clearing cannot invent a completion or consume another controller's IRQ.
    uint32_t pending;if((rc=read_checked(s,CCI_IRQ_STATUS_0,&pending)))return failure(s,rc);
    if(pending)return failure(s,-71);
    if(queue==QUEUE_1){uint32_t level;if((rc=read_checked(s,CCI_I2C_Mm_READ_BUF_LEVEL(0),&level)))return failure(s,rc);if(level)return failure(s,-71);}
    if((rc=s->io.write(s->io.context,CCI_I2C_Mm_Qn_EXEC_WORD_CNT(master,queue),count)))return failure(s,rc);
    s->active=1;
    if((rc=s->io.write(s->io.context,CCI_QUEUE_START,BIT(master*2+queue))))return failure(s,rc);
    rc=wait_status(s,queue==QUEUE_0?CCI_IRQ_STATUS_0_I2C_M0_Q0_REPORT:CCI_IRQ_STATUS_0_I2C_M0_RD_DONE);
    if(rc)return failure(s,rc);s->active=0;s->transactions++;return 0;
}
static int cci_validate_queue(struct cci *s,u8 master,u8 queue){
    if(s->error)return s->error;if(master||queue>1)return -22;uint32_t value=cci_read_reg(s,CCI_I2C_Mm_Qn_CUR_WORD_CNT(master,queue));
    if(s->error)return s->error;
    // Original validates and drains prior REPORT work. This owner never adopts
    // firmware/foreign queued work, so nonempty is refusal rather than flush.
    return value>=s->data->queue_size[queue]?-22:value?-16:0;
}
#define dev_err(...) ((void)0)
#include "CciTransfer.inc"
static int enter(EldenCci *storage){
    if(!storage||core(storage)->magic!=Magic)return -22;uint32_t empty=0;
    return __atomic_compare_exchange_n(&core(storage)->busy,&empty,1u,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:-16;
}
static int leave(struct cci *s,int rc){__atomic_store_n(&s->busy,0u,__ATOMIC_RELEASE);return rc;}
static int begin(struct cci *s){
    if(s->state!=EldenCciReady)return s->error?s->error:-125;
    int rc=admit(s,EldenCciTransfer);if(!rc)rc=same_version(s);if(!rc)rc=idle(s);if(rc)return failure(s,rc);return 0;
}
int elden_camera_cci_init(EldenCci *storage,const EldenCciIo *io,unsigned controller,uint32_t clock){
    if(!storage||core(storage)->magic||!io||!io->owner||!io->epoch||!io->admit||!io->read||!io->write||!io->now_us||!io->delay_us||!io->mask||!io->drain||controller>1||clock!=37500000)return -22;
    int rc=io->admit(io->context,io->owner,io->epoch,controller,EldenCciAcquire);if(rc)return rc;
    EldenCci zero={0};*storage=zero;struct cci *s=core(storage);s->magic=Magic;s->io=*io;s->controller=controller;s->data=&cci_v2_data;
    if((rc=read_checked(s,CCI_HW_VERSION,&s->version))||!s->version||s->version==UINT32_MAX)return failure(s,rc?rc:-95);
    if((rc=idle(s))||(rc=read_checked(s,CCI_IRQ_MASK_0,&s->saved_mask)))return failure(s,rc);
    if(s->saved_mask&(CCI_IRQ_MASK_0_I2C_M1_RD_DONE|CCI_IRQ_MASK_0_I2C_M1_Q0_REPORT|CCI_IRQ_MASK_0_I2C_M1_Q1_REPORT|CCI_IRQ_MASK_0_I2C_M1_Q0Q1_HALT_ACK|CCI_IRQ_MASK_0_I2C_M1_ERROR))return failure(s,-16);
    for(unsigned m=0;m<2;m++)for(unsigned i=0;i<5;i++)if((rc=read_checked(s,0x100+0x100*m+4*i,&s->saved_timing[m][i])))return failure(s,rc);
    if((rc=s->io.mask(s->io.context,1))||(rc=s->io.drain(s->io.context)))return failure(s,rc);
    uint32_t pending;if((rc=read_checked(s,CCI_IRQ_STATUS_0,&pending)))return failure(s,rc);if(pending)return failure(s,-16);
    uint32_t mask=CCI_IRQ_MASK_0_I2C_M0_RD_DONE|CCI_IRQ_MASK_0_I2C_M0_Q0_REPORT|CCI_IRQ_MASK_0_RST_DONE_ACK|
        CCI_IRQ_MASK_0_I2C_M0_Q0Q1_HALT_ACK|CCI_IRQ_MASK_0_I2C_M0_ERROR;
    s->changed=1;
    if((rc=write_check(s,CCI_IRQ_MASK_0,mask))||(rc=s->io.write(s->io.context,CCI_RESET_CMD,CCI_RESET_CMD_MASK)))return failure(s,rc);
    if((rc=wait_status(s,CCI_IRQ_STATUS_0_RST_DONE_ACK))||(rc=configure(s))||(rc=idle(s)))return failure(s,rc);
    s->state=EldenCciReady;return 0;
}
static int write_owned(struct cci *s,uint8_t slave,const uint8_t *data,size_t n){
    if(!data||!n||n>s->data->quirks.max_write_len||!slave||slave>127)return -22;
    int rc=cci_i2c_write(s,0,slave,(u8 *)(uintptr_t)data,(u16)n);return s->error?s->error:rc;
}
static int read_owned(struct cci *s,uint8_t slave,uint8_t *data,size_t n){
    if(!data||!n||n>s->data->quirks.max_read_len||!slave||slave>127)return -22;
    int rc=cci_i2c_read(s,0,slave,data,(u16)n);return s->error?s->error:rc;
}
int elden_camera_cci_write(EldenCci *storage,uint8_t slave,const uint8_t *data,size_t n){
    if(!data||!n||n>11||!slave||slave>127)return -22;int rc=enter(storage);if(rc)return rc;struct cci *s=core(storage);
    rc=begin(s);if(!rc)rc=write_owned(s,slave,data,n);if(!rc)rc=idle(s);if(!rc)rc=admit(s,EldenCciTransfer);
    if(rc&&s->changed&&s->state!=EldenCciStopped)failure(s,rc);return leave(s,rc);
}
int elden_camera_cci_read(EldenCci *storage,uint8_t slave,uint8_t *data,size_t n){
    if(!data||!n||n>12||!slave||slave>127)return -22;int rc=enter(storage);if(rc)return rc;struct cci *s=core(storage);uint8_t bytes[12];
    rc=begin(s);if(!rc)rc=read_owned(s,slave,bytes,n);if(!rc)rc=idle(s);if(!rc)rc=admit(s,EldenCciTransfer);
    if(rc&&s->changed&&s->state!=EldenCciStopped)failure(s,rc);if(!rc)for(size_t i=0;i<n;i++)data[i]=bytes[i];return leave(s,rc);
}
int elden_camera_cci_write_register(EldenCci *s,uint8_t slave,uint16_t reg,unsigned width,uint32_t value){
    if((width!=1&&width!=2)||value>(width==1?255u:65535u))return -22;
    uint8_t bytes[4]={(uint8_t)(reg>>8),(uint8_t)reg,(uint8_t)(width==2?value>>8:value),(uint8_t)value};
    return elden_camera_cci_write(s,slave,bytes,2+width);
}
int elden_camera_cci_read_register(EldenCci *storage,uint8_t slave,uint16_t reg,unsigned width,uint32_t *out){
    if(!out||(width!=1&&width!=2)||!slave||slave>127)return -22;int rc=enter(storage);if(rc)return rc;struct cci *s=core(storage);
    uint8_t address[2]={(uint8_t)(reg>>8),(uint8_t)reg},bytes[2];
    rc=begin(s);if(!rc)rc=write_owned(s,slave,address,2);if(!rc)rc=read_owned(s,slave,bytes,width);
    if(!rc)rc=idle(s);if(!rc)rc=admit(s,EldenCciTransfer);
    if(rc&&s->changed&&s->state!=EldenCciStopped)failure(s,rc);if(!rc)*out=width==2?((uint32_t)bytes[0]<<8|bytes[1]):bytes[0];return leave(s,rc);
}
int elden_camera_cci_stop(EldenCci *storage){
    int rc=enter(storage);if(rc)return rc;struct cci *s=core(storage);
    if(s->state==EldenCciStopped)return leave(s,0);
    // An admission/preflight failure before the first owned write is not
    // enough evidence to release a possibly foreign/incomplete controller.
    if(!s->changed&&s->state==EldenCciHeld)return leave(s,s->error?s->error:-125);
    s->cleanup=1;
    rc=admit(s,EldenCciCleanup);if(!rc)rc=same_version(s);if(!rc)rc=s->io.mask(s->io.context,1);if(!rc)rc=s->io.drain(s->io.context);
    if(!rc&&s->changed){
        // Errors do not authorize an unconditional global reset. Real HALT0
        // must acknowledge before the exact master0 reset/standby sequence.
        uint32_t status;if((rc=read_checked(s,CCI_IRQ_STATUS_0,&status))==0&&status){
            uint32_t own=CCI_IRQ_STATUS_0_I2C_M0_RD_DONE|CCI_IRQ_STATUS_0_I2C_M0_Q0_REPORT|CCI_IRQ_STATUS_0_I2C_M0_Q1_REPORT|
                CCI_IRQ_STATUS_0_RST_DONE_ACK|CCI_IRQ_STATUS_0_I2C_M0_Q0Q1_HALT_ACK|CCI_IRQ_STATUS_0_I2C_M0_ERROR;
            if(status&~own)rc=-71;else rc=acknowledge(s,status);
        }
        if(!rc)rc=s->io.write(s->io.context,CCI_HALT_REQ,CCI_HALT_REQ_I2C_M0_Q0Q1);
        if(!rc)rc=wait_status(s,CCI_IRQ_STATUS_0_I2C_M0_Q0Q1_HALT_ACK);
        if(!rc)rc=s->io.write(s->io.context,CCI_RESET_CMD,CCI_RESET_CMD_M0_MASK);
        if(!rc)rc=wait_status(s,CCI_IRQ_STATUS_0_RST_DONE_ACK);
        if(!rc)rc=idle(s);
        for(unsigned m=0;m<2&&!rc;m++)for(unsigned i=0;i<5&&!rc;i++)rc=write_check(s,0x100+0x100*m+4*i,s->saved_timing[m][i]);
        if(!rc)rc=write_check(s,CCI_IRQ_MASK_0,s->saved_mask);
    }
    if(!rc)rc=s->io.drain(s->io.context);
    if(rc)failure(s,rc);else{s->state=EldenCciStopped;s->active=0;}s->cleanup=0;return leave(s,rc);
}
int elden_camera_cci_status(EldenCci *storage,EldenCciStatus *out){
    if(!out)return -22;int rc=enter(storage);if(rc)return rc;struct cci *s=core(storage);
    EldenCciStatus v={s->version,s->state,s->transactions,s->reads,s->writes,s->last_status,s->error};*out=v;return leave(s,0);
}
