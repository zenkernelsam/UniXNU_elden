/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Strict envelope/schema around the qualified original Pagani wire bodies.
 * Protobuf field numbers and enum values come from the fixed source manifest;
 * no board-specific unit, mount, clock or sensor readiness is fabricated. */
#include "Ssc.h"
#include "SscWire.h"
#include "SscPb.h"
#include "SscAttrPrivate.inc"

static int same_suid(EldenMotionSuid a,EldenMotionSuid b){return a.low==b.low&&a.high==b.high;}
static int text_equal(const char *a,const char *b){unsigned i=0;while(a[i]&&a[i]==b[i])i++;return a[i]==b[i];}
static int qmi(const uint8_t *p,size_t n,SscQmi *out){
    if(!p||n<7||n>EldenSscMaxPacket||elden_ssc_wire_qmi_parse(p,(uint32_t)n,out)||out->length!=n-7)return EldenMotionMalformed;
    uint8_t seen[32]={0};
    for(uint32_t at=0;at<out->length;){
        if(out->length-at<3)return EldenMotionMalformed;
        uint8_t tag=out->tlvs[at];uint32_t len=out->tlvs[at+1]|(uint32_t)out->tlvs[at+2]<<8;
        if(seen[tag/8]&(1u<<(tag%8))||len>out->length-at-3)return EldenMotionMalformed;
        seen[tag/8]|=(uint8_t)(1u<<(tag%8));at+=3+len;
    }
    return 0;
}
int elden_ssc_packet_identity(const uint8_t *p,size_t n,uint8_t *type,uint16_t *txn){
    if(!type||!txn)return EldenMotionArgument;SscQmi m;int rc=qmi(p,n,&m);if(rc)return rc;
    if(m.message!=SSC_QMI_REQ&&m.message!=SSC_QMI_REPORT)return EldenMotionPeer;
    if(m.type!=SSC_QMI_RESPONSE&&m.type!=SSC_QMI_INDICATION)return EldenMotionPeer;
    if((m.type==SSC_QMI_RESPONSE&&m.message!=SSC_QMI_REQ)||(m.type==SSC_QMI_INDICATION&&m.message!=SSC_QMI_REPORT))return EldenMotionPeer;
    *type=m.type;*txn=m.transaction;return 0;
}
static int read_suid(const uint8_t *p,uint32_t n,EldenMotionSuid *out){
    SpbIn in;uint32_t field,wire,seen=0;int rc;EldenMotionSuid s={0};elden_ssc_pb_in(&in,p,n);
    while((rc=elden_ssc_pb_next(&in,&field,&wire))==1){
        if(field==1||field==2){uint32_t bit=1u<<(field-1);uint64_t v;
            if(seen&bit||elden_ssc_pb_fixed64_value(&in,wire,&v))return EldenMotionMalformed;
            seen|=bit;if(field==1)s.low=v;else s.high=v;
        }else if(elden_ssc_pb_skip(&in,wire))return EldenMotionMalformed;
    }
    if(rc<0||seen!=3||(!s.low&&!s.high))return EldenMotionMalformed;*out=s;return 0;
}
typedef struct {EldenMotionSuid suid;SscEvent event[24];uint32_t count;} Events;
static int sample_payload(const uint8_t *p,uint32_t n){
    SpbIn in;uint32_t field,wire,bits[3];unsigned count=0,more=0,status=0;int rc;elden_ssc_pb_in(&in,p,n);
    while((rc=elden_ssc_pb_next(&in,&field,&wire))==1){
        if(field==1){if(elden_ssc_pb_fixed32_values(&in,wire,bits,3,&count,&more))return EldenMotionMalformed;}
        else if(field==2){uint64_t value;if(status++||elden_ssc_pb_varint_value(&in,wire,&value)||value>3)return EldenMotionMalformed;}
        else if(elden_ssc_pb_skip(&in,wire))return EldenMotionMalformed;
    }
    if(rc<0||count!=3||more||status!=1)return EldenMotionMalformed;
    for(unsigned i=0;i<3;i++)if((bits[i]&0x7f800000)==0x7f800000)return EldenMotionMalformed;return 0;
}
static int events(const uint8_t *p,size_t n,uint64_t client,Events *out){
    if(!client)return EldenMotionArgument;SscQmi m;int rc=qmi(p,n,&m);if(rc)return rc;
    if(m.type!=SSC_QMI_INDICATION||m.message!=SSC_QMI_REPORT)return EldenMotionPeer;
    uint32_t size;const uint8_t *v=elden_ssc_wire_qmi_tlv(&m,1,&size);uint64_t id=0;
    if(!v||size!=8)return EldenMotionMalformed;for(unsigned i=0;i<8;i++)id|=(uint64_t)v[i]<<(i*8);
    if(id!=client)return EldenMotionPeer;
    v=elden_ssc_wire_qmi_tlv(&m,2,&size);
    if(!v||size<2||(uint32_t)(v[0]|(uint32_t)v[1]<<8)!=size-2||size-2>SSC_QMI_MAX_PAYLOAD)return EldenMotionMalformed;
    Events e={0};SpbIn in;uint32_t field,wire,suid_seen=0;elden_ssc_pb_in(&in,v+2,size-2);
    while((rc=elden_ssc_pb_next(&in,&field,&wire))==1){
        if(field==1){const uint8_t *b;uint32_t len;
            if(suid_seen++||elden_ssc_pb_bytes_value(&in,wire,&b,&len)||read_suid(b,len,&e.suid))return EldenMotionMalformed;
        }else if(field==2){
            const uint8_t *b;uint32_t len;if(e.count==24||elden_ssc_pb_bytes_value(&in,wire,&b,&len))return EldenMotionMalformed;
            SscEvent *s=&e.event[e.count];SpbIn one;uint32_t sf,sw,seen=0;int next;elden_ssc_pb_in(&one,b,len);
            while((next=elden_ssc_pb_next(&one,&sf,&sw))==1){
                if(sf>=1&&sf<=3){uint32_t bit=1u<<(sf-1);if(seen&bit)return EldenMotionMalformed;seen|=bit;
                    if(sf==1){if(elden_ssc_pb_fixed32_value(&one,sw,&s->msg_id))return EldenMotionMalformed;}
                    else if(sf==2){if(elden_ssc_pb_fixed64_value(&one,sw,&s->timestamp))return EldenMotionMalformed;}
                    else if(elden_ssc_pb_bytes_value(&one,sw,&s->payload,&s->length))return EldenMotionMalformed;
                }else if(elden_ssc_pb_skip(&one,sw))return EldenMotionMalformed;
            }
            if(next<0||seen!=7)return EldenMotionMalformed;
            if(s->msg_id==SSC_MSG_SENSOR_EVENT&&sample_payload(s->payload,s->length))return EldenMotionMalformed;
            e.count++;
        }else if(elden_ssc_pb_skip(&in,wire))return EldenMotionMalformed;
    }
    if(rc<0||suid_seen!=1)return EldenMotionMalformed;*out=e;return 0;
}
static size_t encode(uint8_t *out,size_t cap,uint16_t txn,EldenMotionSuid suid,uint32_t msg,const uint8_t *data,uint32_t size){
    if(!out||!txn||(!suid.low&&!suid.high)||(!data&&size))return 0;
    uint8_t body[160],packet[200];SscSuid id={suid.low,suid.high};
    uint32_t n=elden_ssc_wire_build_request(body,sizeof(body),&id,msg,SSC_DELIVERY_WAKEUP,data,size,-1);
    n=n?elden_ssc_wire_build_qmi_request(packet,sizeof(packet),txn,body,n,0):0;
    if(!n||n>cap)return 0;for(unsigned i=0;i<n;i++)out[i]=packet[i];return n;
}
size_t elden_ssc_discovery_request(uint8_t *out,size_t cap,uint16_t txn,unsigned kind){
    if(kind!=1&&kind!=2)return 0;uint8_t data[32];uint32_t n=elden_ssc_wire_build_suid_req(data,sizeof(data),kind==1?"accel":"gyro",1,1);
    EldenMotionSuid id={UINT64_C(0xabababababababab),UINT64_C(0xabababababababab)};return encode(out,cap,txn,id,SSC_MSG_SUID_REQ,data,n);
}
size_t elden_ssc_attribute_request(uint8_t *out,size_t cap,uint16_t txn,EldenMotionSuid id){return encode(out,cap,txn,id,SSC_MSG_ATTR_REQ,0,0);}
static int positive(uint32_t bits){return !(bits>>31)&&(bits>>23&255)!=0&&(bits>>23&255)!=255;}
size_t elden_ssc_sampling_request(uint8_t *out,size_t cap,uint16_t txn,EldenMotionSuid id,uint32_t rate,int disable){
    if(disable!=0&&disable!=1)return 0;if(!disable&&(!positive(rate)||rate>UINT32_C(0x43c80000)))return 0;
    uint8_t data[16];uint32_t n=disable?0:elden_ssc_wire_build_sensor_config(data,sizeof(data),rate);
    return encode(out,cap,txn,id,disable?SSC_MSG_DISABLE_REQ:SSC_MSG_SENSOR_CONFIG,data,n);
}
/* The fixed T970 scalar conversion, bounded here before any shift/product.
 * Float bits carry actual firmware rates/ranges; no FP/SIMD in kernel. */
