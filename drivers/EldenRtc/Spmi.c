/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Spmi.h"
#include "TransactionGate.h"
enum {RtcVersion=0x70020000u,RtcApids=896,RtcPpid=0x061};
static int live(EldenRtcSpmi *r){
    uint32_t version,map,owner;int rc;
    if((rc=r->io.read32(r->io.context,EldenRtcSpmiCore,0,&version))||
       (rc=r->io.read32(r->io.context,EldenRtcSpmiCore,0x2000+4*r->apid,&map))||
       (rc=r->io.read32(r->io.context,EldenRtcSpmiOwner,4*r->apid,&owner)))return rc;
    return version==RtcVersion&&map&&((map>>8)&0xfff)==RtcPpid&&(owner&7)==r->owner?0:-16;
}
int elden_rtc_spmi_select(EldenRtcSpmi *r){
    if(!r||!r->io.read32||!r->io.write32||!r->io.delay_us||r->selected||r->uncertain)return -22;
    uint32_t version,count;int rc=r->io.read32(r->io.context,EldenRtcSpmiCore,0,&version);if(rc)return rc;
    rc=r->io.read32(r->io.context,EldenRtcSpmiCore,4,&count);if(rc)return rc;
    if(version!=RtcVersion||(count&0x7ff)!=RtcApids)return -19;
    unsigned matches=0,owned=0,selected=0,selected_owner=0,only=0,only_owner=0;
    for(unsigned i=0;i<RtcApids;++i){
        uint32_t map,owner;rc=r->io.read32(r->io.context,EldenRtcSpmiCore,0x2000+4*i,&map);if(rc)return rc;
        if(!map||((map>>8)&0xfff)!=RtcPpid)continue;
        rc=r->io.read32(r->io.context,EldenRtcSpmiOwner,4*i,&owner);if(rc)return rc;
        ++matches;only=i;only_owner=owner&7;
        if(!(owner&7)){++owned;selected=i;selected_owner=0;}
    }
    if(owned==1){r->apid=selected;r->owner=selected_owner;}
    else if(!owned&&matches==1){r->apid=only;r->owner=only_owner;}
    else return -16;
    r->selected=1;return 0;
}
int elden_rtc_spmi_read(void *p,uint16_t address,uint8_t *out,size_t bytes){
    EldenRtcSpmi *r=p;
    if(!r||!r->selected||r->uncertain||!out||!bytes||bytes>4||
       !((address==0x6146&&bytes==1)||(address==0x6148&&(bytes==1||bytes==4))))return -22;
    int rc=elden_spmi_transaction_acquire(EldenSpmiRtcOwner,r->io.context,r->io.delay_us);if(rc)return rc;
    uint8_t value[4];int uncertain=0;rc=live(r);if(rc)goto done;
    rc=r->io.write32(r->io.context,EldenRtcSpmiObserver,0,(1u<<27)|((uint32_t)(address&255)<<4)|(uint32_t)(bytes-1));
    if(rc){uncertain=1;goto done;}++r->commands;
    for(unsigned i=0;i<1000;++i){
        uint32_t status;rc=r->io.read32(r->io.context,EldenRtcSpmiObserver,8,&status);
        if(rc){uncertain=1;goto done;}
        if(status&1){
            if(status&0xe){rc=-5;goto done;}
            uint32_t data;rc=r->io.read32(r->io.context,EldenRtcSpmiObserver,0x18,&data);if(rc)goto done;
            for(unsigned n=0;n<bytes;++n)value[n]=(uint8_t)(data>>(n*8));rc=0;goto done;
        }
        r->io.delay_us(r->io.context,1);
    }
    rc=-110;uncertain=1;
done:
    if(uncertain)r->uncertain=1;
    {int release=elden_spmi_transaction_release(EldenSpmiRtcOwner,uncertain!=0);if(!rc)rc=release;}
    if(!rc)for(unsigned i=0;i<bytes;++i)out[i]=value[i];return rc;
}
