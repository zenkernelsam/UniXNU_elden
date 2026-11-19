/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Same original ADSP registry lock and queues; no GLINK call from clients. */
static bool adsp_channel_member(ShengAdspChannel *c){
    for(unsigned i=0;i<gClientCount;i++)if(c==&gClients[i])return true;return false;
}
static ShengAdspChannel *adsp_register(const char *name,uint32_t size,uint32_t count,int initiator,
                                      const ShengAdspClient *legacy,const EldenAdspEpochClient *client,void *context){
    if(!gClientLock||!name||(!legacy&&!client)||!size||size>65536||!count||count>QCOM_GLINK_MAX_LOCAL_INTENTS)return nullptr;
    size_t length=0;while(length<QCOM_GLINK_NAME&&name[length])length++;if(!length||length==QCOM_GLINK_NAME)return nullptr;
    uint8_t *storage=(uint8_t *)IOMallocData((vm_size_t)size*count);Message *queue=(Message *)IOMallocData(sizeof(Message)*kQueue);
    ShengAdspChannel *c=nullptr;IOLockLock(gClientLock);bool duplicate=false;
    for(unsigned i=0;i<gClientCount;i++)if(!strcmp(gClients[i].name,name))duplicate=true;
    if(!duplicate&&gClientCount<kClients&&storage&&queue){
        c=&gClients[gClientCount];bzero(c,sizeof(*c));strlcpy(c->name,name,sizeof(c->name));c->intent_size=size;c->intent_count=count;c->initiator=initiator!=0;
        if(legacy)c->client=*legacy;if(client)c->epoch_client=*client;c->epoch_callbacks=client!=nullptr;
        c->context=context;c->storage=storage;c->queue=queue;__atomic_store_n(&gClientCount,gClientCount+1,__ATOMIC_RELEASE);
    }
    IOLockUnlock(gClientLock);
    if(!c){if(storage)IOFreeData(storage,(vm_size_t)size*count);if(queue)IOFreeData(queue,sizeof(Message)*kQueue);return nullptr;}
    wake(kEvSend);return c;
}
extern "C" ShengAdspChannel *sheng_adsp_channel(const char *name,uint32_t size,uint32_t count,int initiator,const ShengAdspClient *client,void *context){
    return client?adsp_register(name,size,count,initiator,client,nullptr,context):nullptr;
}
extern "C" ShengAdspChannel *elden_adsp_channel_epoch(const char *name,uint32_t size,uint32_t count,int initiator,const EldenAdspEpochClient *client,void *context){
    return client?adsp_register(name,size,count,initiator,nullptr,client,context):nullptr;
}
static int adspSend(ShengAdspChannel *c,uint32_t epoch,bool checkEpoch,const void *data,uint32_t length){
    if(!gClientLock||!c||!data||!length)return -2;if(length>SHENG_ADSP_MAX_MESSAGE)return -1;
    IOLockLock(gClientLock);
    if(!adsp_channel_member(c)){IOLockUnlock(gClientLock);return -2;}
    if(c->owned_status.closing||c->owned_status.error||!c->connected||(checkEpoch&&c->epoch!=epoch)){IOLockUnlock(gClientLock);return -3;}
    if(c->count==kQueue){IOLockUnlock(gClientLock);return -1;}
    Message &message=c->queue[(c->head+c->count)%kQueue];memcpy(message.data,data,length);message.length=length;message.epoch=c->epoch;c->count++;
    IOLockUnlock(gClientLock);wake(kEvSend);return 0;
}
extern "C" int sheng_adsp_send(ShengAdspChannel *c,const void *data,uint32_t length){return adspSend(c,0,false,data,length);}
extern "C" int sheng_adsp_send_epoch(ShengAdspChannel *c,uint32_t epoch,const void *data,uint32_t length){return adspSend(c,epoch,true,data,length);}
extern "C" int elden_adsp_channel_status(ShengAdspChannel *c,EldenAdspChannelStatus *out){
    if(!gClientLock||!c||!out)return -2;IOLockLock(gClientLock);
    if(!adsp_channel_member(c)){IOLockUnlock(gClientLock);return -2;}
    EldenAdspChannelStatus s=c->owned_status;s.epoch=c->epoch;s.connected=c->connected;s.queued=c->count;*out=s;IOLockUnlock(gClientLock);return 0;
}
extern "C" int elden_adsp_channel_close(ShengAdspChannel *c,uint32_t epoch){
    if(!gClientLock||!c)return -2;IOLockLock(gClientLock);
    if(!adsp_channel_member(c)){IOLockUnlock(gClientLock);return -2;}
    if(c->owned_status.closing){int rc=c->close_epoch==epoch?0:-3;IOLockUnlock(gClientLock);return rc;}
    if(c->epoch!=epoch){IOLockUnlock(gClientLock);return -3;}
    c->close_epoch=epoch;c->owned_status.closing=1;
    // applyClients also holds this lock and now refuses this registration.
    // A never-created endpoint cannot have transmitted or borrowed buffers.
    if(!c->glink&&!c->connected&&!c->count&&!c->owned_status.pumping&&!c->owned_status.callbacks)
        c->owned_status.close_sent=c->owned_status.close_acked=c->owned_status.peer_closed=c->owned_status.fenced=1;
    IOLockUnlock(gClientLock);wake(kEvSend);return 0;
}
