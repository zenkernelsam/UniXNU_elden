/* SPDX-License-Identifier: BSD-2-Clause-Patent */
static int valid_suid(EldenMotionSuid s){return (s.low||s.high)&&!(s.low==Ssc_SUID_SENSOR_LOW&&s.high==Ssc_SUID_SENSOR_HIGH);}
static int strict_qmi(const uint8_t *p,size_t n,SscQmiMessage *out){
    if(!p||!out||n>Ssc_QMI_HEADER+65535u)return EldenMotionArgument;
    SscQmiMessage m;if(ssc_qmi_parse(p,n,&m)||n!=(size_t)Ssc_QMI_HEADER+m.tlv_length)return EldenMotionMalformed;
    uint8_t tags[32]={0};size_t at=0;
    while(at<m.tlv_length){
        if(m.tlv_length-at<3)return EldenMotionMalformed;
        unsigned tag=m.tlv[at];unsigned len=get16(m.tlv+at+1);
        if(tags[tag/8]&(1u<<(tag%8))||len>m.tlv_length-at-3)return EldenMotionMalformed;
        tags[tag/8]|=(uint8_t)(1u<<(tag%8));at+=len+3;
    }
    *out=m;return 0;
}
static int report(const uint8_t *p,size_t n,uint64_t expected,const uint8_t **event,uint16_t *bytes){
    if(!expected)return EldenMotionArgument;SscQmiMessage m;int rc=strict_qmi(p,n,&m);if(rc)return rc;
    if(m.type!=Ssc_QMI_INDICATION||(m.msg!=Ssc_SNS_CLIENT_REPORT_IND&&m.msg!=Ssc_SNS_CLIENT_JUMBO_REPORT_IND))return EldenMotionPeer;
    const uint8_t *v;uint16_t len;
    if(ssc_qmi_tlv(&m,1,&v,&len)||len!=8||get64(v)!=expected)return EldenMotionPeer;
    if(ssc_qmi_tlv(&m,2,&v,&len)||len<2||get16(v)!=len-2)return EldenMotionMalformed;
    uint64_t client;const uint8_t *data;uint16_t size;
    if(ssc_sns_qmi_report(&m,&client,&data,&size)||client!=expected)return EldenMotionMalformed;
    // First pass validates every nested event before any output/callback.
    if(ssc_sns_events(data,size,0,0)<0)return EldenMotionMalformed;
    *event=data;*bytes=size;return 0;
}
static size_t encoded(uint8_t *out,size_t cap,uint16_t txn,SscSuid suid,uint32_t id,const uint8_t *payload,size_t n){
    if(!out||!txn||(!payload&&n)||n>128)return 0;
    uint8_t request[256],packet[512];size_t bytes=ssc_sns_request(request,sizeof(request),suid,id,payload,n);
    if(!bytes)return 0;bytes=ssc_sns_qmi_request(packet,sizeof(packet),txn,request,bytes);
    if(!bytes||bytes>cap)return 0;copy(out,packet,bytes);return bytes;
}
size_t elden_motion_discovery_request(uint8_t *out,size_t cap,uint16_t txn,uint32_t kind){
    if(kind!=EldenMotionAccel&&kind!=EldenMotionGyro)return 0;
    uint8_t payload[32];size_t n=ssc_sns_suid_req(payload,sizeof(payload),kind==EldenMotionAccel?"accel":"gyro",1,1);
    SscSuid source={Ssc_SUID_SENSOR_LOW,Ssc_SUID_SENSOR_HIGH};return encoded(out,cap,txn,source,Ssc_SNS_SUID_REQ,payload,n);
}
size_t elden_motion_sampling_request(uint8_t *out,size_t cap,uint16_t txn,EldenMotionSuid id,uint32_t hz,int disable){
    if(!valid_suid(id)||(!disable&&(!hz||hz>400))||(disable!=0&&disable!=1))return 0;
    SscSuid suid={id.low,id.high};uint8_t data[16];size_t n=disable?0:ssc_sns_sensor_config(data,sizeof(data),hz);
    return encoded(out,cap,txn,suid,disable?Ssc_SNS_STD_DISABLE:Ssc_SNS_STD_SENSOR_CONFIG,data,n);
}
int elden_motion_client_response(const uint8_t *p,size_t n,uint16_t txn,uint64_t *out){
    if(!out||!txn)return EldenMotionArgument;SscQmiMessage m;int rc=strict_qmi(p,n,&m);if(rc)return rc;
    if(m.type!=Ssc_QMI_RESPONSE||m.msg!=Ssc_SNS_CLIENT_REQ||m.txn!=txn)return EldenMotionPeer;
    const uint8_t *v;uint16_t len;
    if(ssc_qmi_tlv(&m,2,&v,&len)||len!=4||ssc_qmi_tlv(&m,0x10,&v,&len)||len!=8||ssc_qmi_tlv(&m,0x11,&v,&len)||len!=4)return EldenMotionMalformed;
    uint16_t result,error;uint64_t client;int32_t sns;
    if(ssc_sns_qmi_response(&m,&result,&error,&client,&sns)||result||error||sns||!client)return EldenMotionPeer;
    *out=client;return 0;
}
struct Discovery {uint32_t kind;EldenMotionSuid found;int has,error;};
static void discovery(void *v,SscSuid from,uint32_t msg,uint64_t time,const uint8_t *p,size_t n){
    struct Discovery *d=v;(void)time;if(msg!=Ssc_SNS_SUID_EVENT)return;
    if(from.low!=Ssc_SUID_SENSOR_LOW||from.high!=Ssc_SUID_SENSOR_HIGH){d->error=EldenMotionPeer;return;}
    char type[16];SscSuid ids[4];int count=ssc_sns_suid_event(p,n,type,sizeof(type),ids,4);
    if(count<0){d->error=EldenMotionMalformed;return;}
    const char *wanted=d->kind==EldenMotionAccel?"accel":"gyro";unsigned i=0;
    while(wanted[i]&&type[i]==wanted[i])i++;if(wanted[i]||type[i])return;
    EldenMotionSuid actual={ids[0].low,ids[0].high};
    if(count!=1||!valid_suid(actual)||(d->has&&(d->found.low!=actual.low||d->found.high!=actual.high))){d->error=EldenMotionPeer;return;}
    d->found=actual;d->has=1;
}
int elden_motion_discovery_report(const uint8_t *p,size_t n,uint64_t client,uint32_t kind,EldenMotionSuid *out){
    if(!out||(kind!=EldenMotionAccel&&kind!=EldenMotionGyro))return EldenMotionArgument;
    const uint8_t *event;uint16_t bytes;int rc=report(p,n,client,&event,&bytes);if(rc)return rc;
    struct Discovery d={0};d.kind=kind;if(ssc_sns_events(event,bytes,discovery,&d)<0)return EldenMotionMalformed;
    if(d.error)return d.error;if(!d.has)return 0;*out=d.found;return 1;
}
static int spec_valid(const EldenMotionSpec *s){
    if(!s||!valid_suid(s->suid)||s->max_abs_q16<=0||s->max_abs_q16>1000*65536)return 0;
    if(s->kind==EldenMotionAccel){if(s->unit!=EldenMotionG&&s->unit!=EldenMotionMetersPerSecond2)return 0;}
    else if(s->kind!=EldenMotionGyro||s->unit!=EldenMotionRadiansPerSecond)return 0;
    unsigned mask=0,neg=0,inversions=0;
    for(unsigned i=0;i<3;i++){
        int v=s->mount[i];if(v<0){neg++;v=-v;}if(v<1||v>3||mask&(1u<<v))return 0;mask|=1u<<v;
        for(unsigned j=0;j<i;j++){int before=s->mount[j];if(before<0)before=-before;if(before>v)inversions++;}
    }
    return ((neg+inversions)&1)==0;
}
struct Samples {const EldenMotionSpec *spec;EldenMotionSample values[16];size_t count;int error;};
static void sample(void *v,SscSuid from,uint32_t msg,uint64_t timestamp,const uint8_t *p,size_t n){
    struct Samples *s=v;if(msg!=Ssc_SNS_STD_SENSOR_EVENT)return;
    if(from.low!=s->spec->suid.low||from.high!=s->spec->suid.high)return;
    if(s->count==16||!timestamp||(s->count&&timestamp<=s->values[s->count-1].timestamp_ticks)){s->error=EldenMotionMalformed;return;}
    uint32_t bits[3];int32_t status;int count=ssc_sns_sensor_event(p,n,bits,3,&status);
    if(count!=3||status<0||status>3){s->error=EldenMotionMalformed;return;}
    int32_t raw[3];
    for(unsigned i=0;i<3;i++){
        if((bits[i]&0x7f800000u)==0x7f800000u){s->error=EldenMotionMalformed;return;}
        raw[i]=ssc_f32_scale(bits[i],65536);
        if(raw[i]>(int64_t)s->spec->max_abs_q16||raw[i]<-(int64_t)s->spec->max_abs_q16){s->error=EldenMotionMalformed;return;}
    }
    EldenMotionSample r={0};r.suid=s->spec->suid;r.timestamp_ticks=timestamp;r.kind=s->spec->kind;r.unit=s->spec->unit;r.accuracy=(uint32_t)status;
    for(unsigned i=0;i<3;i++){int axis=s->spec->mount[i];r.q16[i]=axis<0?-raw[-axis-1]:raw[axis-1];}
    if(r.unit==EldenMotionMetersPerSecond2){for(unsigned i=0;i<3;i++)r.q16[i]=(int32_t)((int64_t)r.q16[i]*100000/980665);r.unit=EldenMotionG;}
    s->values[s->count++]=r;
}
int elden_motion_samples(const uint8_t *p,size_t n,uint64_t client,const EldenMotionSpec *spec,EldenMotionSample *out,size_t capacity){
    if(!out||!capacity||capacity>16||!spec_valid(spec))return EldenMotionArgument;
    const uint8_t *event;uint16_t bytes;int rc=report(p,n,client,&event,&bytes);if(rc)return rc;
    struct Samples s={0};s.spec=spec;
    if(ssc_sns_events(event,bytes,sample,&s)<0||s.error||s.count>capacity)return s.error?s.error:EldenMotionMalformed;
    for(size_t i=0;i<s.count;i++)out[i]=s.values[i];return (int)s.count;
}
int elden_motion_rotation(EldenMotionRotation *r,const EldenMotionSample *s,uint32_t hold_us,int32_t *out){
    if(!r||!s||!out||s->kind!=EldenMotionAccel||s->unit!=EldenMotionG||!hold_us||hold_us>1000000||!s->timestamp_ticks)return EldenMotionArgument;
    if(r->clock_failed)return EldenMotionClock;
    if(r->has_clock&&s->timestamp_ticks<r->last_ticks){r->clock_failed=1;return EldenMotionClock;}
    if(r->has_clock&&s->timestamp_ticks==r->last_ticks)return EldenMotionStale;
    r->has_clock=1;r->last_ticks=s->timestamp_ticks;
    int64_t x=s->q16[0],y=s->q16[1],z=s->q16[2],ax=x<0?-x:x,ay=y<0?-y:y;
    // Only plausible gravity vectors; no clock hold across flat/poor samples.
    if(ax>2*65536||ay>2*65536||z>2*65536||z<-2*65536){r->candidate=0;return 0;}
    int64_t magnitude=x*x+y*y+z*z;int32_t candidate=0;
    if(s->accuracy&&magnitude>=(int64_t)49152*49152&&magnitude<=(int64_t)81920*81920){
        if(ax>=26214&&ax*10>=ay*14)candidate=x<0?-1:1;
        else if(ay>=26214&&ay*10>=ax*14)candidate=y<0?-2:2;
    }
    if(!candidate){r->candidate=0;return 0;}
    if(candidate!=r->candidate){r->candidate=candidate;r->candidate_since=s->timestamp_ticks;return 0;}
    uint64_t hold=((uint64_t)hold_us*96+4)/5; // SSC's standard19.2MHz ticks; native sync still required
    if(candidate==r->axis||s->timestamp_ticks-r->candidate_since<hold)return 0;
    r->axis=candidate;*out=candidate;return 1;
}
