/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Delivery.h"
static int active(const EldenPenFrame *f){return f->tip||f->in_range||f->buttons;}
static int equal(const EldenPenFrame *a,const EldenPenFrame *b){
    return a->x==b->x&&a->y==b->y&&a->pressure==b->pressure&&
        a->tilt_x==b->tilt_x&&a->tilt_y==b->tilt_y&&a->distance==b->distance&&
        a->tip==b->tip&&a->in_range==b->in_range&&a->buttons==b->buttons;
}
static void release(EldenPenDelivery *s){
    s->dirty=0;s->has_coordinate=0;
    if(s->has_accepted&&active(&s->accepted))s->release_pending=1;
}
static int clock_step(EldenPenDelivery *s,uint64_t now){
    if(s->has_clock&&now<s->last_clock){s->clock_fault=1;release(s);}
    s->last_clock=now;s->has_clock=1;
    return s->clock_fault?EldenPenClock:0;
}
static int emit(EldenPenDelivery *s,const EldenPenFrame *f,EldenPenSend send,void *context){
    int rc=send(context,f);
    if(rc){s->failures++;return rc;}
    s->accepted=*f;s->has_accepted=1;s->sent++;return 0;
}
static int flush(EldenPenDelivery *s,EldenPenSend send,void *context){
    if(s->release_pending){
        EldenPenFrame neutral={0};
        neutral.kind=EldenPenLeave;
        neutral.x=s->accepted.x;neutral.y=s->accepted.y;
        neutral.raw_x=s->accepted.raw_x;neutral.raw_y=s->accepted.raw_y;
        int rc=emit(s,&neutral,send,context);if(rc)return rc;
        s->release_pending=0;
    }
    if(!s->dirty||s->stopped||s->clock_fault)return 0;
    if(!s->has_accepted||!equal(&s->accepted,&s->desired)){
        int rc=emit(s,&s->desired,send,context);if(rc)return rc;
    }
    s->dirty=0;return 0;
}
int elden_pen_delivery_init(EldenPenDelivery *s,uint64_t stale){
    if(!s||!stale||stale>1000000)return EldenPenDeliveryArgument;
    EldenPenDelivery value={0};value.stale_us=stale;*s=value;return 0;
}
int elden_pen_delivery_tick(EldenPenDelivery *s,uint64_t now,EldenPenSend send,void *context){
    if(!s||!send||!s->stale_us)return EldenPenDeliveryArgument;
    int fault=clock_step(s,now);
    if(s->has_coordinate&&now-s->last_coordinate>=s->stale_us)release(s);
    int rc=flush(s,send,context);return rc?rc:fault;
}
int elden_pen_delivery_feed(EldenPenDelivery *s,const EldenPenFrame *f,uint64_t now,EldenPenSend send,void *context){
    if(!s||!f||!send||!s->stale_us)return EldenPenDeliveryArgument;
    int fault=clock_step(s,now);
    if(fault){int rc=flush(s,send,context);return rc?rc:fault;}
    if(s->stopped){int rc=flush(s,send,context);return rc?rc:EldenPenStopped;}
    if(s->has_coordinate&&now-s->last_coordinate>=s->stale_us)release(s);
    if(f->kind==EldenPenLeave)release(s);
    else if(f->kind==EldenPenCoordinate){
        s->desired=*f;s->dirty=1;s->has_coordinate=1;s->last_coordinate=now;
    }else if(f->kind!=EldenPenIdentity&&f->kind!=EldenPenInfo){
        release(s);int rc=flush(s,send,context);return rc?rc:EldenPenUnsupported;
    }
    /* F0/F1 are metadata only; neither moves the pen nor refreshes its
     * contact deadline. A release must succeed before a new contact is sent. */
    return flush(s,send,context);
}
int elden_pen_delivery_fault(EldenPenDelivery *s,uint64_t now,EldenPenSend send,void *context){
    if(!s||!send||!s->stale_us)return EldenPenDeliveryArgument;
    int fault=clock_step(s,now);release(s);
    int rc=flush(s,send,context);return rc?rc:fault;
}
int elden_pen_delivery_stop(EldenPenDelivery *s,uint64_t now,EldenPenSend send,void *context){
    if(!s||!send||!s->stale_us)return EldenPenDeliveryArgument;
    s->stopped=1;return elden_pen_delivery_fault(s,now,send,context);
}
int elden_pen_delivery_released(const EldenPenDelivery *s){
    return s&&!s->release_pending&&(!s->has_accepted||!active(&s->accepted));
}
