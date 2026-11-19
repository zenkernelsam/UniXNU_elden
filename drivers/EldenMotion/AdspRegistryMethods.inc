/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included in the actual original ADSP worker, not a second GLINK owner. */
void clientState(void *context,QcomGlinkChannel *,int up){
    auto *c=static_cast<ShengAdspChannel *>(context);bool deliver=false;uint32_t epoch=0;
    IOLockLock(gClientLock);
    if(c->connected!=(up!=0)){
        if(c->epoch==UINT32_MAX){c->owned_status.error=-75;c->owned_status.closing=1;c->close_epoch=c->epoch;c->connected=false;}
        else {c->connected=up!=0;c->epoch++;}
        c->head=c->count=0;
    }
    epoch=c->epoch;
    if(!c->owned_status.closing&&!c->owned_status.error&&((c->epoch_callbacks&&c->epoch_client.state)||(!c->epoch_callbacks&&c->client.state))){
        c->owned_status.callbacks++;deliver=true;
    }
    IOLockUnlock(gClientLock);
    if(deliver){
        if(c->epoch_callbacks)c->epoch_client.state(c->context,epoch,up);else c->client.state(c->context,up);
        IOLockLock(gClientLock);c->owned_status.callbacks--;IOLockUnlock(gClientLock);
    }
}
void clientReceive(void *context,QcomGlinkChannel *,const uint8_t *data,uint32_t length){
    auto *c=static_cast<ShengAdspChannel *>(context);bool deliver=false;uint32_t epoch=0;
    IOLockLock(gClientLock);epoch=c->epoch;
    if(c->connected&&!c->owned_status.closing&&!c->owned_status.error&&((c->epoch_callbacks&&c->epoch_client.receive)||(!c->epoch_callbacks&&c->client.receive))){
        c->owned_status.callbacks++;deliver=true;
    }
    IOLockUnlock(gClientLock);
    if(deliver){
        if(c->epoch_callbacks)c->epoch_client.receive(c->context,epoch,data,length);else c->client.receive(c->context,data,length);
        IOLockLock(gClientLock);c->owned_status.callbacks--;IOLockUnlock(gClientLock);
    }
}
void applyClients(){
    if(!gGlinkStarted)return;const QcomGlinkClient glue={clientState,clientReceive};
    IOLockLock(gClientLock);
    for(unsigned i=0;i<gClientCount;i++){
        auto &c=gClients[i];
        if(!c.glink&&!c.owned_status.closing)c.glink=qcom_glink_channel(&gGlink,c.name,c.intent_size,c.intent_count,c.storage,&glue,&c,c.initiator);
    }
    IOLockUnlock(gClientLock);
}
void noteChannel(const ShengAdspChannel &c,int rc,uint32_t length){
    if(strcmp(c.name,"adsp_apps"))return;
    if(rc==QcomGlinkOk)native_adsp_audio[0]=native_adsp_audio[0]+1;
    else if(rc==QcomGlinkAgain){native_adsp_audio[1]=native_adsp_audio[1]+1;native_adsp_audio[7]=(int32_t)length;}
    else native_adsp_audio[2]=native_adsp_audio[2]+1;
    IOLockLock(gClientLock);native_adsp_audio[3]=(int32_t)c.count;IOLockUnlock(gClientLock);
    native_adsp_audio[4]=c.glink->intent_asked;
    int32_t valid=0,largest=0;
    for(unsigned k=0;k<QCOM_GLINK_MAX_REMOTE_INTENTS;k++)if(c.glink->remote[k].valid){valid++;if((int32_t)c.glink->remote[k].size>largest)largest=(int32_t)c.glink->remote[k].size;}
    native_adsp_audio[5]=valid;native_adsp_audio[6]=largest;
}
void pumpSends(){
    for(unsigned i=0;i<__atomic_load_n(&gClientCount,__ATOMIC_ACQUIRE);i++){
        auto &c=gClients[i];
        if(!c.glink||!c.glink->up)continue;
        for(unsigned budget=0;budget<kQueue;budget++){
            IOLockLock(gClientLock);
            if(!c.count||!c.connected||c.owned_status.closing||c.owned_status.error){IOLockUnlock(gClientLock);break;}
            Message message=c.queue[c.head];
            if(message.epoch!=c.epoch){c.head=(c.head+1)%kQueue;c.count--;IOLockUnlock(gClientLock);continue;}
            c.owned_status.pumping=1;IOLockUnlock(gClientLock);
            int rc=qcom_glink_send(&gGlink,c.glink,message.data,message.length);noteChannel(c,rc,message.length);
            IOLockLock(gClientLock);c.owned_status.pumping=0;
            if(rc==QcomGlinkOk&&c.epoch==message.epoch&&c.count){c.head=(c.head+1)%kQueue;c.count--;}
            IOLockUnlock(gClientLock);if(rc!=QcomGlinkOk)break;
        }
    }
}
void serviceOwnedChannels(){
    if(!gGlinkStarted)return;
    for(unsigned i=0;i<__atomic_load_n(&gClientCount,__ATOMIC_ACQUIRE);i++){
        auto &c=gClients[i];QcomGlinkChannel *channel=nullptr;bool act=false;
        IOLockLock(gClientLock);
        if(c.owned_status.closing&&!c.owned_status.fenced&&!c.owned_status.pumping&&!c.owned_status.callbacks){
            c.head=c.count=0;channel=c.glink;act=true;
            if(!channel){c.owned_status.close_sent=c.owned_status.close_acked=c.owned_status.peer_closed=c.owned_status.fenced=1;}
        }
        IOLockUnlock(gClientLock);
        if(!act||!channel)continue;
        int rc=elden_glink_close_request(&gGlink,channel);uint32_t sent=0,acked=0,peer=0;
        int status=rc?rc:elden_glink_close_status(&gGlink,channel,&sent,&acked,&peer);
        IOLockLock(gClientLock);
        c.owned_status.close_sent=sent;c.owned_status.close_acked=acked;c.owned_status.peer_closed=peer;
        if(status<0)c.owned_status.error=status;
        if(status==1&&!c.count&&!c.owned_status.pumping&&!c.owned_status.callbacks)c.owned_status.fenced=1;
        IOLockUnlock(gClientLock);
    }
}
