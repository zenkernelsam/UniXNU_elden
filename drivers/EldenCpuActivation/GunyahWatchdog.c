/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "GunyahWatchdog.h"

static int invoke(EldenVdog *s,EldenVdogCall call,void *arg,
                  uint32_t op,uint32_t x1,uint32_t x2,EldenVdogReply *reply)
{
    *reply=call(arg,op,x1,x2);
    if(s->trace_count<8){
        EldenVdogTrace *t=&s->trace[s->trace_count++];
        t->op=op;t->arg1=x1;t->arg2=x2;
        t->a0=reply->a0;t->a1=reply->a1;t->a2=reply->a2;t->a3=reply->a3;
    }else s->trace_overflow++;
    s->last_op=op;s->calls++;
    s->last_error=(int32_t)(uint32_t)reply->a0;
    return s->last_error;
}

int elden_vdog_status(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call)return ELDEN_VDOG_BAD_STATE;
    EldenVdogReply r;
    int rc=invoke(s,call,arg,ELDEN_VDOG_STATUS,0,0,&r);
    if(rc)return rc;
    /* Bit31 means the bite action is queued, not necessarily that its deadline
     * has passed. Equal bark/bite queues bite immediately in Gunyah; use the
     * OEM's positive bark-to-bite interval. Never copy status into CONTROL. */
    if(r.a1>UINT32_MAX||r.a2>UINT32_MAX){s->last_error=ELDEN_VDOG_BAD_STATUS;return ELDEN_VDOG_BAD_STATUS;}
    s->control=(uint32_t)r.a1;s->expired=(s->control>>31)&1;
    s->elapsed_ms=(uint32_t)r.a2;
    return ELDEN_VDOG_OK;
}

int elden_vdog_stop(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call||!s->enable_attempted)return ELDEN_VDOG_BAD_STATE;
    if(s->state==ELDEN_VDOG_STOPPED)return ELDEN_VDOG_OK;
    EldenVdogReply r;
    int rc=elden_vdog_status(s,call,arg);
    if(!rc&&!(s->control&1)){s->state=ELDEN_VDOG_STOPPED;return ELDEN_VDOG_OK;}
    /* We may disable only after this context attempted enable. There is no
     * stop/reset of a pre-existing active watchdog and no retry loop. */
    rc=invoke(s,call,arg,ELDEN_VDOG_CONTROL,2,0,&r);
    if(rc){s->state=ELDEN_VDOG_UNCERTAIN;return rc;}
    rc=elden_vdog_status(s,call,arg);
    if(rc||(s->control&1)){s->state=ELDEN_VDOG_UNCERTAIN;return rc?rc:ELDEN_VDOG_BAD_STATUS;}
    s->state=ELDEN_VDOG_STOPPED;
    return ELDEN_VDOG_OK;
}

int elden_vdog_arm(EldenVdog *s,EldenVdogCall call,void *arg,int recovery_ready)
{
    if(!s||!call||s->attempted||s->state!=ELDEN_VDOG_IDLE)return ELDEN_VDOG_BAD_STATE;
    if(recovery_ready!=1)return ELDEN_VDOG_NOT_READY;
    s->attempted=1;
    int rc=elden_vdog_status(s,call,arg);
    if(rc){s->state=ELDEN_VDOG_REFUSED;return rc;}
    if((s->control&1)||s->expired){s->state=ELDEN_VDOG_REFUSED;return ELDEN_VDOG_BUSY;}
    EldenVdogReply r;
    rc=invoke(s,call,arg,ELDEN_VDOG_TIME,ELDEN_VDOG_BARK_MS,ELDEN_VDOG_TIMEOUT_MS,&r);
    if(rc){s->state=ELDEN_VDOG_REFUSED;return rc;}
    s->enable_attempted=1;
    rc=invoke(s,call,arg,ELDEN_VDOG_CONTROL,3,0,&r);
    /* Lenovo qcom_wdt_init enables and immediately resets the counter. A
     * one-shot initial PET establishes the epoch; the trial never pets again. */
    if(!rc)rc=invoke(s,call,arg,ELDEN_VDOG_PET,0,0,&r);
    if(!rc)rc=elden_vdog_status(s,call,arg);
    if(!rc&&(!(s->control&1)||s->expired||s->elapsed_ms>=ELDEN_VDOG_TIMEOUT_MS))rc=ELDEN_VDOG_BAD_STATUS;
    if(rc){
        /* Preserve the original failure as return value; stop result/state
         * records whether returning to disabled was actually confirmed. */
        s->state=ELDEN_VDOG_UNCERTAIN;
        (void)elden_vdog_stop(s,call,arg);
        return rc;
    }
    s->state=ELDEN_VDOG_ARMED;
    return ELDEN_VDOG_OK;
}

int elden_vdog_pet(EldenVdog *s,EldenVdogCall call,void *arg)
{
    if(!s||!call||s->state!=ELDEN_VDOG_ARMED)return ELDEN_VDOG_BAD_STATE;
    EldenVdogReply r;
    int rc=invoke(s,call,arg,ELDEN_VDOG_PET,0,0,&r);
    if(!rc)rc=elden_vdog_status(s,call,arg);
    if(!rc&&(!(s->control&1)||s->expired||s->elapsed_ms>=ELDEN_VDOG_TIMEOUT_MS))rc=ELDEN_VDOG_BAD_STATUS;
    if(rc)s->state=ELDEN_VDOG_UNCERTAIN;
    return rc;
}
