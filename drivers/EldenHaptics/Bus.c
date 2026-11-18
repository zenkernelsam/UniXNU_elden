/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Bus.h"
#define ELDEN_GENI_TX_STATUS 0x800u
static const uint32_t configured[]={GENI_DMA_MODE,GENI_TX_PACK0,GENI_TX_PACK1,GENI_RX_PACK0,GENI_RX_PACK1,
    GENI_BYTE_GRAN,GENI_TX_WATERMARK,GENI_M_IRQ_EN,GENI_SE_IRQ_EN,GENI_TX_LEN,GENI_RX_LEN};
static int read_reg(EldenHapticsBus *b,uint32_t reg,uint32_t *value){
    int rc=b->io.admit(b->io.context,b->cleanup);if(rc)return rc;
    return b->io.read(b->io.context,reg,value);
}
static int write_reg(EldenHapticsBus *b,uint32_t reg,uint32_t value){
    int rc=b->io.admit(b->io.context,b->cleanup);if(rc)return rc;
    return b->io.write(b->io.context,reg,value);
}
static uint32_t core_read(void *context,uint32_t reg){
    EldenHapticsBus *b=context;uint32_t value=UINT32_MAX;
    if(!b->error){int rc=read_reg(b,reg,&value);if(rc)b->error=rc;}
    return value;
}
static void core_write(void *context,uint32_t reg,uint32_t value){
    EldenHapticsBus *b=context;
    if(!b->error){int rc=write_reg(b,reg,reg==GENI_SE_IRQ_EN?0:value);if(rc)b->error=rc;}
}
static int idle(EldenHapticsBus *b){
    const uint32_t regs[]={GENI_STATUS,GENI_GSI_EVENT_EN,ELDEN_GENI_TX_STATUS,GENI_RX_FIFO_STATUS};
    uint32_t values[4];
    for(unsigned i=0;i<4;++i){int rc=read_reg(b,regs[i],&values[i]);if(rc)return rc;}
    if(values[0]&((1u<<0)|(1u<<12))||values[1]||values[2]||values[3])return EldenHapticsBusBusy;
    return 0;
}
int elden_haptics_bus_acquire(EldenHapticsBus *b,const EldenHapticsBusIo *io){
    if(!b||!io||!io->read||!io->write||!io->admit||!io->now_us||!io->delay_us)return EldenHapticsBusArgument;
    if(b->acquired||b->transferring||b->held)return EldenHapticsBusBusy;
    EldenHapticsBus value={0};value.io=*io;*b=value;
    int rc=idle(b);if(rc)return rc;
    uint32_t revision,clock,disabled,counters,dma,param;
    if((rc=read_reg(b,GENI_FW_REVISION,&revision))||(rc=read_reg(b,GENI_CLK_CFG,&clock))||
       (rc=read_reg(b,GENI_IF_DISABLE,&disabled))||(rc=read_reg(b,GENI_SCL_COUNTERS,&counters))||
       (rc=read_reg(b,GENI_DMA_MODE,&dma))||(rc=read_reg(b,0xe24,&param)))return rc;
    if(((revision>>8)&255)!=GENI_PROTO_I2C||!(clock&1)||disabled&1||!counters||dma)return EldenHapticsBusError;
    // Fixed qcom-geni-se TX parameter fields: width32 and >=16 FIFO words.
    // A complete <=64-byte write fits without an unproved refill interrupt.
    if(((param>>24)&63)!=32||((param>>16)&255)<16)return EldenHapticsBusError;
    for(unsigned i=0;i<sizeof(configured)/sizeof(*configured);++i){rc=read_reg(b,configured[i],&b->saved[i]);if(rc)return rc;}
    EldenHapticsGeniIo adapter={core_read,core_write,b};b->acquired=1;
    rc=elden_haptics_geni_prepare(&b->core,&adapter,0x5a,&b->configuration);
    if(rc||b->error){b->held=1;return b->error?b->error:EldenHapticsBusError;}
    return 0;
}
static int wait_transfer(EldenHapticsBus *b){
    uint64_t start=b->io.now_us(b->io.context),last=start;unsigned deadline_phase=0;
    for(unsigned polls=0;polls<1600;++polls){
        uint64_t now=b->io.now_us(b->io.context);
        if(now<last){b->held=1;return EldenHapticsBusTimeout;}last=now;
        if(now-start>=10000){
            if(deadline_phase>=3){b->held=1;return EldenHapticsBusTimeout;}
            b->cleanup=1;int status=elden_haptics_geni_timeout(&b->core);
            ++deadline_phase;++b->cancelled;start=now;
            if(b->error){b->held=1;return b->error;}
            if(status==GeniFailed){
                /* The original core ends abort-unacknowledged as idle.
                 * Metadata alone is insufficient; no new command until reset. */
                b->held=1;return EldenHapticsBusTimeout;
            }
        }
        int status=elden_haptics_geni_poll(&b->core);
        if(b->error){b->held=1;return b->error;}
        if(status!=GeniPending){
            int rc=idle(b);
            if(rc){b->held=1;return rc;}
            return status==GeniDone?0:(b->core.error==GeniErrTimeout?EldenHapticsBusTimeout:EldenHapticsBusError);
        }
        int rc=b->io.delay_us(b->io.context,25);if(rc){b->held=1;return rc;}
    }
    b->held=1;return EldenHapticsBusTimeout;
}
static int transaction(EldenHapticsBus *b,uint8_t address,uint8_t reg,uint8_t *to,const uint8_t *from,size_t bytes){
    if(!b||!b->acquired||!bytes||bytes>(to?16u:64u)||(address!=0x5a&&address!=0x5b)||(!to&&!from))return EldenHapticsBusArgument;
    if(reg>0x80||((reg!=0x42&&reg!=0x32)&&bytes>0x81u-reg))return EldenHapticsBusArgument;
    if(b->held)return EldenHapticsBusHeld;
    if(b->transferring||elden_haptics_geni_busy(&b->core))return EldenHapticsBusBusy;
    int rc=idle(b);if(rc)return rc;
    b->transferring=1;b->core.address=address;b->error=0;b->cleanup=0;
    uint8_t received[16];
    for(size_t offset=0;offset<bytes;){
        const size_t limit=to?4:63,count=bytes-offset<limit?bytes-offset:limit;
        uint8_t header[64];header[0]=(reg==0x42||reg==0x32)?reg:(uint8_t)(reg+offset);
        if(from){for(unsigned i=0;i<count;++i)header[i+1]=from[offset+i];rc=elden_haptics_geni_start_write_bytes(&b->core,header,(uint32_t)count+1);}
        else rc=elden_haptics_geni_start_read_bytes(&b->core,header,1,(uint32_t)count);
        if(rc){rc=EldenHapticsBusBusy;break;}
        rc=wait_transfer(b);if(rc)break;
        if(to)for(unsigned i=0;i<count;++i)received[offset+i]=(uint8_t)(b->core.rx>>(8*i));
        ++b->split_transfers;offset+=count;
    }
    b->transferring=0;b->cleanup=0;
    if(!rc){if(to)for(size_t i=0;i<bytes;++i)to[i]=received[i];++b->transfers;}return rc;
}
int elden_haptics_bus_read(EldenHapticsBus *b,uint8_t address,uint8_t reg,uint8_t *out,size_t bytes){
    if(!out)return EldenHapticsBusArgument;
    return transaction(b,address,reg,out,0,bytes);
}
int elden_haptics_bus_write(EldenHapticsBus *b,uint8_t address,uint8_t reg,const uint8_t *in,size_t bytes){
    if(!in)return EldenHapticsBusArgument;
    return transaction(b,address,reg,0,in,bytes);
}
int elden_haptics_bus_release(EldenHapticsBus *b){
    if(!b||!b->acquired)return EldenHapticsBusArgument;
    if(b->held||b->transferring||elden_haptics_geni_busy(&b->core))return EldenHapticsBusHeld;
    b->cleanup=1;int rc=idle(b);if(rc)goto failed;
    /* Restore the external interrupt output last, after all engine metadata. */
    for(unsigned i=0;i<sizeof(configured)/sizeof(*configured);++i){
        if(configured[i]==GENI_SE_IRQ_EN)continue;
        rc=write_reg(b,configured[i],b->saved[i]);if(rc)goto failed;
        uint32_t value;rc=read_reg(b,configured[i],&value);if(rc||value!=b->saved[i]){if(!rc)rc=EldenHapticsBusError;goto failed;}
    }
    rc=write_reg(b,GENI_SE_IRQ_EN,b->saved[8]);if(rc)goto failed;
    uint32_t restored;rc=read_reg(b,GENI_SE_IRQ_EN,&restored);if(rc||restored!=b->saved[8]){if(!rc)rc=EldenHapticsBusError;goto failed;}
    b->cleanup=0;b->acquired=0;return 0;
failed:
    b->cleanup=0;b->held=1;return rc;
}