static int32_t q16(uint32_t bits){
    int exponent=(int)(bits>>23&255);if(exponent==255)return INT32_MIN;if(!exponent)return 0;
    uint64_t product=(uint64_t)((bits&0x7fffff)|0x800000)*65536;int shift=exponent-150;uint64_t v;
    if(shift>=0){if(shift>7)return INT32_MIN;v=product<<shift;}
    else if(shift>-64)v=(product+(UINT64_C(1)<<(-shift-1)))>>-shift;else v=0;
    if(v>INT32_MAX)return INT32_MIN;return bits>>31?-(int32_t)v:(int32_t)v;
}
static int string(char *out,unsigned cap,const AttrDatum *d){
    if(d->kind!=2||!d->size)return EldenMotionMalformed;
    uint32_t n=d->size;if(d->bytes[n-1]==0)n--; // OEM publishers may count their terminator
    if(!n||n>=cap)return EldenMotionMalformed;
    for(unsigned i=0;i<n;i++){if(d->bytes[i]<32||d->bytes[i]>=127)return EldenMotionMalformed;out[i]=(char)d->bytes[i];}
    out[n]=0;return 0;
}
static int attribute(EldenSscAttrs *a,uint32_t id,const uint8_t *p,uint32_t n){
    AttrDatum d[SSC_MAX_RATES];unsigned count=0;if(attr_values(p,n,d,SSC_MAX_RATES,&count)||!count)return EldenMotionMalformed;
    switch(id){
    case SSC_ATTR_NAME:case SSC_ATTR_VENDOR:case SSC_ATTR_TYPE:case SSC_ATTR_API:
        if(count!=1)return EldenMotionMalformed;
        return string(id==0?a->name:id==1?a->vendor:id==2?a->type:a->api,id==5?sizeof(a->api):32,&d[0]);
    case SSC_ATTR_AVAILABLE:case SSC_ATTR_PHYSICAL_SENSOR:
        if(count!=1||d[0].kind!=5)return EldenMotionMalformed;
        if(id==SSC_ATTR_AVAILABLE)a->available=(uint8_t)d[0].sint;else a->physical=(uint8_t)d[0].sint;return 0;
    case SSC_ATTR_STREAM_TYPE:case SSC_ATTR_HW_ID:
        if(count!=1||d[0].kind!=4)return EldenMotionMalformed;
        if(id==SSC_ATTR_STREAM_TYPE){if(d[0].sint<0||d[0].sint>2)return EldenMotionMalformed;a->stream_type=(uint8_t)d[0].sint;}else a->hw_id=d[0].sint;return 0;
    case SSC_ATTR_RATES:
        for(unsigned i=0;i<count;i++){
            if(d[i].kind!=3||!positive(d[i].flt))return EldenMotionMalformed;
            for(unsigned j=0;j<i;j++)if(d[j].flt==d[i].flt)return EldenMotionMalformed;
            a->rates[i]=d[i].flt;
        }a->rate_count=count;return 0;
    case SSC_ATTR_RANGES:
        if(count>EldenSscMaxRanges)return EldenMotionMalformed;
        for(unsigned i=0;i<count;i++){
            AttrDatum pair[2];unsigned size=0;
            if(d[i].kind!=1||attr_values(d[i].bytes,d[i].size,pair,2,&size)||size!=2||pair[0].kind!=3||pair[1].kind!=3)return EldenMotionMalformed;
            int32_t lo=q16(pair[0].flt),hi=q16(pair[1].flt);
            if(lo==INT32_MIN||lo>=0||hi<=0||hi>1000*65536||lo<-1000*65536)return EldenMotionMalformed;
            a->ranges[i][0]=pair[0].flt;a->ranges[i][1]=pair[1].flt;
        }a->range_count=count;return 0;
    default:return 0;
    }
}
static int attrs(const uint8_t *p,uint32_t n,EldenSscAttrs *out){
    SpbIn in;uint32_t field,wire,ids[32],count=0;int rc;EldenSscAttrs a={0};a.hw_id=-1;elden_ssc_pb_in(&in,p,n);
    while((rc=elden_ssc_pb_next(&in,&field,&wire))==1){
        if(field!=1){if(elden_ssc_pb_skip(&in,wire))return EldenMotionMalformed;continue;}
        const uint8_t *b,*value=0;uint32_t len,value_size=0,seen=0;uint64_t id=0;
        if(count==32||elden_ssc_pb_bytes_value(&in,wire,&b,&len))return EldenMotionMalformed;
        SpbIn one;uint32_t sf,sw;int next;elden_ssc_pb_in(&one,b,len);
        while((next=elden_ssc_pb_next(&one,&sf,&sw))==1){
            if(sf==1){if(seen&1||elden_ssc_pb_varint_value(&one,sw,&id)||id>UINT32_MAX)return EldenMotionMalformed;seen|=1;}
            else if(sf==2){if(seen&2||elden_ssc_pb_bytes_value(&one,sw,&value,&value_size))return EldenMotionMalformed;seen|=2;}
            else if(elden_ssc_pb_skip(&one,sw))return EldenMotionMalformed;
        }
        if(next<0||seen!=3)return EldenMotionMalformed;
        for(unsigned i=0;i<count;i++)if(ids[i]==id)return EldenMotionMalformed;ids[count++]=(uint32_t)id;
        if(attribute(&a,(uint32_t)id,value,value_size))return EldenMotionMalformed;
        if(id<32)a.seen|=1u<<id;
    }
    uint32_t required=(1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<11)|(1u<<16)|(1u<<21);
    if(rc<0)return EldenMotionMalformed;if((a.seen&required)!=required)return EldenSscAgain;
    // physical_sensor describes the SSC implementation, not board presence.
    // A calibrated virtual stream may legitimately report0. The board
    // qualifier must approve its real source/calibration; never assume true.
    if(!a.available||a.stream_type!=SSC_STREAM_STREAMING)return EldenMotionPeer;
    *out=a;return 0;
}
int elden_ssc_attribute_report(const uint8_t *p,size_t n,uint64_t client,EldenMotionSuid id,unsigned kind,EldenSscAttrs *out){
    if(!out||(kind!=1&&kind!=2))return EldenMotionArgument;Events e;int rc=events(p,n,client,&e);if(rc)return rc;
    if(!same_suid(e.suid,id))return EldenMotionPeer;EldenSscAttrs result;unsigned found=0;
    for(unsigned i=0;i<e.count;i++)if(e.event[i].msg_id==SSC_MSG_ATTR_EVENT){
        if(found++)return EldenMotionMalformed;rc=attrs(e.event[i].payload,e.event[i].length,&result);if(rc)return rc;
        if(!text_equal(result.type,kind==1?"accel":"gyro"))return EldenMotionPeer;
    }
    if(!found)return 0;*out=result;return 1;
}
int elden_ssc_report_error(const uint8_t *p,size_t n,uint64_t client,EldenMotionSuid id,int64_t *out){
    if(!out)return EldenMotionArgument;Events e;int rc=events(p,n,client,&e);if(rc)return rc;if(!same_suid(e.suid,id))return EldenMotionPeer;
    int64_t error=0;unsigned found=0;
    for(unsigned i=0;i<e.count;i++)if(e.event[i].msg_id==SSC_MSG_ERROR_EVENT){
        if(found++)return EldenMotionMalformed;SpbIn in;uint32_t f,w,seen=0;uint64_t value=0;int next;
        elden_ssc_pb_in(&in,e.event[i].payload,e.event[i].length);
        while((next=elden_ssc_pb_next(&in,&f,&w))==1){
            if(f==1){if(seen++||elden_ssc_pb_varint_value(&in,w,&value)||value>INT32_MAX)return EldenMotionMalformed;}
            else if(elden_ssc_pb_skip(&in,w))return EldenMotionMalformed;
        }
        if(next<0||seen!=1)return EldenMotionMalformed;error=(int64_t)value;
    }
    if(!found)return 0;*out=error;return 1;
}
uint32_t elden_ssc_pick_rate(const EldenSscAttrs *a,uint32_t hz){
    if(!a||!hz||hz>400||!a->rate_count||a->rate_count>EldenSscMaxRates)return 0;
    unsigned top=31;while(!(hz>>top))top--;uint32_t demand=(top+127)<<23|((hz<<(23-top))&0x7fffff),best=0;
    for(unsigned i=0;i<a->rate_count;i++){uint32_t r=a->rates[i];if(positive(r)&&r>=demand&&r<=UINT32_C(0x43c80000)&&(!best||r<best))best=r;}
    return best;
}
int elden_ssc_spec_valid(const EldenMotionSpec *s,const EldenSscAttrs *a,unsigned kind,EldenMotionSuid id){
    if(!s||!a||!same_suid(s->suid,id)||s->kind!=kind||kind<1||kind>2||s->max_abs_q16<=0||s->max_abs_q16>1000*65536||!a->available||a->stream_type!=0||!a->range_count||a->range_count>EldenSscMaxRanges)return EldenMotionArgument;
    if((kind==1&&s->unit!=EldenMotionG&&s->unit!=EldenMotionMetersPerSecond2)||(kind==2&&s->unit!=EldenMotionRadiansPerSecond))return EldenMotionArgument;
    unsigned matched=0,mask=0,negative=0,inversions=0;
    for(unsigned i=0;i<a->range_count;i++){
        int32_t lo=q16(a->ranges[i][0]),hi=q16(a->ranges[i][1]);
        if(lo==INT32_MIN)return EldenMotionMalformed;int64_t max=-(int64_t)lo;if(hi>max)max=hi;if(s->max_abs_q16==max)matched=1;
    }
    if(!matched)return EldenMotionPeer;
    for(unsigned i=0;i<3;i++){
        int v=s->mount[i];if(v<0){negative++;v=-v;}if(v<1||v>3||(mask&(1u<<v)))return EldenMotionArgument;mask|=1u<<v;
        for(unsigned j=0;j<i;j++){int before=s->mount[j];if(before<0)before=-before;if(before>v)inversions++;}
    }
    return ((negative+inversions)&1)?EldenMotionArgument:0;
}
