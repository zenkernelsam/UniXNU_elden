/* SPDX-License-Identifier: GPL-2.0-only */
/* Firmware metadata and WCN7861 NVM exceptions follow TB322FC Linux
 * 6f97250ead155fc3b980856323e99a4623dae73d, drivers/bluetooth/btqca.c.
 * Canonical PaganiBt c888efd3 supplies the H4 and EDL packet codecs.
 * Only unified Command Complete is supported for this exact Peach20 board.
 */
#include "Wcn7861Hci.h"
#include "Wcn7861Wire.h"
#include "BtTlv.h"
#include <string.h>

static uint16_t le16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p){return (uint32_t)le16(p)|((uint32_t)le16(p+2)<<16);}
static bool range(const void *p,size_t n){return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static bool overlap(const void *a,size_t n,const void *b,size_t m){
    if(!range(a,n)||!range(b,m))return true;
    return (uintptr_t)a<(uintptr_t)b+m&&(uintptr_t)b<(uintptr_t)a+n;
}
static bool address_valid(const uint8_t a[6]){
    if(!a)return false;
    unsigned zero=0,ones=0;for(unsigned i=0;i<6;i++){zero+=a[i]==0;ones+=a[i]==255;}
    uint32_t lap=(uint32_t)a[0]|((uint32_t)a[1]<<8)|((uint32_t)a[2]<<16);
    return zero!=6&&ones!=6&&!(lap>=0x9e8b00&&lap<=0x9e8b3f);
}
int elden_wcn_hci_firmware_prepare(EldenWcnHciFirmware *out,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes){
    if(!out||!range(patch,patch_bytes)||!range(nvm,nvm_bytes)||
       overlap(out,sizeof(*out),patch,patch_bytes)||overlap(out,sizeof(*out),nvm,nvm_bytes))
        return EldenWcnStartupArgument;
    if(patch_bytes<28||patch_bytes>524288||nvm_bytes<4||nvm_bytes>65536)
        return EldenWcnHciFirmwareError;
    if(patch[0]!=1||(le32(patch)>>8)!=patch_bytes-4||patch[12]!=1||
       patch[13]!=0||patch[14]!=3||le16(patch+16)!=0x21||le16(patch+18)!=0x200||
       !le32(patch+8)||le32(patch+8)>patch_bytes-28||
       le32(patch+4)>patch_bytes-4||le32(patch+4)<le32(patch+8)||
       nvm[0]!=2||(le32(nvm)>>8)!=nvm_bytes-4)
        return EldenWcnHciFirmwareError;
    EldenWcnHciFirmware p={0};
    p.patch=patch;p.patch_bytes=patch_bytes;p.nvm=nvm;p.nvm_bytes=nvm_bytes;
    p.product=le16(patch+16);p.rom=le16(patch+18);p.patch_version=le16(patch+20);
    p.baud_byte=p.ibs_byte=p.address_byte=SIZE_MAX;
    for(size_t at=4;at<nvm_bytes;){
        if(nvm_bytes-at<12)return EldenWcnHciFirmwareError;
        unsigned tag=le16(nvm+at);size_t n=le16(nvm+at+2);
        if(n>nvm_bytes-at-12)return EldenWcnHciFirmwareError;
        if(tag==2){
            if(n!=6||p.address_byte!=SIZE_MAX)return EldenWcnHciFirmwareError;
            p.address_byte=at+12;memcpy(p.nvm_address,nvm+at+12,6);
        }else if(tag==17){
            if(n<3||p.baud_byte!=SIZE_MAX)return EldenWcnHciFirmwareError;
            p.baud_byte=at+13;
        }else if(tag==27){
            if(n<2||p.ibs_byte!=SIZE_MAX)return EldenWcnHciFirmwareError;
            p.ibs_byte=at+13;
        }
        at+=12+n;
    }
    if(p.address_byte==SIZE_MAX||p.baud_byte==SIZE_MAX||p.ibs_byte==SIZE_MAX||
       !p.patch_version||!address_valid(p.nvm_address))
        return EldenWcnHciFirmwareError;
    *out=p;return 0;
}

int elden_wcn_hci_segment(const EldenWcnHciFirmware *p,bool nvm,size_t offset,
    const uint8_t address[6],uint8_t *packet,size_t capacity,size_t *bytes,bool *wait){
    if(!p||!packet||!bytes||!wait||(nvm&&!address_valid(address)))return EldenWcnStartupArgument;
    const uint8_t *data=nvm?p->nvm:p->patch;size_t size=nvm?p->nvm_bytes:p->patch_bytes;
    if(!range(data,size)||offset>=size||offset%PbSegmentBytes||size>524288||(nvm&&size<6)||
       (nvm&&(p->baud_byte>=size||p->ibs_byte>=size||p->address_byte>size-6)))
        return EldenWcnStartupArgument;
    size_t n=size-offset;if(n>PbSegmentBytes)n=PbSegmentBytes;
    if(capacity<n+PbEdlHeader)return EldenWcnStartupArgument;
    if(overlap(packet,n+PbEdlHeader,p->patch,p->patch_bytes)||
       overlap(packet,n+PbEdlHeader,p->nvm,p->nvm_bytes)||
       overlap(packet,n+PbEdlHeader,p,sizeof(*p))||
       (nvm&&overlap(packet,n+PbEdlHeader,address,6)))
        return EldenWcnStartupArgument;
    uint8_t temp[PbSegmentBytes+PbEdlHeader];
    unsigned encoded=pb_edl_segment(temp,data+offset,(unsigned)n);
    if(encoded!=n+PbEdlHeader)return EldenWcnStartupProtocol;
    if(nvm){
        for(size_t i=0;i<n;i++){
            size_t at=offset+i;
            if(at==p->baud_byte)temp[PbEdlHeader+i]=0x15;
            if(at==p->ibs_byte)temp[PbEdlHeader+i]|=1;
            if(at>=p->address_byte&&at-p->address_byte<6)
                temp[PbEdlHeader+i]=address[at-p->address_byte];
        }
    }
    memcpy(packet,temp,encoded);*bytes=encoded;*wait=nvm||offset+n==size;return 0;
}
static int fail(EldenWcnHci *s,int error){
    s->error=error;s->ready=false;s->poisoned=true;
    return elden_wcn_wire_fail(s->wire,error);
}
static void stage(EldenWcnHci *s,unsigned value){
    s->stage=value;s->wire->stage=value;
    if(s->wire->io.report)s->wire->io.report(s->wire->io.context,value,0);
}
/* A partial old frame is not quiescence. No new command may make its remaining
 * bytes look like a reply to another transaction. Full stale CC is refused too. */
static int quiet(EldenWcnHci *s){
    EldenWcnStartup *w=s->wire;
    for(unsigned i=0;i<32;i++){
        int rc=elden_wcn_wire_frame(w);if(rc<0)return fail(s,rc);
        if(rc==1)return fail(s,EldenWcnStartupProtocol);
        if(!rc&&w->position==w->received){
            if(w->h4.skip||(w->h4.have&&w->h4.have!=w->h4.need))
                return fail(s,EldenWcnStartupProtocol);
            return 0;
        }
    }
    return fail(s,EldenWcnStartupProtocol);
}
static int wake(EldenWcnHci *s){
    EldenWcnStartup *w=s->wire;
    if(quiet(s))return s->error;
    uint8_t byte=PbIbsWake;w->wake_ack=false;
    int rc=elden_wcn_wire_send(w,&byte,1);if(rc)return fail(s,rc);
    uint64_t start=w->io.now_ms(w->io.context);
    for(unsigned i=0;i<200;i++){
        rc=elden_wcn_wire_frame(w);if(rc<0)return fail(s,rc);
        if(rc==1)return fail(s,EldenWcnStartupProtocol);
        if(w->wake_ack)return 0;
        if(w->io.now_ms(w->io.context)-start>=100)return fail(s,EldenWcnStartupTimeout);
        w->io.delay_ms(w->io.context,1);
    }
    return fail(s,EldenWcnStartupTimeout);
}
static int send(EldenWcnHci *s,const uint8_t *p,size_t n){
    if(quiet(s))return s->error;
    if(s->wire->sleeping&&wake(s))return s->error;
    int rc=elden_wcn_wire_send(s->wire,p,n);return rc?fail(s,rc):0;
}
static int complete(EldenWcnHci *s,uint16_t opcode,int sub,size_t expected_bytes){
    EldenWcnStartup *w=s->wire;uint64_t start=w->io.now_ms(w->io.context);
    for(unsigned i=0;i<6000;i++){
        int rc=elden_wcn_wire_frame(w);if(rc<0)return fail(s,rc);
        if(rc==1){
            const uint8_t *f=w->h4.buf;uint16_t op=0;uint8_t status=255;int actual=-1;
            if(w->h4.have!=expected_bytes||f[2]!=expected_bytes-3||!f[3]||
               !pb_hci_complete(f,w->h4.have,&op,&status,&actual)||op!=opcode||
               (sub>=0&&actual!=sub)||status)
                return fail(s,EldenWcnStartupProtocol);
            s->acks++;return 0;
        }
        if(w->io.now_ms(w->io.context)-start>=3000)return fail(s,EldenWcnStartupTimeout);
        w->io.delay_ms(w->io.context,1);
    }
    return fail(s,EldenWcnStartupTimeout);
}
static int command(EldenWcnHci *s,uint16_t opcode,int sub,size_t reply_bytes){
    uint8_t packet[6];unsigned bytes=sub>=0?pb_edl_command(packet,(uint8_t)sub):
        pb_hci_command(packet,opcode,NULL,0);
    if(send(s,packet,bytes))return s->error;
    return complete(s,opcode,sub,reply_bytes);
}
static int version(EldenWcnHci *s,bool patched){
    if(command(s,PbOpEdl,PbEdlGetVer,21))return s->error;
    EldenWcnStartup *w=s->wire;PbEdlVersion v;
    /* Unified EDL carries a 12-byte version payload length here; status is byte6. */
    if(w->h4.buf[8]!=12||pb_edl_version(w->h4.buf,w->h4.have,&v)||
       (v.product!=0x21&&v.product!=0x1e)||v.build!=0x200||v.soc!=0x40210200||
       v.build!=w->identity.rom||v.soc!=w->identity.soc||
       (patched&&v.patch!=s->firmware.patch_version))
        return fail(s,EldenWcnStartupIdentity);
    s->reported_patch=v.patch;return 0;
}
static int download(EldenWcnHci *s,bool nvm){
    size_t size=nvm?s->firmware.nvm_bytes:s->firmware.patch_bytes;
    uint8_t packet[PbSegmentBytes+PbEdlHeader];
    for(size_t offset=0;offset<size;){
        size_t bytes=0;bool wait=false;
        int rc=elden_wcn_hci_segment(&s->firmware,nvm,offset,s->address,packet,sizeof(packet),&bytes,&wait);
        if(rc)return fail(s,rc);
        if(send(s,packet,bytes))return s->error;
        s->segments++;
        if(wait&&complete(s,PbOpEdl,PbEdlDownload,8))return s->error;
        offset+=bytes-PbEdlHeader;
    }
    return 0;
}
int elden_wcn_hci_prepare(EldenWcnHci *out,EldenWcnStartup *wire,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes,
    const uint8_t address[6]){
    if(!out||!wire||!address_valid(address)||out->poisoned||out->stage||
       wire->poisoned||wire->hci_owned||!wire->ready||wire->stage!=13||
       wire->baud!=8000000||!wire->identity.supported_v2||
       overlap(out,sizeof(*out),wire,sizeof(*wire))||
       overlap(out,sizeof(*out),patch,patch_bytes)||overlap(out,sizeof(*out),nvm,nvm_bytes)||
       overlap(wire,sizeof(*wire),patch,patch_bytes)||overlap(wire,sizeof(*wire),nvm,nvm_bytes))
        return EldenWcnStartupArgument;
    EldenWcnHciFirmware p;int rc=elden_wcn_hci_firmware_prepare(&p,patch,patch_bytes,nvm,nvm_bytes);
    if(rc)return rc;
    if(!memcmp(address,p.nvm_address,6))return EldenWcnStartupIdentity;
    uint8_t a[6];memcpy(a,address,6);
    memset(out,0,sizeof(*out));out->wire=wire;out->firmware=p;memcpy(out->address,a,6);
    wire->hci_owned=true;wire->ready=false;stage(out,EldenWcnHciPrepared);return 0;
}
int elden_wcn_hci_run(EldenWcnHci *s){
    if(!s||!s->wire||s->stage!=EldenWcnHciPrepared||s->poisoned||
       s->wire->poisoned||!s->wire->hci_owned)return EldenWcnStartupArgument;
    stage(s,EldenWcnHciWake);if(wake(s))return s->error;
    stage(s,EldenWcnHciVersion);if(version(s,false))return s->error;
    stage(s,EldenWcnHciPatch);if(download(s,false))return s->error;
    stage(s,EldenWcnHciNvm);if(download(s,true))return s->error;
    stage(s,EldenWcnHciReset);if(command(s,PbOpReset,-1,7))return s->error;
    stage(s,EldenWcnHciPatchedVersion);if(version(s,true))return s->error;
    stage(s,EldenWcnHciLocalVersion);if(command(s,PbOpLocalVersion,-1,15))return s->error;
    memcpy(s->local_version,s->wire->h4.buf+7,8);
    if(!s->local_version[0]||s->local_version[0]>0x20||!s->local_version[3]||
       s->local_version[3]>0x20||le16(s->local_version+4)!=0x1d)
        return fail(s,EldenWcnStartupIdentity);
    stage(s,EldenWcnHciAddress);if(command(s,PbOpBdAddr,-1,13))return s->error;
    memcpy(s->local_address,s->wire->h4.buf+7,6);
    if(memcmp(s->local_address,s->address,6))return fail(s,EldenWcnStartupIdentity);
    stage(s,EldenWcnHciHandover);if(wake(s))return s->error;
    s->ready=true;stage(s,EldenWcnHciReady);return 0;
}
