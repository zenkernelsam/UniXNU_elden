/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual source-derived RpcCore.c is included for metadata/file-call tests.
 * Explicit transport/DMA/clock/files are modeled, no physical heap/profile. */
#include "RpcTestCheck.h"
#include "RpcPrivate.h"
#include "SensorRpc.h"
static void wr32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void wr64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
#include "RpcOriginalPackTests.inc"
#include "RpcCore.c"
typedef struct {
    SsrCore core;uint8_t *arena,*bounce;uint8_t pack_bytes[0x40000];uint32_t pack_size;
    uint64_t now,next_cookie,last_cookie;unsigned sent,prepared,completed,allocations;
    uint8_t *tables[8];uint8_t last_message[40];
    int live,pack_absent,reserve_fail,complete_fail,no_response,send_fail,stale_before;
    int open_fail,prepare_fail,metadata_verified,corrupt_table;
    int wrapper_withdraw_error;unsigned wrapper_withdrawn,wrapper_wakes;
} Model;
static unsigned scenarios;
static uint64_t m_now(void *v){return ((Model *)v)->now;}
static int m_live(void *v){return ((Model *)v)->live;}
static void m_wait(void *v,uint32_t ms){((Model *)v)->now+=ms;}
static void m_trace(void *v,const char *p){(void)v;CHECK(p);}
static int m_publish(void *v){CHECK(((Model *)v)->live);return 0;}
static int m_prepare(void *v){Model *m=v;m->prepared++;return m->prepare_fail;}
static int m_pack_size(void *v,uint64_t *size){Model *m=v;if(m->pack_absent)return SsrPackAbsent;*size=m->pack_size;return 0;}
static int m_pack_read(void *v,uint64_t off,void *out,uint32_t n){Model *m=v;if(off>m->pack_size||n>m->pack_size-off)return -1;memcpy(out,m->pack_bytes+off,n);return 0;}
static void *m_alloc(void *v,uint32_t n){Model *m=v;CHECK(m->allocations<8);uint8_t *p=malloc(n);CHECK(p);m->tables[m->allocations++]=p;return p;}
static int m_reserve(void *v,uint64_t *out){Model *m=v;if(m->reserve_fail)return -1;if(m->next_cookie==UINT64_MAX)return -1;m->next_cookie++;*out=m->next_cookie<<4|2;m->last_cookie=*out;return 0;}
static int m_complete(void *v,uint64_t cookie){Model *m=v;CHECK(cookie==m->last_cookie);if(m->complete_fail)return -1;m->completed++;return 0;}
static void respond(Model *m,uint64_t ctx,int value){uint8_t reply[16]={0};wr64(reply,ctx);wr32(reply+8,(uint32_t)value);elden_sensor_rpc_core_receive(&m->core,reply,sizeof(reply));}
static int m_send(void *v,const uint8_t *message,uint32_t n){
    Model *m=v;CHECK(n==40&&m->live);m->sent++;memcpy(m->last_message,message,40);if(m->send_fail)return -1;
    uint64_t cookie=get64(message+8);uint32_t handle=get32(message+16),sc=get32(message+20),method=sc>>24&31;
    CHECK(cookie==m->last_cookie&&(cookie&15)==2&&get32(message+4)==4242);
    if(m->stale_before)respond(m,cookie+16,-1);
    if(m->no_response)return 0;
    if(handle==0&&method==0){wr32(m->arena+0x800,0x77);wr32(m->arena+0x804,(uint32_t)m->open_fail);}
    if(handle==1&&method==0){
        CHECK(get64(message+24)==UINT64_C(0x420000000)&&get64(message+32)==4096);
        CHECK(get64(m->arena)==0x20000400&&get64(m->arena+8)==4&&get32(m->arena+16)==1);
        CHECK(get64(m->arena+24)==UINT64_C(0x420000000)&&get64(m->arena+32)==4096);m->metadata_verified=1;
    }
    respond(m,cookie,0);return 0;
}
static void init(Model *m){
    memset(m,0,sizeof(*m));m->arena=malloc(0x800000);m->bounce=malloc(SSR_BOUNCE);CHECK(m->arena&&m->bounce);
    memset(m->arena,0x5a,0x800000);m->live=1;m->now=1000;
    static const Item items[]={
        {"/",0,0},{"/.elden",0,0},{"/.elden/library_path","/vendor/dsp/sensors;/vendor/lib/rfsa/adsp\n",0},
        {"/mnt",0,0},{"/mnt/vendor",0,0},{"/mnt/vendor/persist",0,0},{"/mnt/vendor/persist/sensors",0,0},
        {"/mnt/vendor/persist/sensors/registry",0,0},{"/mnt/vendor/persist/sensors/registry/registry",0,0},
        {"/mnt/vendor/persist/sensors/registry/registry/calibration","MODEL_CALIBRATION",0},
        {"/vendor",0,0},{"/vendor/etc",0,0},{"/vendor/etc/sensors",0,0},{"/vendor/etc/sensors/sns_reg_config","MODEL_CONFIG",0}
    };
    m->pack_size=build_pack(m->pack_bytes,sizeof(m->pack_bytes),items,sizeof(items)/sizeof(items[0]));
    SsrOps ops={m,m_now,m_send,m_wait,m_trace,m_publish,m_prepare,m_pack_size,m_pack_read,m_alloc,m_live,m_reserve,m_complete};
    // ops lifetime is copied into retained model storage, not stack borrowed.
    static SsrOps saved[32];static unsigned used;CHECK(used<32);saved[used]=ops;
    SsrConfig config={m->arena,0x20000000,0x800000,m->bounce,4242,4,1};elden_sensor_rpc_core_init(&m->core,&saved[used++],&config);
    elden_sensor_rpc_core_channel(&m->core,1);
}
static void destroy(Model *m){for(unsigned i=0;i<m->allocations;i++)free(m->tables[i]);free(m->arena);free(m->bounce);}
static void load(Model *m){CHECK(elden_sensor_rpc_core_step(&m->core)==1);CHECK(elden_sensor_rpc_core_step(&m->core)==1);CHECK(m->core.status.stage==SsrStPack&&m->core.status.error==0&&m->sent==0);}
static int wrapper_gate(void *v,uint64_t owner,uint64_t epoch,int cleanup){CHECK(v&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int wrapper_send(void *v,uint64_t owner,uint64_t epoch,const uint8_t *p,uint32_t n){CHECK(owner==7&&epoch==9);return m_send(v,p,n);}
static int wrapper_publish(void *v,uint64_t owner,uint64_t epoch){CHECK(owner==7&&epoch==9);return m_publish(v);}
static int wrapper_prepare(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){CHECK(owner==7&&epoch==9&&m->session==4&&m->iova==0x20000000);return m_prepare(v);}
static int wrapper_reserve(void *v,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(owner==7&&epoch==9);return m_reserve(v,out);}
static int wrapper_complete(void *v,uint64_t owner,uint64_t epoch,uint64_t ctx){CHECK(owner==7&&epoch==9);return m_complete(v,ctx);}
static int wrapper_size(void *v,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(owner==7&&epoch==9);return m_pack_size(v,out);}
static int wrapper_read(void *v,uint64_t owner,uint64_t epoch,uint64_t off,void *out,uint32_t n){CHECK(owner==7&&epoch==9);return m_pack_read(v,off,out,n);}
static int wrapper_withdraw(void *v,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *memory){Model *m=v;CHECK(owner==7&&epoch==9&&memory->arena==m->arena);if(m->wrapper_withdraw_error)return m->wrapper_withdraw_error;m->wrapper_withdrawn++;return 0;}
static void wrapper_wake(void *v){((Model *)v)->wrapper_wakes++;}
int main(void){
    paths();pack_tables();
    {
        scenarios++;Model m;init(&m);load(&m);CHECK(elden_sensor_rpc_core_step(&m.core)==1&&m.core.status.stage==SsrStAttach&&m.metadata_verified);
        for(unsigned i=0;i<4;i++)CHECK(elden_sensor_rpc_core_step(&m.core)==1);
        CHECK(m.core.phase==PhaseServe&&m.core.status.stage==SsrStListen&&m.sent==5&&m.completed==5&&m.prepared==1);
        uint32_t fd=0;char path[]="/vendor/etc/sensors/sns_reg_config",mode[]="rb";Buf name={(uint8_t *)path,sizeof(path)},open_mode={(uint8_t *)mode,sizeof(mode)};
        CHECK(std_open(&m.core,&name,&open_mode,&fd)==0&&fd>=16);
        uint8_t primitive[8]={0},out[32]={0},result[8]={0};wr32(primitive,fd);wr32(primitive+4,32);
        Call request={0};request.in_count=1;request.out_count=2;request.in[0]=(Buf){primitive,8};request.out[0]=(Buf){result,8};request.out[1]=(Buf){out,32};
        CHECK(std_read(&m.core,&request)==0&&get32(result)==12&&!memcmp(out,"MODEL_CONFIG",12));
        CHECK(std_seek(&m.core,&request)==SSR_EBADPARM);m.live=0;CHECK(!elden_sensor_rpc_core_step(&m.core));CHECK(m.core.phase==PhaseStopped&&m.core.status.error==-125);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);m.pack_absent=1;CHECK(elden_sensor_rpc_core_step(&m.core)==1);CHECK(!elden_sensor_rpc_core_step(&m.core));CHECK(m.sent==0&&m.prepared==0&&m.core.status.stage==-SsrStPack);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);m.pack_bytes[40]^=1;CHECK(elden_sensor_rpc_core_step(&m.core)==1);CHECK(!elden_sensor_rpc_core_step(&m.core));CHECK(m.sent==0);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);m.prepare_fail=-16;CHECK(!elden_sensor_rpc_core_step(&m.core)&&m.sent==0);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);m.no_response=1;CHECK(!elden_sensor_rpc_core_step(&m.core));CHECK(m.core.phase==PhaseStopped&&m.core.status.error==-110&&m.sent==1&&m.completed==0&&m.core.heap_used==0);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);m.stale_before=1;CHECK(elden_sensor_rpc_core_step(&m.core)==1&&m.completed==1&&m.core.stray==1);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);m.complete_fail=1;CHECK(!elden_sensor_rpc_core_step(&m.core)&&m.sent==1&&m.completed==0);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);m.send_fail=1;CHECK(!elden_sensor_rpc_core_step(&m.core)&&m.sent==1&&m.completed==0);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);load(&m);CHECK(elden_sensor_rpc_core_step(&m.core)==1);elden_sensor_rpc_core_channel(&m.core,0);
        CHECK(!elden_sensor_rpc_core_step(&m.core)&&m.core.status.error==-116);CHECK(m.core.prepared&&m.core.cfg.arena==m.arena);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);for(unsigned i=0;i<9;i++)respond(&m,0x100+i*16,0);CHECK(m.core.reply_overflow==1);load(&m);CHECK(!elden_sensor_rpc_core_step(&m.core));destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);m.core.cfg.bank=9;m.core.ops=0;SsrConfig config={m.arena,UINT64_C(0xfffff000),0x800000,m.bounce,4242,9,0};
        elden_sensor_rpc_core_init(&m.core,0,&config);CHECK(!elden_sensor_rpc_core_step(&m.core)&&m.sent==0&&m.core.status.error==-22);destroy(&m);
    }
    {
        scenarios++;Model m;init(&m);uint8_t blob[16]={0};memcpy(m.arena+0x1000,blob,16);Call c;
        CHECK(unpack(&m.core,SC(0,1,1),12,&c)==SSR_EBADPARM);CHECK(unpack(&m.core,SC(0,1,1),8,&c)==0);
        uint32_t before=m.core.out_length;CHECK(call(&m.core,1,SC(5,0,0),0)==SSR_EUNSUPPORTED&&m.core.out_length==0);(void)before;
        CHECK(call(&m.core,1,SC(17,0,0),0)==SSR_EUNSUPPORTED);CHECK(call(&m.core,2,SC(3,0,0),0)==SSR_EUNSUPPORTED);destroy(&m);
    }
    {
        scenarios++;char out[256];CHECK(elden_sensor_pack_path("/abc\0evil",9,out,sizeof(out))==-1);CHECK(elden_sensor_pack_path("/a\\b",4,out,sizeof(out))==-1);
        CHECK(elden_sensor_pack_path("/vendor",7,out,sizeof(out))==7&&!strcmp(out,"/vendor"));
    }
    {
        scenarios++;Model m;init(&m);EldenSensorRpc *context=0;
        EldenSensorRpcProvider p={&m,7,9,wrapper_gate,m_now,wrapper_send,m_wait,wrapper_publish,wrapper_prepare,wrapper_reserve,wrapper_complete,wrapper_size,wrapper_read,m_alloc,m_trace,wrapper_withdraw,wrapper_wake};
        EldenSensorRpcMemory memory={m.arena,0x20000000,0x800000,4,4242};CHECK(!elden_sensor_rpc_open(&p,&memory,&context)&&context);
        CHECK(elden_sensor_rpc_channel(context,7,10,1)==-116);CHECK(!elden_sensor_rpc_channel(context,7,9,1));
        CHECK(elden_sensor_rpc_step(context)==1);CHECK(elden_sensor_rpc_step(context)==1);
        EldenSensorRpcStatus status;CHECK(!elden_sensor_rpc_status(context,&status)&&status.stage==SsrStPack);
        CHECK(!elden_sensor_rpc_request_stop(context,7,9)&&m.wrapper_wakes==1);CHECK(elden_sensor_rpc_step(context)==0);
        m.wrapper_withdraw_error=-16;CHECK(elden_sensor_rpc_finish(context,7,9)==-16);CHECK(!elden_sensor_rpc_status(context,&status)&&!status.terminal);
        m.wrapper_withdraw_error=0;CHECK(!elden_sensor_rpc_finish(context,7,9)&&m.wrapper_withdrawn==1);
        CHECK(!elden_sensor_rpc_finish(context,7,9)&&m.wrapper_withdrawn==1);CHECK(!elden_sensor_rpc_status(context,&status)&&status.terminal);
        CHECK(elden_sensor_rpc_channel(context,7,9,1)==-125);destroy(&m);
    }
    printf("PASS %u actual FastRPC/core/readonly registry/metadata/session scenarios plus original pack tests, %u checks; transport/DMA/clock/files modeled\n",scenarios,checks);return 0;
}
