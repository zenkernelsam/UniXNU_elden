/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual manager/strict codec. Independent wire fixtures describe a modeled
 * SSC server, never actual firmware, mounted axes or native candidate inputs. */
#include "Ssc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,scenarios;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"line %u: %s\n",__LINE__,#v);exit(1);}}while(0)
typedef struct {uint8_t b[2048];size_t n;} Bytes;
static void byte(Bytes *b,unsigned v){CHECK(b->n<sizeof(b->b));b->b[b->n++]=(uint8_t)v;}
static void varint(Bytes *b,uint64_t v){do{byte(b,(unsigned)((v&127)|(v>127?128:0)));v>>=7;}while(v);}
static void raw(Bytes *b,const void *p,size_t n){CHECK(n<=sizeof(b->b)-b->n);memcpy(b->b+b->n,p,n);b->n+=n;}
static void fixed(Bytes *b,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)byte(b,(unsigned)(v>>(8*i)));}
static void field(Bytes *b,unsigned f,unsigned wire,uint64_t v){varint(b,(uint64_t)f<<3|wire);if(wire==0)varint(b,v);else fixed(b,v,wire==1?8:4);}
static void nested(Bytes *b,unsigned f,const Bytes *data){varint(b,(uint64_t)f<<3|2);varint(b,data->n);raw(b,data->b,data->n);}
static void tlv(Bytes *b,unsigned tag,const Bytes *data){byte(b,tag);fixed(b,data->n,2);raw(b,data->b,data->n);}
static EldenMotionSuid ids[2]={{0x1234,0x9876},{0x5678,0x8765}};
static EldenMotionSuid lookup_id={UINT64_C(0xabababababababab),UINT64_C(0xabababababababab)};
static Bytes suid_message(EldenMotionSuid id){Bytes b={0};field(&b,1,1,id.low);field(&b,2,1,id.high);return b;}
static Bytes response_packet(uint16_t txn,uint64_t client,int result){
    Bytes body={0},v={0};fixed(&v,(unsigned)result,2);fixed(&v,0,2);tlv(&body,2,&v);
    v=(Bytes){0};fixed(&v,client,8);tlv(&body,0x10,&v);v=(Bytes){0};fixed(&v,0,4);tlv(&body,0x11,&v);
    Bytes b={0};byte(&b,2);fixed(&b,txn,2);fixed(&b,0x20,2);fixed(&b,body.n,2);raw(&b,body.b,body.n);return b;
}
static Bytes report_packet(EldenMotionSuid id,uint32_t msg,const Bytes *payload,uint64_t timestamp,uint64_t client){
    Bytes e={0},ev={0},s=suid_message(id);field(&ev,1,5,msg);field(&ev,2,1,timestamp);nested(&ev,3,payload);
    nested(&e,1,&s);nested(&e,2,&ev);Bytes body={0},v={0};fixed(&v,client,8);tlv(&body,1,&v);
    v=(Bytes){0};fixed(&v,e.n,2);raw(&v,e.b,e.n);tlv(&body,2,&v);
    Bytes b={0};byte(&b,4);fixed(&b,0,2);fixed(&b,0x21,2);fixed(&b,body.n,2);raw(&b,body.b,body.n);return b;
}
static Bytes lookup_packet(unsigned kind,uint64_t client){
    Bytes data={0},text={0},id=suid_message(ids[kind-1]);const char *type=kind==1?"accel":"gyro";
    raw(&text,type,strlen(type));nested(&data,1,&text);nested(&data,2,&id);return report_packet(lookup_id,768,&data,100,client);
}
static void add_attr(Bytes *list,unsigned id,const Bytes *value){Bytes a={0};field(&a,1,0,id);nested(&a,2,value);nested(list,1,&a);}
static Bytes datum(unsigned type,uint64_t value){Bytes d={0},v={0};field(&d,type,type==3?5:type==4?1:0,value);nested(&v,1,&d);return v;}
static Bytes string_datum(const char *text,int terminal){Bytes d={0},str={0},v={0};raw(&str,text,strlen(text)+(terminal!=0));nested(&d,2,&str);nested(&v,1,&d);return v;}
static Bytes attr_payload(unsigned kind,int mode){
    Bytes attrs={0},v=string_datum("TEST MODEL",1);add_attr(&attrs,0,&v);v=string_datum("MODELED",0);add_attr(&attrs,1,&v);
    v=string_datum(kind==1?"accel":"gyro",0);add_attr(&attrs,2,&v);v=datum(5,mode==1?0:1);add_attr(&attrs,3,&v);
    v=string_datum(kind==1?"sns_accel.proto":"sns_gyro.proto",0);if(mode!=2)add_attr(&attrs,5,&v);
    Bytes rates={0};uint32_t bits[]={0x41c80000,0x42480000,0x42c80000,0x43c80000};
    for(unsigned i=0;i<4;i++){Bytes d={0};field(&d,3,5,bits[i]);nested(&rates,1,&d);}add_attr(&attrs,6,&rates);
    Bytes pair={0};for(unsigned i=0;i<2;i++){Bytes d={0};field(&d,3,5,i?0x41800000:0xc1800000);nested(&pair,1,&d);}
    Bytes subtype={0},range={0};nested(&subtype,1,&pair);nested(&range,1,&subtype);add_attr(&attrs,11,&range);
    v=datum(4,0);add_attr(&attrs,16,&v);v=datum(5,mode==6?0:1);add_attr(&attrs,21,&v);v=datum(4,mode==5?UINT64_MAX-1:456);add_attr(&attrs,18,&v);
    if(mode==3){v=datum(5,1);add_attr(&attrs,3,&v);}
    if(mode==4){Bytes d={0};field(&d,5,0,1);field(&d,4,1,1);v=(Bytes){0};nested(&v,1,&d);add_attr(&attrs,22,&v);}
    return attrs;
}
static Bytes attributes(unsigned kind,uint64_t client,int mode){Bytes p=attr_payload(kind,mode);return report_packet(ids[kind-1],128,&p,110,client);}
static Bytes sample_packet(unsigned kind,uint64_t client,uint64_t timestamp){Bytes p={0};field(&p,1,5,0x3f800000);field(&p,1,5,0);field(&p,1,5,0);field(&p,2,0,3);return report_packet(ids[kind-1],1025,&p,timestamp,client);}
typedef struct {unsigned conn;Bytes packet;} Sent;
typedef struct {
    EldenSsc state;EldenSscServer server;uint64_t now;Sent sent[32];unsigned sent_n,delivered,withdrawn,qualifies,map_calls;
    int gate_error,send_error,withdraw_error,qualify_error,map_error,delivery_error;
    EldenMotionSample last;int bad_spec,bad_clock,allow_virtual;uint64_t mapping;
} Peer;
static int admit(void *p,uint64_t owner,uint64_t epoch,int cleanup){Peer *m=p;CHECK(owner==7&&epoch==9);CHECK(cleanup==0||cleanup==1);return m->gate_error;}
static int send_packet(void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s,unsigned conn,const uint8_t *data,uint32_t n){
    Peer *m=p;CHECK(owner==7&&epoch==9&&s->generation==m->server.generation&&conn<3&&n<208);
    if(m->send_error)return m->send_error;CHECK(m->sent_n<32);Sent *sent=&m->sent[m->sent_n++];sent->conn=conn;raw(&sent->packet,data,n);return 0;
}
static int withdraw_port(void *p,uint64_t owner,uint64_t epoch,const EldenSscServer *s,unsigned conn){Peer *m=p;CHECK(owner==7&&epoch==9&&s->generation==5&&conn<3);if(m->withdraw_error)return m->withdraw_error;m->withdrawn++;return 0;}
static int qualify(void *p,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *a,EldenMotionSpec *s){
    Peer *m=p;CHECK(owner==7&&epoch==9&&kind>=1&&kind<=2);CHECK(!strcmp(a->vendor,"MODELED")&&a->range_count==1&&a->rate_count==4);m->qualifies++;
    if(m->qualify_error)return m->qualify_error;if(!a->physical&&!m->allow_virtual)return -13;
    s->unit=kind==1?EldenMotionMetersPerSecond2:EldenMotionRadiansPerSecond;
    s->max_abs_q16=m->bad_spec?15*65536:16*65536;s->mount[0]=1;s->mount[1]=2;s->mount[2]=3;return 0;
}
static int map_time(void *p,uint64_t owner,uint64_t epoch,uint64_t ticks,uint64_t *out){Peer *m=p;CHECK(owner==7&&epoch==9&&ticks);m->map_calls++;if(m->map_error)return m->map_error;*out=m->bad_clock?0:ticks+m->mapping;return 0;}
static int deliver(void *p,uint64_t owner,uint64_t epoch,const EldenMotionSample *s){Peer *m=p;CHECK(owner==7&&epoch==9&&s->kind>=1&&s->kind<=2);if(m->delivery_error)return m->delivery_error;m->last=*s;m->delivered++;return 0;}
static void init(Peer *m){
    memset(m,0,sizeof(*m));m->server=(EldenSscServer){400,1,2,80,5};m->now=19200000;m->mapping=192;
    EldenSscOps ops={m,7,9,admit,send_packet,withdraw_port,qualify,map_time,deliver};CHECK(!elden_ssc_init(&m->state,&ops));
    CHECK(!elden_ssc_server(&m->state,7,9,&m->server,1,m->now));CHECK(m->sent_n==1&&m->sent[0].conn==0);
}
static int receive(Peer *m,unsigned conn,const Bytes *b){m->now+=192;return elden_ssc_receive(&m->state,7,9,&m->server,conn,b->b,b->n,m->now);}
static void ack(Peer *m,unsigned conn){EldenSscConnection *c=&m->state.conn[conn];CHECK(c->pending);Bytes b=response_packet(c->pending_transaction,100+conn,0);CHECK(!receive(m,conn,&b));}
static void discover(Peer *m){
    ack(m,0);Bytes b=lookup_packet(1,100);CHECK(!receive(m,0,&b));CHECK(!elden_ssc_step(&m->state,m->now));
    CHECK(m->state.conn[0].pending&&m->state.conn[1].pending);ack(m,0);b=lookup_packet(2,100);CHECK(!receive(m,0,&b));
    CHECK(!elden_ssc_step(&m->state,m->now));CHECK(m->state.conn[2].pending);ack(m,1);ack(m,2);
}
static void ready(Peer *m){discover(m);Bytes b=attributes(1,101,0);CHECK(!receive(m,1,&b));b=attributes(2,102,0);CHECK(!receive(m,2,&b));CHECK(m->state.state==EldenSscReady&&m->qualifies==2);}
static void start_stream(Peer *m){CHECK(!elden_ssc_demand(&m->state,7,9,1,26));CHECK(!elden_ssc_step(&m->state,m->now));CHECK(m->state.conn[1].pending_rate==0x42480000);ack(m,1);}
static void normal_stop(Peer *m){CHECK(elden_ssc_stop(&m->state,7,9,m->now)==EldenSscAgain);CHECK(m->state.conn[1].pending&&m->state.conn[2].pending);ack(m,1);ack(m,2);CHECK(!elden_ssc_stop(&m->state,7,9,m->now));CHECK(m->state.state==EldenSscStopped&&m->withdrawn==3);}
int main(void){
    {
        scenarios++;Peer m;init(&m);ready(&m);CHECK(!elden_ssc_admit(&m.state,7,9,1,0));start_stream(&m);
        Bytes b=sample_packet(1,101,m.now-192);CHECK(receive(&m,1,&b)==1&&m.delivered==1);CHECK(m.last.unit==EldenMotionG&&m.last.q16[0]==6682);CHECK(m.last.timestamp_ticks==m.state.sensor[0].last_ssc+192);
        CHECK(elden_ssc_demand(&m.state,7,9,1,401)==EldenMotionArgument);normal_stop(&m);CHECK(elden_ssc_receive(&m.state,7,9,&m.server,1,b.b,b.n,m.now)==EldenSscClosed);
    }
    {
        scenarios++;Peer m;init(&m);Bytes early=lookup_packet(1,100);CHECK(receive(&m,0,&early)==EldenSscAgain);CHECK(!m.state.sensor[0].discovered);
        ack(&m,0);CHECK(m.state.sensor[0].discovered&&m.state.conn[0].early_length==0);
    }
    {
        scenarios++;Peer m;init(&m);Bytes early=lookup_packet(1,100);CHECK(receive(&m,0,&early)==EldenSscAgain);CHECK(receive(&m,0,&early)==EldenSscOverflow);CHECK(m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);Bytes b=response_packet(2,100,0);CHECK(receive(&m,0,&b)==EldenMotionStale&&m.state.conn[0].pending);
        EldenSscServer wrong=m.server;wrong.generation++;CHECK(elden_ssc_receive(&m.state,7,9,&wrong,0,b.b,b.n,m.now)==EldenMotionStale);
        CHECK(elden_ssc_receive(&m.state,7,10,&m.server,0,b.b,b.n,m.now)==EldenMotionStale);
    }
    for(int mode=1;mode<=4;mode++){
        scenarios++;Peer m;init(&m);discover(&m);Bytes b=attributes(1,101,mode);int rc=receive(&m,1,&b);CHECK(rc<0&&rc!=EldenSscAgain&&m.delivered==0&&m.qualifies==0&&m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);discover(&m);m.bad_spec=1;Bytes b=attributes(1,101,0);CHECK(receive(&m,1,&b)==EldenMotionPeer&&m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);discover(&m);m.qualify_error=-13;Bytes b=attributes(1,101,0);CHECK(receive(&m,1,&b)==-13&&m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);start_stream(&m);m.bad_clock=1;Bytes b=sample_packet(1,101,m.now);CHECK(receive(&m,1,&b)==EldenMotionClock&&m.delivered==0);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);start_stream(&m);Bytes b=sample_packet(1,101,m.now);CHECK(receive(&m,1,&b)==1);CHECK(receive(&m,1,&b)==EldenMotionClock&&m.delivered==1);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);start_stream(&m);m.delivery_error=-12;Bytes b=sample_packet(1,101,m.now);CHECK(receive(&m,1,&b)==-12&&m.delivered==0&&m.state.sensor[0].last_ssc==0);
    }
    {
        scenarios++;Peer m;init(&m);CHECK(elden_ssc_step(&m.state,m.now+19200*2000)==EldenMotionClock);CHECK(m.state.state==EldenSscHeld&&m.qualifies==0);
    }
    {
        scenarios++;Peer m;init(&m);discover(&m);CHECK(elden_ssc_step(&m.state,m.now+19200*5000)==EldenMotionClock&&m.qualifies==0);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);m.send_error=EldenSscAgain;CHECK(!elden_ssc_demand(&m.state,7,9,1,25));unsigned sent=m.sent_n;
        CHECK(!elden_ssc_step(&m.state,m.now));CHECK(m.sent_n==sent&&m.state.retry_ticks>m.now&&elden_ssc_idle_ms(&m.state,m.now)==100);
        CHECK(elden_ssc_step(&m.state,m.now+1)==EldenSscAgain);m.send_error=0;m.now+=1920000;CHECK(!elden_ssc_step(&m.state,m.now));CHECK(m.sent_n==sent+1);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);m.state.conn[1].next_transaction=UINT16_MAX;CHECK(!elden_ssc_demand(&m.state,7,9,1,25));CHECK(elden_ssc_step(&m.state,m.now)==EldenSscOverflow);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);EldenSscServer replacement=m.server;replacement.generation++;CHECK(elden_ssc_server(&m.state,7,9,&replacement,1,m.now)==EldenMotionStale);
        m.withdraw_error=-16;CHECK(elden_ssc_stop(&m.state,7,9,m.now)==-16&&m.state.state==EldenSscHeld&&m.withdrawn==0);
        m.withdraw_error=0;CHECK(!elden_ssc_stop(&m.state,7,9,m.now));CHECK(m.state.state==EldenSscStopped&&m.withdrawn==3);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);CHECK(elden_ssc_stop(&m.state,7,9,m.now)==EldenSscAgain);CHECK(m.withdrawn==1);
        m.now+=19200*5000;CHECK(!elden_ssc_stop(&m.state,7,9,m.now));CHECK(m.withdrawn==3&&m.state.state==EldenSscStopped);
    }
    {
        scenarios++;Peer m;init(&m);m.now--;CHECK(elden_ssc_step(&m.state,m.now)==EldenMotionClock);CHECK(m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);m.gate_error=1;CHECK(elden_ssc_step(&m.state,m.now)==EldenMotionPeer&&m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);m.state.requests=UINT32_MAX;CHECK(!elden_ssc_demand(&m.state,7,9,1,25));CHECK(elden_ssc_step(&m.state,m.now)==EldenSscOverflow);
    }
    {
        scenarios++;Peer m;init(&m);ready(&m);start_stream(&m);Bytes payload={0};field(&payload,1,5,0x3f800000);field(&payload,2,0,3);
        Bytes packet=report_packet(ids[0],1025,&payload,m.now,101);CHECK(receive(&m,1,&packet)==EldenMotionMalformed&&m.delivered==0);
    }
    {
        scenarios++;Peer m;init(&m);discover(&m);Bytes b=attributes(1,101,6);CHECK(receive(&m,1,&b)==-13&&m.state.state==EldenSscHeld);
    }
    {
        scenarios++;Peer m;init(&m);discover(&m);m.allow_virtual=1;Bytes b=attributes(1,101,6);CHECK(!receive(&m,1,&b)&&m.state.sensor[0].qualified&&m.state.sensor[0].attrs.physical==0);
    }
    {
        scenarios++;Bytes b=attributes(1,101,0);EldenSscAttrs before;memset(&before,0x5a,sizeof(before));
        for(size_t n=0;n<b.n;n++){EldenSscAttrs out=before;CHECK(elden_ssc_attribute_report(b.b,n,101,ids[0],1,&out)<0);CHECK(!memcmp(&out,&before,sizeof(out)));}
        EldenSscAttrs actual={0};CHECK(elden_ssc_attribute_report(b.b,b.n,101,ids[0],1,&actual)==1);CHECK(!strcmp(actual.name,"TEST MODEL"));
        CHECK(elden_ssc_pick_rate(&actual,26)==0x42480000&&elden_ssc_pick_rate(&actual,401)==0);
        Bytes negative_id=attributes(1,101,5);CHECK(elden_ssc_attribute_report(negative_id.b,negative_id.n,101,ids[0],1,&actual)==1&&actual.hw_id==-2);
        Bytes p={0};field(&p,1,0,4);Bytes error=report_packet(ids[0],130,&p,100,101);int64_t value=-1;
        CHECK(elden_ssc_report_error(error.b,error.n,101,ids[0],&value)==1&&value==4);
        error=report_packet(ids[0],6,&p,100,101);CHECK(!elden_ssc_report_error(error.b,error.n,101,ids[0],&value));
    }
    printf("PASS %u actual SSC client/attribute/session/clock/stop scenarios, %u checks; transport/MMIO/clock/mount/HID peers modeled\n",scenarios,checks);return 0;
}
