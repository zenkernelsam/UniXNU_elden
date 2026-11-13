/* SPDX-License-Identifier: GPL-2.0-only */
/* Reuse the independently packet-checking peripheral controller fixture.
 * This is host-only; neither test main nor model belongs in a kernel archive. */
#define main peripheral_fixture_main
#include "TestWcn7861Startup.c"
#undef main
#include "Wcn7861Hci.h"
typedef struct {
    Model peripheral; /* shared transport queue, not a fresh H4 stream */
    const uint8_t *bt,*nvm;size_t bt_bytes,nvm_bytes,bt_offset,nvm_offset;
    uint8_t address[6];
    unsigned fault,hci_writes,wakes,host_acks,resets,versions,fragment;
    bool active;
} HciModel;
static void cc(HciModel *m,uint16_t op,int sub,uint8_t status,const uint8_t *data,size_t size){
    uint8_t p[32]={4,0x0e,0,1,(uint8_t)op,(uint8_t)(op>>8),status};
    size_t n=7;
    if(sub>=0){p[n++]=(uint8_t)sub;}
    if(size){memcpy(p+n,data,size);n+=size;}CHECK(n<=sizeof(p));p[2]=(uint8_t)(n-3);
    if(m->fault==17&&op==0x1001){p[1]=0x0f;}
    if(m->fault==18&&sub==0x19){p[1]=0xff;}
    push(&m->peripheral,p,n);
}
static int hci_write(void *context,const uint8_t *p,size_t n,unsigned timeout){
    HciModel *m=context;
    if(!m->active)return write(context,p,n,timeout);
    CHECK(timeout==2000);m->hci_writes++;
    if(n==1){
        if(p[0]==0xfc){m->host_acks++;return 0;}
        CHECK(p[0]==0xfd);m->wakes++;
        if(m->fault==1&&m->wakes==1)return 0;
        uint8_t ack=0xfc;push(&m->peripheral,&ack,1);return 0;
    }
    CHECK(n>=4&&p[0]==1&&n==(size_t)p[3]+4);
    uint16_t op=p[1]|(uint16_t)p[2]<<8;
    if(op==0xfc00){
        CHECK(n>=5);
        if(p[4]==0x19){
            CHECK(n==5);m->versions++;
            uint16_t patch=m->resets?0x80ab:0x6d58; /* exact v113 pre-download reply */
            uint8_t d[13]={12,m->fault==12?0x1e:0x21,0,0,0,(uint8_t)patch,(uint8_t)(patch>>8),0,2,0,2,0x21,0x40};
            if(m->fault>=21&&m->fault<=23)d[0]=m->fault==21?0:m->fault==22?11:13;
            if(m->fault==2)d[12]^=1;
            if(m->fault==19&&m->resets)d[5]^=1;
            if(m->fault==16&&m->versions==1){uint8_t wake_byte=0xfd;push(&m->peripheral,&wake_byte,1);}
            cc(m,op,0x19,m->fault==24?1:0,d,sizeof(d));
            if(m->fault==15&&m->versions==1){uint8_t sleep_byte=0xfe;push(&m->peripheral,&sleep_byte,1);}
            return 0;
        }
        CHECK(p[4]==0x1e&&n>=7&&p[5]==n-6&&p[5]<=243);
        bool nv=m->bt_offset==m->bt_bytes;size_t at=nv?m->nvm_offset:m->bt_offset;
        size_t size=nv?m->nvm_bytes:m->bt_bytes;const uint8_t *original=nv?m->nvm:m->bt;
        CHECK(at+n-6<=size);
        for(size_t i=0;i<n-6;i++){
            size_t index=at+i;uint8_t expected=original[index];
            /* Offsets independently fixed from exact board NVM fixture. */
            if(nv&&index==168)expected=0x15;
            if(nv&&index==338)expected|=1;
            if(nv&&index>=92&&index<98)expected=m->address[index-92];
            CHECK(p[6+i]==expected);
        }
        if(nv&&m->fault==10)return -5;
        if(nv)m->nvm_offset+=n-6;else m->bt_offset+=n-6;
        bool final=at+n-6==size;
        if((nv||final)&&!(!nv&&(m->fault==3||m->fault==11)))
            cc(m,op,0x1e,nv&&m->fault==4?1:0,NULL,0);
        if(!nv&&final&&m->fault==5)cc(m,op,0x1e,0,NULL,0);
        if(!nv&&final&&m->fault==6){const uint8_t partial[]={4,0x0e,5};push(&m->peripheral,partial,sizeof(partial));}
        return 0;
    }
    CHECK(n==4);
    if(op==0x0c03){m->resets++;cc(m,op,-1,m->fault==7?1:0,NULL,0);}
    else if(op==0x1001){
        const uint8_t version[]={0x0e,0,0,0x0e,m->fault==8?0x0f:0x1d,0,0x21,0x7e};
        cc(m,op,-1,0,version,sizeof(version));
    }else if(op==0x1009){
        uint8_t address[6];memcpy(address,m->address,6);if(m->fault==9)address[0]^=1;
        cc(m,op,-1,0,address,sizeof(address));
    }else CHECK(false);
    return 0;
}
static size_t hci_read(void *context,uint8_t *out,size_t capacity){
    HciModel *m=context;
    if(m->active&&m->fault==20)return capacity+1;
    size_t n=m->peripheral.used-m->peripheral.pos;
    unsigned chunk=m->active?m->fragment:3;
    if(n>chunk)n=chunk;
    if(n>capacity)n=capacity;
    memcpy(out,m->peripheral.queue+m->peripheral.pos,n);m->peripheral.pos+=n;return n;
}
static void prepare_all(HciModel *m,EldenWcnStartup *wire,uint8_t *fw[5],size_t size[5]){
    EldenWcnStartupIo io={m,hci_write,hci_read,now,delay,flow,baud,NULL};
    for(unsigned i=0;i<3;i++){
        m->peripheral.fw[i]=fw[i];m->peripheral.size[i]=size[i];
        CHECK(!elden_wcn_peripheral_prepare((enum EldenWcnPeripheralFirmware)i,fw[i],size[i],&m->peripheral.plan[i]));
    }
    CHECK(!elden_wcn_startup_prepare(wire,&io,fw[0],size[0],fw[1],size[1],fw[2],size[2]));
    CHECK(!elden_wcn_startup_run(wire)&&wire->ready);
    m->bt=fw[3];m->bt_bytes=size[3];m->nvm=fw[4];m->nvm_bytes=size[4];
    const uint8_t address[]={1,2,3,4,5,0xc2};memcpy(m->address,address,6);
    m->fragment=m->fault==13?1:m->fault==14?512:3;m->active=true;
}
static void codec_tests(const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes){
    EldenWcnHciFirmware p={0};
    CHECK(!elden_wcn_hci_firmware_prepare(&p,patch,patch_bytes,nvm,nvm_bytes));
    CHECK(p.address_byte==92&&p.baud_byte==168&&p.ibs_byte==338&&p.patch_version==0x80ab);
    uint8_t *bad=malloc(nvm_bytes);CHECK(bad);memcpy(bad,nvm,nvm_bytes);
    /* Corrupt the final tag: full preflight must leave its output untouched. */
    bad[nvm_bytes-232+2]=255;bad[nvm_bytes-232+3]=255;
    EldenWcnHciFirmware unchanged;memset(&unchanged,0xa5,sizeof(unchanged));p=unchanged;
    CHECK(elden_wcn_hci_firmware_prepare(&p,patch,patch_bytes,bad,nvm_bytes)<0);
    CHECK(!memcmp(&p,&unchanged,sizeof(p)));
    free(bad);
    /* A six-byte address straddles a 243-byte wire boundary. */
    uint8_t synthetic[276]={2,0x10,1,0};
    synthetic[4]=0xf4;synthetic[5]=1;synthetic[6]=213;
    size_t at=229;synthetic[at]=2;synthetic[at+2]=6;
    for(unsigned i=0;i<6;i++)synthetic[at+12+i]=(uint8_t)(10+i);
    at+=18;synthetic[at]=17;synthetic[at+2]=3;synthetic[at+12]=0x15;synthetic[at+13]=0x11;
    at+=15;synthetic[at]=27;synthetic[at+2]=2;
    CHECK(at+14==sizeof(synthetic));
    CHECK(!elden_wcn_hci_firmware_prepare(&p,patch,patch_bytes,synthetic,sizeof(synthetic)));
    CHECK(p.address_byte==241);
    uint8_t expected[sizeof(synthetic)];memcpy(expected,synthetic,sizeof(expected));
    const uint8_t address[]={1,2,3,4,5,0xc2};memcpy(expected+241,address,6);
    expected[p.baud_byte]=0x15;expected[p.ibs_byte]|=1;
    uint8_t packet[249];size_t bytes;bool wait;
    for(size_t offset=0;offset<sizeof(synthetic);offset+=243){
        CHECK(!elden_wcn_hci_segment(&p,true,offset,address,packet,sizeof(packet),&bytes,&wait)&&wait);
        CHECK(!memcmp(packet+6,expected+offset,bytes-6));
    }
    memset(packet,0xa5,sizeof(packet));uint8_t old[249];memcpy(old,packet,sizeof(old));
    CHECK(elden_wcn_hci_segment(&p,true,0,address,packet,248,&bytes,&wait)<0);
    CHECK(!memcmp(packet,old,sizeof(old)));
    CHECK(elden_wcn_hci_segment(&p,true,0,address,synthetic+1,sizeof(synthetic)-1,&bytes,&wait)<0);
    CHECK(!memcmp(synthetic+241,(uint8_t[]){10,11,12,13,14,15},6));
    p.nvm_bytes=4;CHECK(elden_wcn_hci_segment(&p,true,0,address,packet,sizeof(packet),&bytes,&wait)<0);
}
#ifndef ELDEN_WCN_HCI_TEST_MAIN
#define ELDEN_WCN_HCI_TEST_MAIN main
#endif
int ELDEN_WCN_HCI_TEST_MAIN(int argc,char **argv){
    CHECK(argc==6);uint8_t *fw[5];size_t size[5];
    for(unsigned i=0;i<5;i++)fw[i]=load(argv[i+1],&size[i]);
    CHECK(size[3]==219296&&size[4]==13319);
    codec_tests(fw[3],size[3],fw[4],size[4]);
    unsigned scenarios=0;
    for(unsigned test=0;test<25;test++){
        HciModel m={0};EldenWcnStartup wire={0};EldenWcnHci hci={0};m.fault=test;scenarios++;
        prepare_all(&m,&wire,fw,size);
        CHECK(!elden_wcn_hci_prepare(&hci,&wire,fw[3],size[3],fw[4],size[4],m.address));
        CHECK(!m.hci_writes&&wire.hci_owned&&!wire.ready);
        if(test==11)m.peripheral.stuck_clock=true;
        int rc=elden_wcn_hci_run(&hci);
        bool success=test==0||test==12||test==13||test==14||test==15||test==16;
        if(success){
            CHECK(!rc&&hci.ready&&!hci.poisoned&&hci.stage==EldenWcnHciReady);
            CHECK(m.bt_offset==size[3]&&m.nvm_offset==size[4]);
            CHECK(m.resets==1&&m.versions==2&&hci.reported_patch==0x80ab);
            CHECK(hci.segments==(size[3]+242)/243+(size[4]+242)/243);
            CHECK(hci.acks==6+(size[4]+242)/243);
            CHECK(!memcmp(hci.local_address,m.address,6));
            if(test==15)CHECK(m.wakes==3);
            if(test==16)CHECK(m.host_acks==1);
        }else{
            CHECK(rc&&hci.poisoned&&wire.poisoned&&!hci.ready);
            unsigned writes=m.hci_writes;
            CHECK(elden_wcn_hci_run(&hci)<0&&m.hci_writes==writes);
            CHECK(elden_wcn_hci_prepare(&hci,&wire,fw[3],size[3],fw[4],size[4],m.address)<0&&m.hci_writes==writes);
            if(test==5||test==6)CHECK(!m.nvm_offset);
        }
    }
    /* Shared factory NVM address and a non-ready/incorrect-baud owner do not
     * gain wire authority. Validate before the first write. */
    for(unsigned test=0;test<3;test++){
        HciModel m={0};EldenWcnStartup wire={0};EldenWcnHci hci={0};
        prepare_all(&m,&wire,fw,size);
        if(test==0)wire.ready=false;
        if(test==1)wire.baud=4000000;
        if(test==2)memcpy(m.address,fw[4]+92,6);
        CHECK(elden_wcn_hci_prepare(&hci,&wire,fw[3],size[3],fw[4],size[4],m.address)<0);
        CHECK(!m.hci_writes&&!wire.hci_owned&&!hci.stage);
    }
    for(unsigned i=0;i<5;i++)free(fw[i]);
    printf("PASS %u HCI scenarios, %u five-firmware byte/transport/boundary assertions\n",scenarios,checks);
    return 0;
}
