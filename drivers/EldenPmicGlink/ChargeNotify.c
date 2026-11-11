/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ChargeNotify.h"
#include "ChargeRead.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));
}
void elden_charge_notify_init(EldenChargeNotify *s) {memset(s,0,sizeof(*s));}
void elden_charge_notify_link(EldenChargeNotify *s,bool up) {
    if(s->up==up)return;
    s->up=up;s->pending=s->registered=s->quarantined=false;
    s->reply_bytes=s->reply_type=s->reply_word=0;
    if(up)++s->epoch;
}
int elden_charge_notify_next(EldenChargeNotify *s,uint8_t out[28]) {
    if(!s->up||s->pending||s->registered||s->quarantined||!out)return 0;
    memset(out,0,28);put32(out,ELDEN_CHARGE_OWNER);put32(out+4,2);put32(out+8,4);
    s->pending=true;++s->requests;return 28;
}
void elden_charge_notify_unsent(EldenChargeNotify *s) {s->pending=false;}
void elden_charge_notify_timeout(EldenChargeNotify *s) {
    if(!s->pending)return;
    s->pending=false;s->quarantined=true;++s->timeouts;
}
int elden_charge_notify_receive(EldenChargeNotify *s,const uint8_t *p,size_t bytes) {
    if(!s->up||!s->pending||s->quarantined||!p||bytes<12||
       le32(p)!=ELDEN_CHARGE_OWNER||le32(p+8)!=4)return 0;
    s->reply_bytes=(uint32_t)bytes;s->reply_type=le32(p+4);
    s->reply_word=bytes>=16?le32(p+12):0;
    /* The OEM callback ACKs this opcode without a property result. Accept
     * its header or a zero-padded ACK, while recording the actual wire shape.
     * A nonzero/unknown extension is retained as an error, never guessed.
     */
    bool valid=(s->reply_type==1||s->reply_type==2)&&
        (bytes==12||bytes==16||bytes==28);
    for(size_t i=12;valid&&i<bytes;++i)if(p[i])valid=false;
    s->pending=false;s->registered=valid;s->quarantined=!valid;
    return valid?1:-1;
}
