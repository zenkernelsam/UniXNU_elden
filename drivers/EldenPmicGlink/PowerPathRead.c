/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "PowerPathRead.h"
#include <string.h>
/* Exact loaded stock pmic-glink-debug ABI: owner32783/read0x37/28B request,
 * 284B response. No write0x38 or arbitrary register interface exists here.
 * PMIH0104 bus0/SID7 is the stock DT virtual SPMI target. Addresses are from
 * the actual ADSP charger code and read back through stock Android. */
static const uint32_t registers[]={0x270b,0x2952,0x2a52,0x270b};
/* Stock root-PD GPIO driver: base8800 + zero-based index*100.
 * Offsets match descriptor f0425204 and Android pinctrl. This exact read
 * list was captured in stock Android, with software cursors restored. */
static const uint32_t extended_registers[]={0x270b,0x2954,0x2952,0x2a52,
    0x9104,0x9105,0x9140,0x9141,0x9144,0x9146,0x9108,
    0x9204,0x9205,0x9240,0x9241,0x9244,0x9246,0x9208,0x270b};
unsigned elden_power_reg_count(const EldenPowerPathRead *s){return s->extended?ELDEN_POWER_REG_VALUES:4u;}
void elden_power_reg_extend(EldenPowerPathRead *s){if(!s->up&&!s->requests)s->extended=true;}
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void elden_power_reg_init(EldenPowerPathRead *s){memset(s,0,sizeof(*s));}
void elden_power_reg_link(EldenPowerPathRead *s,bool up,uint32_t epoch){
    if(s->up==up&&s->epoch==epoch)return;
    bool extended=s->extended;memset(s,0,sizeof(*s));s->extended=extended;s->up=up;s->epoch=epoch;
}
void elden_power_reg_fail(EldenPowerPathRead *s,unsigned error){s->failed=true;s->active=s->pending=false;s->error=error;++s->errors;}
int elden_power_reg_begin(EldenPowerPathRead *s){
    if(!s->up||s->failed||s->active||s->pending)return 0;
    s->active=true;s->index=0;memset(s->working,0,sizeof(s->working));return 1;
}
int elden_power_reg_next(EldenPowerPathRead *s,uint8_t out[28]){
    if(!s->up||s->failed||!s->active||s->pending||!out)return 0;
    if(s->index>=elden_power_reg_count(s)){elden_power_reg_fail(s,EldenPowerRegTuple);return 0;}
    put(out,ELDEN_POWER_REG_OWNER);put(out+4,1);put(out+8,0x37);
    put(out+12,0);put(out+16,7);put(out+20,(s->extended?extended_registers:registers)[s->index]);put(out+24,1);
    s->pending=true;++s->requests;return 28;
}
void elden_power_reg_unsent(EldenPowerPathRead *s){s->pending=false;}
int elden_power_reg_receive(EldenPowerPathRead *s,const uint8_t *p,size_t bytes){
    if(!s->up||!s->pending||s->failed||!p)return 0;
    if(bytes>=4&&get(p)!=ELDEN_POWER_REG_OWNER)return 0;
    if(bytes!=284){elden_power_reg_fail(s,EldenPowerRegLength);return -1;}
    s->reply_count=get(p+24);
    if(s->reply_count&0x80000000u){elden_power_reg_fail(s,EldenPowerRegFirmware);return -1;}
    if(s->index>=elden_power_reg_count(s)||get(p+4)!=1||get(p+8)!=0x37||get(p+12)!=0||get(p+16)!=7||
       get(p+20)!=(s->extended?extended_registers:registers)[s->index]||s->reply_count!=1){elden_power_reg_fail(s,EldenPowerRegTuple);return -1;}
    s->working[s->index++]=p[28];s->pending=false;
    if(s->index<elden_power_reg_count(s))return 1;
    memcpy(s->values,s->working,sizeof(s->values));s->active=false;++s->cycles;return 2;
}
