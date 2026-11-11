/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "UsbIcl.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
void elden_usb_icl_init(EldenUsbIcl *s){memset(s,0,sizeof(*s));}
void elden_usb_icl_sdp_only(EldenUsbIcl *s,bool enabled){if(!s->up&&!s->requests)s->sdp_only=enabled;}
void elden_usb_icl_link(EldenUsbIcl *s,bool up){
    if(s->up==up)return;
    s->up=up;s->pending=s->quarantined=s->suspend_requested=s->deferred=false;s->ret_code=0;s->applied=s->request_cycle=0;
    if(up)++s->epoch; /* retain ownership until the known default is restored */
}
void elden_usb_icl_suspend(EldenUsbIcl *s,bool suspend){s->suspend_requested=suspend;}
int elden_usb_icl_next(EldenUsbIcl *s,const EldenChargeRawSnapshot *raw,
    const EldenUcsiSnapshot *ucsi,bool fresh,bool notified,uint32_t budget,uint8_t out[24]){
    if(!s->up||s->pending||s->quarantined||!out)return 0;
    uint32_t want=0;bool have=false;
    bool allowed=raw&&ucsi&&fresh&&notified&&raw->cycle>=4&&raw->epoch==s->epoch&&ucsi->epoch==s->epoch&&
        raw->valid==((UINT64_C(1)<<EldenChargeFieldCount)-1)&&ucsi->connectors==2;
    bool typeKnown=allowed&&ucsi->port[0].status_valid&&ucsi->port[0].connected&&
        !ucsi->port[0].source&&ucsi->port[0].usb&&ucsi->port[0].power_mode==1&&
        raw->value[EldenChargeUsbOnline].raw==1&&raw->value[EldenChargeUsbAdapter].raw==1&&
        raw->value[EldenChargeUsbRealType].raw==1;
    /* The stock PD branch interprets 100mA as vote removal, not a fixed safe
     * limit. Never send that fallback based on a mixed/PD/stale snapshot.
     * Normal SDP reductions, chemistry checks and ownership remain below. */
    s->deferred=s->sdp_only&&!typeKnown;
    if(s->deferred)return 0;
    if(allowed){
        const EldenUcsiPort *p=&ucsi->port[0];
        allowed=p->status_valid&&p->connected&&!p->source&&p->usb&&p->power_mode==1&&
            raw->value[EldenChargeUsbOnline].raw==1&&raw->value[EldenChargeUsbAdapter].raw==1&&
            raw->value[EldenChargeUsbRealType].raw==1&&raw->value[EldenChargePresent].raw==1&&
            raw->value[EldenChargeUsbMax].raw>=500000&&
            raw->value[EldenChargeHealth].raw==1&&raw->value[EldenChargeInputSuspended].raw==0&&
            /* USB also powers the system during bypass or a charge-window
             * hold. Battery charging permission must not revoke its budget. */
            raw->value[EldenChargeVoltage].raw>=3500000&&
            raw->value[EldenChargeVoltage].raw<=4500000&&raw->value[EldenChargeTemperature].signed_value>=0&&
            raw->value[EldenChargeTemperature].signed_value<=4500;
        for(unsigned i=EldenChargeFg1Voltage;allowed&&i<=EldenChargeFg2Voltage;++i){
            uint32_t v=raw->value[i].raw,b=raw->value[EldenChargeVoltage].raw;
            if(v<3500000||v>4500000||(v>b?v-b:b-v)>200000)allowed=false;
        }
    }
    if(allowed&&budget==500000){
        uint32_t limit=raw->value[EldenChargeUsbLimit].raw;
        if(!s->owns_limit&&limit!=100000)return 0; /* preserve a foreign policy */
        want=s->suspend_requested&&s->owns_limit?0:500000;have=true;
    }else if(s->owns_limit){want=budget==2000?2000:100000;have=true;}
    if(!have)return 0;
    if(want==s->applied){
        /* An ACK is not permanent hardware state: PD/source transitions can
         * reinstate the firmware default without changing the GLINK epoch.
         * Wait beyond the in-flight GET cycle, then reconcile only known
         * defaults or a budget reduction. Never reopen an unexplained zero. */
        bool later=raw&&fresh&&raw->epoch==s->epoch&&raw->cycle>s->request_cycle&&raw->cycle-s->request_cycle>=2;
        uint32_t observed=raw?raw->value[EldenChargeUsbLimit].raw:0;
        bool reset=want==500000&&allowed&&(observed==100000||observed==2000);
        bool lower=(want==100000||want==2000)&&observed>want;
        if(!s->owns_limit||!later||(!reset&&!lower))return 0;
    }
    memset(out,0,24);put32(out,ELDEN_CHARGE_OWNER);put32(out+4,1);put32(out+8,0x33);
    put32(out+16,5);put32(out+20,want);s->requested=want;s->request_cycle=raw?raw->cycle:0;s->pending=true;++s->requests;
    return 24;
}
void elden_usb_icl_unsent(EldenUsbIcl *s){s->pending=false;}
void elden_usb_icl_timeout(EldenUsbIcl *s){
    if(!s->pending)return;
    s->pending=false;s->quarantined=true;++s->timeouts;
    if(s->requested==500000)s->owns_limit=true;
}
int elden_usb_icl_receive(EldenUsbIcl *s,const uint8_t *p,size_t bytes){
    if(!s->up||!s->pending||s->quarantined||!p||bytes<12||le32(p)!=ELDEN_CHARGE_OWNER||le32(p+8)!=0x33)return 0;
    if(bytes!=24||le32(p+4)!=1||le32(p+12)!=5){s->quarantined=true;s->pending=false;s->ret_code=UINT32_MAX;if(s->requested==500000)s->owns_limit=true;return -1;}
    s->pending=false;s->ret_code=le32(p+20);
    if(s->ret_code){s->quarantined=true;return -1;}
    s->applied=s->requested;s->owns_limit=s->owns_limit||s->applied==500000;++s->acks;return 1;
}
