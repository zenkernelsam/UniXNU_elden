/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Property IDs follow the stock Lenovo interface and pinned source profile,
 * not sheng's Xiaomi 0x50/temperature13/vendor gauge property layout.
 * Outputs stay raw until the board validates current direction and FG units.
 */
#include "ChargePoll.h"
#include <string.h>
_Static_assert(EldenChargeFieldCount <= 64, "one validity bit per raw field");
static const EldenChargeQuery fields[EldenChargeFieldCount] = {
    {0x30,0},{0x30,1},{0x30,2},{0x30,3},{0x30,4},{0x30,7},{0x30,9},{0x30,12},
    {0x30,14},{0x30,15},{0x30,16},{0x30,17},{0x30,19},{0x30,20},
    {0x32,0},{0x32,1},{0x32,3},{0x32,4},{0x32,5},{0x32,6},{0x32,7},
    {0x60,12},{0x60,13},{0x60,14},{0x60,15},{0x60,16},{0x60,17},
    {0x60,27},{0x60,28},{0x60,44},{0x60,45},{0x60,46},{0x60,47},{0x60,48},
    {0x60,49},{0x60,50},{0x60,52}
};
static uint32_t le32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
EldenChargeQuery elden_charge_poll_query(unsigned field) {
    return field < EldenChargeFieldCount ? fields[field] : (EldenChargeQuery){0,0};
}
void elden_charge_poll_init(EldenChargePoll *p, bool extended) {
    memset(p,0,sizeof(*p));p->fields=extended?EldenChargeFieldCount:EldenChargeUsbAdapter+1;
    p->failed_field=-1;
}
void elden_charge_poll_link(EldenChargePoll *p, bool up) {
    if (p->up==up) return; /* duplicate up is not a fresh protocol epoch */
    p->up=up;p->active=p->pending=p->refresh_wanted=false;
    p->working.valid=p->committed.valid=0;p->cursor=0;
    if (up) {++p->epoch;p->quarantined=false;p->failed_field=-1;elden_charge_poll_refresh(p);}
}
void elden_charge_poll_refresh(EldenChargePoll *p) {
    if (!p->up || p->quarantined) return;
    if (p->active) {p->refresh_wanted=true;return;}
    memset(&p->working,0,sizeof(p->working));p->working.epoch=p->epoch;
    p->working.cycle=p->cycles+1;p->cursor=0;p->active=true;p->pending=false;
}
int elden_charge_poll_next(EldenChargePoll *p, uint8_t out[24]) {
    if (!p->up || p->quarantined || !p->active || p->pending || p->cursor>=p->fields) return 0;
    if (elden_charge_read_request(fields[p->cursor],out)) return -1;
    p->pending=true;return 24;
}
void elden_charge_poll_unsent(EldenChargePoll *p) {p->pending=false;}
static void stop(EldenChargePoll *p) {
    p->quarantined=true;p->active=p->pending=p->refresh_wanted=false;
    p->failed_field=(int)p->cursor;p->working.valid=p->committed.valid=0;
}
void elden_charge_poll_timeout(EldenChargePoll *p) {
    if (!p->pending) return;
    ++p->timeouts;stop(p);
}
int elden_charge_poll_receive(EldenChargePoll *p,const uint8_t *data,size_t bytes) {
    if (!p->up || p->quarantined) return EldenChargePollStopped;
    if (!data || bytes<12 || le32(data)!=ELDEN_CHARGE_OWNER) {++p->ignored;return 0;}
    if (bytes==16 && le32(data+4)==2 && le32(data+8)==7) {
        ++p->notifications;elden_charge_poll_refresh(p);return EldenChargePollNotified;
    }
    if (!p->pending || !p->active || p->cursor>=p->fields || le32(data+4)!=1 ||
        le32(data+8)!=fields[p->cursor].opcode || bytes<16 ||
        le32(data+12)!=fields[p->cursor].property) {++p->ignored;return 0;}
    EldenChargeValue value;
    int result=elden_charge_read_reply(fields[p->cursor],data,bytes,&value);
    if (result==EldenChargeLength || result==EldenChargeArgument || result==EldenChargeMismatch) {
        ++p->errors;stop(p);return EldenChargePollStopped;
    }
    if (!result) {
        p->working.value[p->cursor]=value;p->working.valid|=UINT64_C(1)<<p->cursor;++p->responses;
    } else ++p->errors; /* explicit firmware error is a completed query with no valid value */
    p->pending=false;
    if (++p->cursor<p->fields) return EldenChargePollAnswer;
    p->committed=p->working;++p->cycles;p->active=false;
    bool again=p->refresh_wanted;p->refresh_wanted=false;
    if (again) elden_charge_poll_refresh(p);
    return EldenChargePollCycle;
}
