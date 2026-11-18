/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Reuse the existing exact GENI/chip peer. Test the REAL mailbox, queue, AW
 * register bodies and GENI packet transfer together, not direct fake motors. */
#define main prior_bus_test_main
#include "TestBus.c"
#undef main
#include "Queue.h"
#include "Mailbox.h"
typedef struct {
    Model hardware;EldenAwChip chips[2];EldenHapticsQueue queue;EldenHapticsMailbox mail;
    unsigned connected,completions;
} Delivery;
static int delivery_admit(void *v,uint64_t owner,uint64_t epoch){
    Delivery *d=v;return d->connected&&d->mail.owner==owner&&d->mail.epoch==epoch?0:-125;
}
static int delivery_start(void *v,unsigned motor,uint8_t wave,uint8_t gain,uint32_t duration){return elden_aw_ram_start(&((Delivery *)v)->chips[motor],wave,gain,duration);}
static int delivery_poll(void *v,unsigned motor){return elden_aw_poll(&((Delivery *)v)->chips[motor]);}
static int delivery_stop(void *v,unsigned motor){return elden_aw_stop(&((Delivery *)v)->chips[motor]);}
static void delivery_complete(void *v,const EldenHapticsResult *r){Delivery *d=v;CHECK(!elden_haptics_mail_finish(&d->mail,r));d->completions++;}
static Delivery *delivery(const uint8_t *firmware,size_t bytes){
    Delivery *d=calloc(1,sizeof(*d));CHECK(d);Model *m=&d->hardware;initial(m);acquire(m);m->firmware=firmware;m->firmware_bytes=bytes;
    EldenAwIo io={m,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned i=0;i<2;i++)CHECK(!elden_aw_bind(&d->chips[i],&io,(uint8_t)(0x5a+i))&&!elden_aw_initialize(&d->chips[i],firmware,bytes,64));
    // Explicit synthetic calibration for the hardware PEER. These limits are
    // not exported as measured or written into the production native catalog.
    static const EldenHapticsClickPoint points[]={{0,{0,0}},{32767,{32,24}}};
    EldenHapticsQueueLimits limits={points,20000,100000,1000000,{100000,100000},100000,{UINT64_C(1)<<40,UINT64_C(1)<<40},64,2,{64,64},3};
    EldenHapticsQueueIo qio={d,delivery_admit,delivery_start,delivery_poll,delivery_stop,delivery_complete,NULL};
    CHECK(!elden_haptics_queue_init(&d->queue,&limits,&qio));CHECK(!elden_haptics_mail_open(&d->mail,1,2));d->connected=1;return d;
}
static void send(Delivery *d,uint64_t id,uint8_t mask){
    EldenHapticsRequest r={1,2,id,d->hardware.now,10000,mask,1,{0,0},0};
    CHECK(!elden_haptics_mail_submit(&d->mail,&r,32767));uint16_t intensity=0;
    CHECK(!elden_haptics_mail_take(&d->mail,&r,&intensity)&&intensity==32767);
    CHECK(!elden_haptics_queue_submit(&d->queue,r.owner,r.epoch,r.id,r.not_before_us,r.duration_us,r.mask,r.wave,intensity));
    CHECK(!elden_haptics_queue_step(&d->queue,d->hardware.now));
}
int main(int argc,char **argv){
    CHECK(argc==2);uint8_t firmware[4096];FILE *f=fopen(argv[1],"rb");CHECK(f);size_t bytes=fread(firmware,1,sizeof(firmware),f);CHECK(bytes==1436&&!ferror(f));CHECK(!fclose(f));
    Delivery *d=delivery(firmware,bytes);send(d,1,3);
    CHECK(d->hardware.motor[0].starts==1&&d->hardware.motor[1].starts==1&&d->queue.active_mask==3);
    CHECK(d->hardware.motor[0].reg[7]==32&&d->hardware.motor[1].reg[7]==24);
    CHECK(!elden_haptics_mail_cancel(&d->mail,1,2,1));EldenHapticsRequest r;
    CHECK(!elden_haptics_mail_next_cancel(&d->mail,&r));CHECK(!elden_haptics_queue_cancel(&d->queue,r.owner,r.epoch,r.id));
    CHECK(!elden_haptics_queue_step(&d->queue,d->hardware.now)&&d->completions==1&&!d->queue.active_mask);
    EldenHapticsResult result;CHECK(!elden_haptics_mail_result(&d->mail,1,2,1,&result)&&result.state==EldenHapticsCancelled);
    send(d,2,1);unsigned right=d->hardware.motor[1].starts;
    CHECK(!elden_haptics_mail_close(&d->mail,1,2));d->connected=0;
    CHECK(elden_haptics_mail_open(&d->mail,3,4)==-16);
    CHECK(!elden_haptics_queue_step(&d->queue,d->hardware.now)&&!d->mail.owner&&!d->mail.closing);
    CHECK(d->hardware.motor[1].starts==right&&!d->chips[0].busy&&!d->chips[1].busy);
    CHECK(!elden_haptics_queue_stop(&d->queue,d->hardware.now)&&elden_haptics_queue_terminal(&d->queue));
    CHECK(!elden_haptics_bus_release(&d->hardware.bus));free(d);
    // Unacknowledged STOP/cancel keeps the original owner and physical bus.
    d=delivery(firmware,bytes);send(d,1,3);CHECK(!elden_haptics_mail_close(&d->mail,1,2));d->connected=0;
    d->hardware.stall=d->hardware.drop_cancel=d->hardware.drop_abort=1;
    CHECK(elden_haptics_queue_step(&d->queue,d->hardware.now)<0&&d->queue.held&&d->queue.active_mask);
    CHECK(d->mail.closing&&elden_haptics_mail_open(&d->mail,3,4)==-16&&d->completions==0);
    CHECK(d->hardware.bus.held&&elden_haptics_bus_release(&d->hardware.bus)<0);free(d);
    printf("PASS: %u actual mailbox/queue/GENI/AW composition checks; both motors, close/cancel receipts and uncertain-stop retention; hardware modeled\n",checks);
}
