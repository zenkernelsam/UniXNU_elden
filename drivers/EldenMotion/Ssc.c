/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Single-worker client of the original shared QRTR provider, not a router.
 * Protocol ordering follows the qualified Pagani/libssc state machine;
 * missing attributes/clock/mount never turn into defaults or fake samples. */
#include "Ssc.h"
enum {RequestMs=2000,AttributeMs=5000,StopMs=5000};
static uint64_t ticks(uint32_t ms){return (uint64_t)ms*19200;}
static void zero(void *p,size_t n){uint8_t *b=p;for(size_t i=0;i<n;i++)b[i]=0;}
static int owner(const EldenSsc *s,uint64_t id,uint64_t epoch){return s&&s->initialized&&id==s->ops.owner&&epoch==s->ops.epoch;}
static int same(const EldenSscServer *a,const EldenSscServer *b){return a->service==b->service&&a->instance==b->instance&&a->node==b->node&&a->port==b->port&&a->generation==b->generation;}
static int suid(EldenMotionSuid a,EldenMotionSuid b){return a.low==b.low&&a.high==b.high;}
static int held(EldenSsc *s,int rc){s->state=EldenSscHeld;s->error=rc<0&&rc!=EldenSscAgain?rc:EldenMotionPeer;return s->error;}
static int clock_step(EldenSsc *s,uint64_t now){
    if(!now||(s->has_clock&&now<s->last_ticks))return held(s,EldenMotionClock);
    s->has_clock=1;s->last_ticks=now;return 0;
}
static int gate(EldenSsc *s,int cleanup){int rc=s->ops.admit(s->ops.context,s->ops.owner,s->ops.epoch,cleanup);return rc<=0?rc:EldenMotionPeer;}
static int count(uint32_t *p){if(*p==UINT32_MAX)return EldenSscOverflow;(*p)++;return 0;}
int elden_ssc_init(EldenSsc *s,const EldenSscOps *ops){
    if(!s||!ops||!ops->owner||!ops->epoch||!ops->admit||!ops->send||!ops->withdraw||!ops->qualify||!ops->map_time||!ops->deliver)return EldenMotionArgument;
    int rc=ops->admit(ops->context,ops->owner,ops->epoch,0);if(rc)return rc;
    zero(s,sizeof(*s));s->ops=*ops;s->initialized=1;s->state=EldenSscOff;return 0;
}
int elden_ssc_server(EldenSsc *s,uint64_t id,uint64_t epoch,const EldenSscServer *server,int up,uint64_t now){
    if(!owner(s,id,epoch)||!server||(up!=0&&up!=1))return EldenMotionStale;
    if(server->service!=EldenSscService||!server->generation||!server->node||server->node==UINT32_MAX||!server->port||server->port>=UINT32_C(0xfffffffe))return EldenMotionPeer;
    if(s->state==EldenSscStopped||s->closing)return EldenSscClosed;
    if(s->server_up&&!same(&s->server,server))return held(s,EldenMotionStale);
    if(!up){if(!s->server_up)return EldenSscAgain;s->server_up=0;return held(s,EldenMotionStale);}
    if(s->state==EldenSscHeld)return s->error;
    int rc=clock_step(s,now);if(rc)return rc;rc=gate(s,0);if(rc)return held(s,rc);
    if(s->server_up)return 0;s->server=*server;s->server_up=1;s->state=EldenSscDiscovering;return elden_ssc_step(s,now);
}
static int request(EldenSsc *s,unsigned conn,unsigned kind,uint32_t message,uint32_t rate,uint64_t now){
    EldenSscConnection *c=&s->conn[conn];if(c->pending)return EldenSscBusy;
    if(c->withdrawn||c->next_transaction==UINT16_MAX)return held(s,EldenSscOverflow);
    uint16_t txn=(uint16_t)(c->next_transaction+1);uint8_t packet[208];size_t n;
    EldenMotionSuid id=s->sensor[kind-1].suid;
    if(message==512)n=elden_ssc_discovery_request(packet,sizeof(packet),txn,kind);
    else if(message==1)n=elden_ssc_attribute_request(packet,sizeof(packet),txn,id);
    else n=elden_ssc_sampling_request(packet,sizeof(packet),txn,id,rate,message==10);
    if(!n||s->requests==UINT32_MAX)return held(s,EldenSscOverflow);
    int rc=gate(s,s->closing);if(rc)return held(s,rc);
    rc=s->ops.send(s->ops.context,s->ops.owner,s->ops.epoch,&s->server,conn,packet,(uint32_t)n);
    if(rc){
        if(rc!=EldenSscAgain)return held(s,rc);
        if(now>UINT64_MAX-ticks(100))return held(s,EldenSscOverflow);
        s->retry_ticks=now+ticks(100);return rc;
    }
    c->next_transaction=txn;c->pending_transaction=txn;c->pending_message=message;c->pending_kind=kind;c->pending_rate=rate;
    c->sent_ticks=now;c->pending=1;c->used=1;s->requests++;
    if(message==1)s->sensor[kind-1].attribute_ticks=now;
    return 0;
}
static void readiness(EldenSsc *s){
    if(s->closing||s->state==EldenSscHeld)return;
    if(s->sensor[0].qualified&&s->sensor[1].qualified)s->state=EldenSscReady;
    else if(s->sensor[0].discovered||s->sensor[1].discovered)s->state=EldenSscDescribing;
}
static int withdraw(EldenSsc *s,uint64_t now,int force){
    int rc=gate(s,1);if(rc)return held(s,rc);
    for(unsigned i=0;i<EldenSscConnections;i++){
        EldenSscConnection *c=&s->conn[i];if(!c->used||c->withdrawn)continue;
        if(!force&&i&&(!s->sensor[i-1].disable_confirmed||c->pending))return EldenSscAgain;
        rc=s->ops.withdraw(s->ops.context,s->ops.owner,s->ops.epoch,&s->server,i);
        if(rc)return rc==EldenSscAgain?rc:held(s,rc);
        c->withdrawn=1;c->pending=0;c->early_length=0;
    }
    (void)now;s->state=EldenSscStopped;s->server_up=0;return 0;
}
int elden_ssc_step(EldenSsc *s,uint64_t now){
    if(!s||!s->initialized)return EldenMotionArgument;
    if(s->state==EldenSscStopped)return EldenSscClosed;
    int rc=clock_step(s,now);if(rc)return rc;
    if(s->closing&&(s->state==EldenSscHeld||now>=s->stop_deadline))return withdraw(s,now,1);
    if(s->state==EldenSscHeld)return s->error;if(!s->server_up)return EldenSscAgain;
    rc=gate(s,s->closing);if(rc)return held(s,rc);
    for(unsigned i=0;i<EldenSscConnections;i++)if(s->conn[i].pending&&now-s->conn[i].sent_ticks>=ticks(RequestMs)){
        if(s->closing)return withdraw(s,now,1);return held(s,EldenMotionClock);
    }
    if(s->retry_ticks&&now<s->retry_ticks)return EldenSscAgain;s->retry_ticks=0;
    if(s->closing){
        for(unsigned i=0;i<2;i++){
            EldenSscConnection *c=&s->conn[i+1];EldenSscSensor *sensor=&s->sensor[i];
            if(c->used&&!c->pending&&!sensor->disable_confirmed){rc=request(s,i+1,i+1,10,0,now);if(rc&&rc!=EldenSscAgain)return rc;}
        }
        return withdraw(s,now,0);
    }
    if(!s->conn[0].pending){for(unsigned i=0;i<2;i++)if(!s->sensor[i].lookup_accepted){
        rc=request(s,0,i+1,512,0,now);if(rc&&rc!=EldenSscAgain)return rc;break;
    }}
    for(unsigned i=0;i<2;i++){
        EldenSscSensor *sensor=&s->sensor[i];EldenSscConnection *c=&s->conn[i+1];
        if(!sensor->discovered||c->pending)continue;
        if(!sensor->attribute_accepted&&!sensor->attribute_seen){rc=request(s,i+1,i+1,1,0,now);if(rc&&rc!=EldenSscAgain)return rc;continue;}
        if(!sensor->attribute_seen&&now-sensor->attribute_ticks>=ticks(AttributeMs))return held(s,EldenMotionClock);
        if(!sensor->qualified)continue;
        uint32_t desired=sensor->demand_hz?elden_ssc_pick_rate(&sensor->attrs,sensor->demand_hz):0;
        if(sensor->demand_hz&&!desired)return held(s,EldenMotionPeer);
        if(sensor->active_rate!=desired){rc=request(s,i+1,i+1,desired?513:10,desired,now);if(rc&&rc!=EldenSscAgain)return rc;}
    }
    readiness(s);return 0;
}
int elden_ssc_demand(EldenSsc *s,uint64_t id,uint64_t epoch,unsigned kind,uint32_t hz){
    if(!owner(s,id,epoch)||kind<1||kind>2)return EldenMotionStale;
    if(hz>400)return EldenMotionArgument;
    if(s->closing)return hz?EldenSscClosed:0;
    if(s->state==EldenSscHeld||s->state==EldenSscStopped)return EldenSscClosed;
    if(s->state!=EldenSscReady||!s->sensor[kind-1].qualified)return EldenSscAgain;
    int rc=gate(s,0);if(rc)return rc;
    if(hz&&!elden_ssc_pick_rate(&s->sensor[kind-1].attrs,hz))return EldenMotionPeer;
    s->sensor[kind-1].demand_hz=hz;return 0;
}
int elden_ssc_admit(EldenSsc *s,uint64_t id,uint64_t epoch,unsigned kind,int cleanup){
    if(!owner(s,id,epoch)||kind<1||kind>2)return EldenMotionStale;
    if(!cleanup&&(s->closing||s->state!=EldenSscReady||!s->server_up||!s->sensor[kind-1].qualified))return EldenSscAgain;
    return gate(s,cleanup);
}
static int response(EldenSsc *s,unsigned conn,const uint8_t *p,size_t n,uint16_t txn,uint64_t now){
    EldenSscConnection *c=&s->conn[conn];if(!c->pending||txn!=c->pending_transaction)return EldenMotionStale;
    uint64_t client;int rc=elden_motion_client_response(p,n,txn,&client);if(rc)return held(s,rc);
    if(c->client&&c->client!=client)return held(s,EldenMotionPeer);
    if(count(&s->responses))return held(s,EldenSscOverflow);
    c->client=client;c->pending=0;EldenSscSensor *sensor=&s->sensor[c->pending_kind-1];
    if(c->pending_message==512)sensor->lookup_accepted=1;
    else if(c->pending_message==1)sensor->attribute_accepted=1;
    else if(c->pending_message==513){sensor->active_rate=c->pending_rate;sensor->disable_confirmed=0;}
    else if(c->pending_message==10){sensor->active_rate=0;sensor->disable_confirmed=1;}
    if(c->early_length){
        uint32_t length=c->early_length;c->early_length=0;
        rc=elden_ssc_receive(s,s->ops.owner,s->ops.epoch,&s->server,conn,c->early,length,now);if(rc<0&&rc!=EldenSscAgain)return rc;
    }
    return 0;
}
static int lookup(EldenSsc *s,const uint8_t *p,size_t n,uint64_t client){
    EldenMotionSuid ids[2];uint8_t found[2]={0};
    for(unsigned i=0;i<2;i++){
        EldenMotionSuid id;int rc=elden_motion_discovery_report(p,n,client,i+1,&id);if(rc<0)return held(s,rc);
        if(rc==1){
            EldenSscSensor *sensor=&s->sensor[i];
            if(sensor->discovered&&!suid(sensor->suid,id))return held(s,EldenMotionStale);
            if(s->sensor[1-i].discovered&&suid(s->sensor[1-i].suid,id))return held(s,EldenMotionPeer);
            ids[i]=id;found[i]=1;
        }
    }
    if(found[0]&&found[1]&&suid(ids[0],ids[1]))return held(s,EldenMotionPeer);
    for(unsigned i=0;i<2;i++)if(found[i]){s->sensor[i].suid=ids[i];s->sensor[i].discovered=1;}
    readiness(s);return 0;
}
static int report(EldenSsc *s,unsigned conn,const uint8_t *p,size_t n,uint64_t now){
    EldenSscConnection *c=&s->conn[conn];
    if(!c->client){
        if(!c->pending||c->early_length||n>sizeof(c->early))return held(s,EldenSscOverflow);
        for(size_t i=0;i<n;i++)c->early[i]=p[i];c->early_length=(uint32_t)n;c->early_ticks=now;return EldenSscAgain;
    }
    if(count(&s->reports))return held(s,EldenSscOverflow);
    if(conn==0)return s->closing?0:lookup(s,p,n,c->client);
    EldenSscSensor *sensor=&s->sensor[conn-1];if(!sensor->discovered)return held(s,EldenMotionPeer);
    int64_t error;int rc=elden_ssc_report_error(p,n,c->client,sensor->suid,&error);if(rc<0)return held(s,rc);if(rc==1)return held(s,EldenMotionPeer);
    if(s->closing)return 0;
    EldenSscAttrs attrs;rc=elden_ssc_attribute_report(p,n,c->client,sensor->suid,conn,&attrs);if(rc<0)return held(s,rc);
    if(rc==1){
        if(sensor->attribute_seen)return held(s,EldenMotionPeer);
        EldenMotionSpec spec={0};spec.suid=sensor->suid;spec.kind=conn;
        rc=s->ops.qualify(s->ops.context,s->ops.owner,s->ops.epoch,conn,&attrs,&spec);if(rc)return held(s,rc);
        rc=elden_ssc_spec_valid(&spec,&attrs,conn,sensor->suid);if(rc)return held(s,rc);
        sensor->attrs=attrs;sensor->spec=spec;sensor->attribute_seen=1;sensor->described=1;sensor->qualified=1;readiness(s);
    }
    if(!sensor->qualified||!sensor->active_rate)return 0;
    EldenMotionSample samples[16];int n_samples=elden_motion_samples(p,n,c->client,&sensor->spec,samples,16);if(n_samples<0)return held(s,n_samples);
    uint64_t previous_ssc=sensor->last_ssc,previous_mach=sensor->last_mach;
    uint64_t original[16];
    for(int i=0;i<n_samples;i++){
        if(previous_ssc&&samples[i].timestamp_ticks<=previous_ssc)return held(s,EldenMotionClock);
        original[i]=samples[i].timestamp_ticks;uint64_t mapped=0;
        rc=s->ops.map_time(s->ops.context,s->ops.owner,s->ops.epoch,original[i],&mapped);
        if(rc||!mapped||(previous_mach&&mapped<=previous_mach))return held(s,rc?rc:EldenMotionClock);
        previous_ssc=original[i];previous_mach=mapped;samples[i].timestamp_ticks=mapped;
    }
    for(int i=0;i<n_samples;i++){
        rc=gate(s,0);if(rc)return held(s,rc);
        rc=s->ops.deliver(s->ops.context,s->ops.owner,s->ops.epoch,&samples[i]);if(rc)return rc==EldenSscAgain?rc:held(s,rc);
        sensor->last_ssc=original[i];sensor->last_mach=samples[i].timestamp_ticks;if(count(&s->samples))return held(s,EldenSscOverflow);
    }
    return n_samples;
}
int elden_ssc_receive(EldenSsc *s,uint64_t id,uint64_t epoch,const EldenSscServer *server,unsigned conn,const uint8_t *p,size_t n,uint64_t now){
    if(!owner(s,id,epoch)||!server||!same(&s->server,server)||conn>=EldenSscConnections)return EldenMotionStale;
    if(s->state==EldenSscStopped||s->conn[conn].withdrawn)return EldenSscClosed;
    if(s->state==EldenSscHeld&&!s->closing)return s->error;
    int rc=clock_step(s,now);if(rc)return rc;rc=gate(s,s->closing);if(rc)return held(s,rc);
    uint8_t type;uint16_t txn;rc=elden_ssc_packet_identity(p,n,&type,&txn);if(rc)return rc;
    rc=type==2?response(s,conn,p,n,txn,now):report(s,conn,p,n,now);
    if(rc<0&&rc!=EldenSscAgain&&count(&s->rejected))return held(s,EldenSscOverflow);return rc;
}
int elden_ssc_stop(EldenSsc *s,uint64_t id,uint64_t epoch,uint64_t now){
    if(!owner(s,id,epoch))return EldenMotionStale;if(s->state==EldenSscStopped)return 0;
    int rc=clock_step(s,now);if(rc)return rc;
    if(!s->closing){
        if(now>UINT64_MAX-ticks(StopMs))return held(s,EldenSscOverflow);
        s->closing=1;s->stop_deadline=now+ticks(StopMs);
        for(unsigned i=0;i<2;i++)s->sensor[i].demand_hz=0;
        if(s->state!=EldenSscHeld)s->state=EldenSscStopping;
    }
    if(!s->server_up)return withdraw(s,now,1);
    rc=elden_ssc_step(s,now);return s->state==EldenSscStopped?0:rc?rc:EldenSscAgain;
}
uint32_t elden_ssc_idle_ms(const EldenSsc *s,uint64_t now){
    if(!s||!s->initialized||!now)return 1;uint64_t next=now>UINT64_MAX-ticks(60000)?UINT64_MAX:now+ticks(60000);
    if(s->closing&&s->stop_deadline<next)next=s->stop_deadline;
    if(s->retry_ticks&&s->retry_ticks<next)next=s->retry_ticks;
    for(unsigned i=0;i<EldenSscConnections;i++)if(s->conn[i].pending){uint64_t sent=s->conn[i].sent_ticks;uint64_t deadline=sent>UINT64_MAX-ticks(RequestMs)?UINT64_MAX:sent+ticks(RequestMs);if(deadline<next)next=deadline;}
    for(unsigned i=0;i<2;i++)if(s->sensor[i].attribute_accepted&&!s->sensor[i].attribute_seen){uint64_t sent=s->sensor[i].attribute_ticks;uint64_t deadline=sent>UINT64_MAX-ticks(AttributeMs)?UINT64_MAX:sent+ticks(AttributeMs);if(deadline<next)next=deadline;}
    if(next<=now)return 1;uint64_t delta=next-now;uint64_t ms=delta/19200+(delta%19200!=0);return ms>60000?60000:(uint32_t)ms;
}
