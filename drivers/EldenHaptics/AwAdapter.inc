/* SPDX-License-Identifier: GPL-2.0-only */
static enum EldenAwOperation aw_operation(EldenAwChip *c){
    return c->cleanup?EldenAwStop:c->inspecting?EldenAwInspect:c->busy?EldenAwPlay:c->ready?EldenAwPlay:EldenAwConfigure;
}
static int aw_admitted(EldenAwChip *c){
    if(c->sticky_error&&!c->cleanup)return c->sticky_error;
    return c->io.admit(c->io.context,c->address,aw_operation(c));
}
static int aw_read_block(EldenAwChip *c,unsigned reg,void *data,size_t bytes){
    if(!c||!data||!bytes||bytes>16||reg>0x80)return EldenAwArgument;
    int rc=aw_admitted(c);if(rc)return rc;
    ++c->reads;return c->io.read(c->io.context,c->address,(uint8_t)reg,data,bytes);
}
static int aw_write_block(EldenAwChip *c,unsigned reg,const void *data,size_t bytes){
    if(!c||!data||!bytes||bytes>16||reg>0x80)return EldenAwArgument;
    int rc=aw_admitted(c);if(rc)return rc;
    const uint8_t *raw=data;
    const int go=c->starting&&reg==0x09&&bytes==1&&raw[0]==1&&c->play_mode==0;
    if(go){
        uint64_t now=c->io.now_us(c->io.context);
        if(now<c->last_clock||now>=c->setup_deadline||now>UINT64_MAX-c->requested_duration)return EldenAwTimeout;
        c->last_clock=now;c->deadline=now+c->requested_duration;
    }
    ++c->writes;rc=c->io.write(c->io.context,c->address,(uint8_t)reg,data,bytes);
    if(!rc&&reg==0x08&&bytes==1)c->play_mode=raw[0]&3u;
    return rc;
}
static int aw_reg_read(EldenAwChip *c,unsigned reg,unsigned *value){
    uint8_t v;int rc=aw_read_block(c,reg,&v,1);if(!rc){*value=v;if(reg==0x08)c->play_mode=v&3u;}return rc;
}
static int aw_reg_write(EldenAwChip *c,unsigned reg,unsigned value){
    if(value>255)return EldenAwArgument;uint8_t v=(uint8_t)value;return aw_write_block(c,reg,&v,1);
}
static int aw_reg_update(EldenAwChip *c,unsigned reg,unsigned mask,unsigned value){
    if(mask>255||value&~mask)return EldenAwArgument;
    unsigned old;int rc=aw_reg_read(c,reg,&old);if(rc)return rc;
    unsigned wanted=(old&~mask)|value;
    return old==wanted?0:aw_reg_write(c,reg,wanted);
}
static int aw_many(EldenAwChip *c,const struct reg_sequence *rows,size_t count){
    for(size_t i=0;i<count;++i){int rc=aw_reg_write(c,rows[i].reg,rows[i].def);if(rc)return rc;}return 0;
}
static void aw_sleep(EldenAwChip *c,unsigned low,unsigned high){
    int rc=aw_admitted(c);if(!rc)rc=c->io.delay_us(c->io.context,low,high);
    if(rc)c->sticky_error=rc;
}
static int aw_wait(EldenAwChip *c){
    uint64_t start=c->io.now_us(c->io.context),last=start;
    for(unsigned i=0;i<100;++i){
        uint64_t now=c->io.now_us(c->io.context);
        if(now<last||now-start>250000)return EldenAwTimeout;last=now;
        unsigned value;int rc=aw_reg_read(c,0x3f,&value);if(rc)return rc;
        if(!(value&15))return 0;
        aw_sleep(c,2500,2500);if(c->sticky_error)return c->sticky_error;
    }
    return EldenAwTimeout;
}
static int aw_request(const struct firmware **out,const char *name,EldenAwChip *c){
    (void)name;
    /* One request is executing on the shared owning worker. A local static
     * record is safe only under that external serialization contract. */
    static struct firmware record;
    if(!c->image||!c->image_bytes)return EldenAwArgument;
    int rc=c->io.firmware(c->io.context,c->image,c->image_bytes);if(rc)return rc;
    record.data=c->image;record.size=c->image_bytes;*out=&record;return 0;
}
static void aw_release(const struct firmware *record){(void)record;}
