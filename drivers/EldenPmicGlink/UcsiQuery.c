/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Wire layout/order from QcomDwc3 UCSI and Linux; the stock QTI module also
 * independently proves owner32779,12B READ and64B WRITE/READ responses.
 * Exact reply/return/version bounds are checked before any state is published.
 */
#include "UcsiQuery.h"
#include <string.h>
#define CCI_BUSY (UINT32_C(1)<<28)
#define CCI_RESET (UINT32_C(1)<<27)
#define CCI_ACK (UINT32_C(1)<<29)
#define CCI_ERROR (UINT32_C(1)<<30)
#define CCI_COMPLETE (UINT32_C(1)<<31)
#define CCI_UNSUPPORTED (UINT32_C(1)<<25)
/* Linux v1 mandatory notifications with the optional feature bits absent:
 * command/error, power opmode/direction, battery, partner and connector.
 * The exact stock PPM requires mask&5804==5804 to finish OPM initialization. */
#define EVENTS_CONTROL (UINT64_C(0xda050000)|5u)
uint64_t elden_ucsi_event_control(const EldenUcsiQuery *q){
    /* Linux ties these two notifications to PDO_DETAILS. Elden's verified
     * v1.1/two-port PPM implements GET_PDOS despite features=0, so apply the
     * same existing read quirk to notifications. Keep reported features raw. */
    bool known=q&&q->query_pdos&&((q->working.features&(1u<<4))||
        (q->working.version==0x110&&q->working.connectors==2&&q->working.features==0));
    return EVENTS_CONTROL|(known?UINT64_C(0x00600000):0);
}
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t le16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
uint64_t elden_ucsi_query_control(unsigned i){
    static const uint64_t controls[]={0x06,0x07|(UINT64_C(1)<<16),0x12|(UINT64_C(1)<<16),
        0x07|(UINT64_C(2)<<16),0x12|(UINT64_C(2)<<16)};
    return i<5?controls[i]:0;
}
uint64_t elden_ucsi_pdo_control(unsigned connector,unsigned offset,unsigned count){
    if(connector<1||connector>2||!count||count>4||offset+count>7)return 0;
    return UINT64_C(0x10)|((uint64_t)connector<<16)|(UINT64_C(1)<<23)|
        ((uint64_t)offset<<24)|((uint64_t)(count-1)<<32)|(UINT64_C(1)<<34);
}
static bool pdo_query(unsigned q){return q>=5&&q<=8;}
static uint64_t control(const EldenUcsiQuery *q){
    if(q->query<5)return elden_ucsi_query_control(q->query);
    if(!q->query_pdos)return 0;
    if(pdo_query(q->query)){unsigned i=q->query-5;return elden_ucsi_pdo_control(i/2+1,(i&1)?4:0,(i&1)?3:4);}
    if(q->query<=10)return UINT64_C(0x12)|((uint64_t)(q->query-8)<<16);
    return 0;
}
static void stop(EldenUcsiQuery *q,uint32_t error){
    q->failed_phase=q->phase;
    if(!q->failure)q->failure=EldenUcsiFailureResponse;
    q->phase=EldenUcsiFailed;q->pending=q->polling=q->refresh_wanted=false;
    q->firmware_error=error;++q->errors;
    q->working.connectors=q->committed.connectors=0;
    for(unsigned i=0;i<2;i++)q->working.port[i].status_valid=q->committed.port[i].status_valid=false;
}
void elden_ucsi_init(EldenUcsiQuery *q){memset(q,0,sizeof(*q));}
void elden_ucsi_initialize_ppm(EldenUcsiQuery *q,bool enable){if(q&&!q->up)q->initialize_ppm=enable;}
void elden_ucsi_enable_events(EldenUcsiQuery *q,bool enable){if(q&&!q->up)q->connector_events=enable;}
void elden_ucsi_enable_pdos(EldenUcsiQuery *q,bool enable){if(q&&!q->up)q->query_pdos=enable;}
static bool writing(enum EldenUcsiPhase phase){
    return phase==EldenUcsiQueryWrite||phase==EldenUcsiAckWrite||phase==EldenUcsiClearWrite||
        phase==EldenUcsiResetWrite||phase==EldenUcsiNotifyWrite||phase==EldenUcsiEventsWrite;
}
void elden_ucsi_link(EldenUcsiQuery *q,bool up){
    if(q->up==up)return; /* duplicate up cannot clear a failed transaction */
    q->up=up;q->pending=q->polling=q->refresh_wanted=q->bootstrap_ack=q->command_error=false;
    q->notify_ack=false;q->failed_phase=EldenUcsiOff;q->failure=EldenUcsiFailureNone;
    q->events_ready=q->events_ack=false;q->active_connector=q->ack_connector=0;
    q->notification_control=0;
    q->pdo_disabled=0;
    q->last_opcode=q->last_reply_bytes=0;q->last_control=0;
    q->working=(EldenUcsiSnapshot){0};q->committed=(EldenUcsiSnapshot){0};q->buffer_bytes=0;q->query=q->polls=0;
    if(up){++q->epoch;q->phase=EldenUcsiVersion;q->working.epoch=q->epoch;}
    else q->phase=EldenUcsiOff;
}
void elden_ucsi_refresh(EldenUcsiQuery *q){
    if(!q->up||q->phase==EldenUcsiFailed)return;
    if(q->phase!=EldenUcsiReady){q->refresh_wanted=true;return;}
    uint16_t version=q->working.version;q->working=(EldenUcsiSnapshot){0};
    q->working.version=version;q->working.epoch=q->epoch;q->query=0;q->polls=0;q->refresh_wanted=false;q->phase=EldenUcsiQueryWrite;
}
int elden_ucsi_next(EldenUcsiQuery *q,uint8_t *out,size_t capacity){
    if(!q->up||q->pending||q->polling||q->phase==EldenUcsiOff||q->phase==EldenUcsiReady||q->phase==EldenUcsiFailed)return 0;
    const bool write=writing(q->phase);
    size_t bytes=write?12+q->buffer_bytes+4:12;
    if(!out||capacity<bytes)return -2;
    if(write&&q->buffer_bytes!=48&&q->buffer_bytes!=528)return -2;
    uint8_t packet[ELDEN_UCSI_MAX_MESSAGE]={0};put32(packet,ELDEN_UCSI_OWNER);put32(packet+4,1);put32(packet+8,write?0x12:0x11);
    if(write){
        uint64_t command=q->phase==EldenUcsiAckWrite?UINT64_C(0x04)|(UINT64_C(1)<<17):control(q);
        /* A different connector may be announced after the status reply but
         * before this ACK is queued. Complete only the command in that case. */
        if(q->phase==EldenUcsiAckWrite&&q->ack_connector&&q->active_connector&&
           q->active_connector!=q->ack_connector)q->ack_connector=0;
        if(q->phase==EldenUcsiAckWrite&&q->ack_connector){
            command|=UINT64_C(1)<<16;
            /* Like Linux EVENT_PENDING: clear before sending ACK, so a new
             * notification while waiting for completion remains pending. */
            if(q->active_connector==q->ack_connector)q->active_connector=0;
        }
        if(q->phase==EldenUcsiResetWrite)command=1;
        if(q->phase==EldenUcsiClearWrite)command=5;
        if(q->phase==EldenUcsiNotifyWrite)command=UINT64_C(5)|(UINT64_C(1)<<16)|(UINT64_C(1)<<31);
        if(q->phase==EldenUcsiEventsWrite){command=elden_ucsi_event_control(q);q->notification_control=(uint32_t)command;}
        if(!command)return -2;
        put32(packet+20,(uint32_t)command);put32(packet+24,(uint32_t)(command>>32));
        q->last_control=command;
    }
    q->last_opcode=write?0x12:0x11;
    memcpy(out,packet,bytes);q->pending=true;return (int)bytes;
}
void elden_ucsi_unsent(EldenUcsiQuery *q){
    /* Only the immediate queue failure path uses this, before another core
     * receive call. Never erase a newer notification if the caller has one. */
    if(q->phase==EldenUcsiAckWrite&&q->ack_connector&&!q->active_connector)q->active_connector=q->ack_connector;
    q->pending=false;
}
void elden_ucsi_timeout(EldenUcsiQuery *q){if(q->pending){++q->timeouts;q->failure=EldenUcsiFailureTimeout;stop(q,UINT32_MAX);}}
void elden_ucsi_tick(EldenUcsiQuery *q){
    if(!q->polling)return;
    if(++q->polls>250){++q->timeouts;q->failure=EldenUcsiFailurePolling;stop(q,UINT32_MAX);return;}
    q->polling=false;
}
static bool supported_version(uint16_t version){
    return version==0x100||version==0x110||version==0x120||version==0x200||version==0x210||version==0x300;
}
static unsigned status_connector(unsigned query){
    if(query==2||query==9)return 1;
    if(query==4||query==10)return 2;
    return 0;
}
static int capture(EldenUcsiQuery *q,const uint8_t *buf,unsigned length){
    const uint8_t *data=buf+16;
    if(q->query==0){
        if(length<16)return -1;
        q->working.attributes=le32(data);q->working.features=le16(data+5);q->working.connectors=data[4];
        return data[4]==2?0:-1; /* this board's two ports, not a single-connector facade */
    }
    if(pdo_query(q->query)){
        unsigned port=(q->query-5)/2,offset=(q->query&1)?0:4,wanted=offset?3:4;
        EldenUcsiPort *p=&q->working.port[port];
        if((length&3)||length>wanted*4||p->pdo_count!=offset)return -1;
        bool ended=false;
        for(unsigned i=0;i<length/4;i++){
            uint32_t value=le32(data+4*i);
            if(!value){ended=true;continue;}
            if(ended)return -1;
            p->source_pdos[p->pdo_count++]=value;
        }
        p->pdos_complete=offset||ended||length<16;return 0;
    }
    if(q->query>=9){
        if(q->query>10||length<9)return -1;
        EldenUcsiPort *p=&q->working.port[q->query-9];
        uint32_t flags=le32(data),rdo=le32(data+4);
        const uint32_t critical=(7u<<16)|(1u<<19)|(1u<<20)|(1u<<21)|(7u<<29);
        p->pdos_consistent=p->pdos_complete&&p->pdo_count&&!p->pdos_error&&!q->refresh_wanted&&
            ((flags^p->flags)&critical)==0&&rdo==p->rdo;
        return 0;
    }
    unsigned port=(q->query-1)/2;EldenUcsiPort *p=&q->working.port[port];
    if(q->query==1||q->query==3){
        if(length<2)return -1;
        p->capability=le16(data);if(length>=4)p->capability=le32(data);
        p->capability_bytes=(uint8_t)length;p->capability_valid=true;return 0;
    }
    if(length<9)return -1;
    p->flags=le32(data);p->rdo=le32(data+4);p->bc_status=data[8]&3;p->status_bytes=(uint8_t)length;
    p->connected=(p->flags>>19)&1;p->source=(p->flags>>20)&1;p->usb=(p->flags>>21)&1;
    p->power_mode=(p->flags>>16)&7;p->partner=(p->flags>>29)&7;p->status_valid=true;
    if(q->working.version>=0x200&&length>=11){
        p->orientation_known=p->sink_path_known=true;p->reversed=(data[10]>>6)&1;p->sink_path_enabled=(data[10]>>7)&1;
    }
    return 0;
}
int elden_ucsi_receive(EldenUcsiQuery *q,const uint8_t *data,size_t bytes){
    if(!q->up||q->phase==EldenUcsiFailed)return EldenUcsiStopped;
    if(!data||bytes<12||le32(data)!=ELDEN_UCSI_OWNER){++q->ignored;return 0;}
    uint32_t type=le32(data+4),op=le32(data+8);
    if(op==0x13){
        if(type!=2||bytes!=24||le32(data+16)!=0||le32(data+20)){++q->ignored;return 0;}
        ++q->notifications;
        /* Command/ACK completion is not a connector change. Treating every
         * notification as a refresh feeds our own GET completions back into
         * another cycle indefinitely after SET_NOTIFICATION_ENABLE. */
        uint32_t cci=le32(data+12);unsigned connector=(cci>>1)&127;
        if(!(cci&CCI_BUSY)&&connector>=1&&connector<=2){
            q->refresh_wanted=true;q->pdo_disabled&=(uint8_t)~(1u<<(connector-1));
            if(q->connector_events)q->active_connector=(uint8_t)connector;
        }
        return EldenUcsiNotification;
    }
    bool write=writing(q->phase);
    if(!q->pending||type!=1||op!=(write?0x12u:0x11u)){++q->ignored;return 0;}
    q->last_reply_bytes=(uint32_t)bytes;
    if(write){
        if(bytes!=16||le32(data+12)){stop(q,bytes==16?le32(data+12):UINT32_MAX-1);return EldenUcsiStopped;}
        q->pending=false;q->polls=0;
        switch(q->phase){
        case EldenUcsiClearWrite:q->phase=EldenUcsiClearRead;break;
        case EldenUcsiResetWrite:q->phase=EldenUcsiResetRead;break;
        case EldenUcsiNotifyWrite:q->phase=EldenUcsiNotifyRead;break;
        case EldenUcsiEventsWrite:q->phase=EldenUcsiEventsRead;break;
        case EldenUcsiQueryWrite:q->phase=EldenUcsiQueryRead;break;
        default:q->phase=EldenUcsiAckRead;break;
        }
        return EldenUcsiProgress;
    }
    if(bytes!=64&&bytes!=544){stop(q,UINT32_MAX-1);return EldenUcsiStopped;}
    const unsigned count=(unsigned)bytes-16;const uint8_t *buf=data+12;uint16_t version=le16(buf);
    if(le32(data+12+count)||!supported_version(version)||count!=(version<0x200?48u:528u)||
        (q->buffer_bytes&&q->buffer_bytes!=count)||(q->working.version&&q->working.version!=version)){
        stop(q,le32(data+12+count)?le32(data+12+count):UINT32_MAX-1);return EldenUcsiStopped;
    }
    q->buffer_bytes=count;q->working.version=version;q->cci=le32(buf+4);q->pending=false;
    if(q->cci&CCI_BUSY){q->polling=true;return EldenUcsiProgress;}
    unsigned connector=(q->cci>>1)&127;
    if(q->connector_events&&connector>=1&&connector<=2){
        q->active_connector=(uint8_t)connector;q->refresh_wanted=true;
    }
    if(q->phase==EldenUcsiVersion){
        q->query=0;
        if(q->initialize_ppm){
            if(version>=0x200){q->failure=EldenUcsiFailureProtocol;stop(q,UINT32_MAX-2);return EldenUcsiStopped;}
            q->phase=(q->cci&CCI_RESET)?EldenUcsiClearWrite:EldenUcsiResetWrite;
            return EldenUcsiProgress;
        }
        if(q->cci&(CCI_COMPLETE|CCI_ERROR|CCI_UNSUPPORTED)){q->bootstrap_ack=true;q->phase=EldenUcsiAckWrite;}
        else q->phase=EldenUcsiQueryWrite;
        return EldenUcsiProgress;
    }
    if(q->phase==EldenUcsiClearRead){
        if(q->cci&(CCI_ERROR|CCI_UNSUPPORTED)){q->failure=EldenUcsiFailureProtocol;stop(q,q->cci);return EldenUcsiStopped;}
        if(!(q->cci&CCI_COMPLETE)){q->polling=true;return EldenUcsiProgress;}
        if(q->cci&CCI_RESET){q->failure=EldenUcsiFailureProtocol;stop(q,q->cci);return EldenUcsiStopped;}
        q->phase=EldenUcsiResetWrite;return EldenUcsiProgress;
    }
    if(q->phase==EldenUcsiResetRead){
        if(q->cci&(CCI_ERROR|CCI_UNSUPPORTED)){q->failure=EldenUcsiFailureProtocol;stop(q,q->cci);return EldenUcsiStopped;}
        if(!(q->cci&CCI_RESET)){q->polling=true;return EldenUcsiProgress;}
        q->phase=EldenUcsiNotifyWrite;return EldenUcsiProgress;
    }
    if(q->phase==EldenUcsiNotifyRead||q->phase==EldenUcsiEventsRead){
        if(!(q->cci&(CCI_COMPLETE|CCI_ERROR|CCI_UNSUPPORTED))){q->polling=true;return EldenUcsiProgress;}
        q->command_status=q->cci;q->command_error=(q->cci&(CCI_ERROR|CCI_UNSUPPORTED))!=0;
        q->events_ack=q->phase==EldenUcsiEventsRead;q->notify_ack=!q->events_ack;
        q->ack_connector=0;q->phase=EldenUcsiAckWrite;return EldenUcsiProgress;
    }
    if(q->phase==EldenUcsiQueryRead){
        if(!(q->cci&(CCI_COMPLETE|CCI_ERROR|CCI_UNSUPPORTED))){q->polling=true;return EldenUcsiProgress;}
        unsigned length=(q->cci>>8)&255;
        q->command_status=q->cci;q->command_error=(q->cci&(CCI_ERROR|CCI_UNSUPPORTED))!=0;
        if(!q->command_error&&(length>(version<0x200?16u:256u)||capture(q,buf,length)))q->command_error=true;
        unsigned target=status_connector(q->query);
        q->ack_connector=(!q->command_error&&q->events_ready&&target&&target==q->active_connector)?(uint8_t)target:0;
        q->working.cci=q->cci;q->phase=EldenUcsiAckWrite;return EldenUcsiProgress;
    }
    if(q->phase==EldenUcsiAckRead){
        if(q->connector_events&&(q->cci&(CCI_ERROR|CCI_UNSUPPORTED))){q->failure=EldenUcsiFailureProtocol;stop(q,q->cci);return EldenUcsiStopped;}
        if(!(q->cci&CCI_ACK)){q->polling=true;return EldenUcsiProgress;}
        if(q->ack_connector){++q->connector_acks;q->ack_connector=0;}
        if(q->command_error){
            if(!q->notify_ack&&!q->events_ack&&!q->bootstrap_ack&&pdo_query(q->query)&&(q->command_status&(CCI_ERROR|CCI_UNSUPPORTED))){
                unsigned port=(q->query-5)/2;q->working.port[port].pdos_error=true;
                if(q->command_status&CCI_UNSUPPORTED)q->pdo_disabled|=(uint8_t)(1u<<port);
                q->command_error=false;
            }else{stop(q,q->command_status|1);return EldenUcsiStopped;}
        }
        if(q->events_ack){
            q->events_ack=false;q->events_ready=true;
            /* Re-read both ports after the PPM's OPM-ready transition. Do not
             * publish the pre-notification snapshot as initialized data. */
            uint16_t version=q->working.version;q->working=(EldenUcsiSnapshot){0};
            q->working.version=version;q->working.epoch=q->epoch;q->query=0;q->polls=0;q->refresh_wanted=false;
            q->phase=EldenUcsiQueryWrite;return EldenUcsiProgress;
        }
        if(q->notify_ack){q->notify_ack=false;q->phase=EldenUcsiQueryWrite;return EldenUcsiProgress;}
        if(q->bootstrap_ack){q->bootstrap_ack=false;q->phase=EldenUcsiQueryWrite;return EldenUcsiProgress;}
        ++q->query;
        if(q->query==5&&q->initialize_ppm&&q->connector_events&&!q->events_ready){
            q->phase=EldenUcsiEventsWrite;return EldenUcsiProgress;
        }
        while(q->query_pdos&&pdo_query(q->query)){
            unsigned port=(q->query-5)/2;EldenUcsiPort *p=&q->working.port[port];
            if(q->pdo_disabled&(1u<<port))p->pdos_error=true;
            /* The actual Elden v1.1 PPM reports features=0 but its GET_PDOS
             * works (stock command/readback qualified). This optional probe
             * handles an explicit UNSUPPORTED reply after ACK instead. */
            bool skip=!p->connected||p->source||p->power_mode!=3||!p->rdo||p->pdos_error;
            if((q->query&1)==0&&p->pdos_complete)skip=true;
            if(!skip)break;
            ++q->query;
        }
        if(q->query<(q->query_pdos?11u:5u)){q->phase=EldenUcsiQueryWrite;return EldenUcsiProgress;}
        if(q->refresh_wanted){for(unsigned i=0;i<2;i++)q->working.port[i].pdos_consistent=false;}
        q->working.cycle=++q->cycles;q->working.epoch=q->epoch;q->committed=q->working;q->phase=EldenUcsiReady;
        return EldenUcsiCycle;
    }
    ++q->ignored;return 0;
}
