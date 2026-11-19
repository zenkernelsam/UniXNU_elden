/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Executes NativePackMethods.inc with explicit VFS/owner peers. SHA256 and
 * source-derived pack parsing are real. Fixture data never enter production. */
#include "NativePack.h"
#include "RpcPack.h"
#include "PackBinding.h"
#include <CommonCrypto/CommonDigest.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};static unsigned scenarios;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using vm_size_t=size_t;using boolean_t=int;using IOLock=std::mutex;using SHA256_CTX=CC_SHA256_CTX;
static void SHA256_Init(SHA256_CTX *c){CC_SHA256_Init(c);}
static void SHA256_Update(SHA256_CTX *c,const void *p,size_t n){CHECK(n<=65536);CC_SHA256_Update(c,p,(CC_LONG)n);}
static void SHA256_Final(uint8_t *p,SHA256_CTX *c){CC_SHA256_Final(p,c);}
using vnode_t=void *;using vfs_context_t=void *;using caddr_t=char *;
static constexpr vnode_t NULLVP=nullptr;
enum {FREAD=1,O_NOFOLLOW_ANY=0x20000000,UIO_READ=0,UIO_SYSSPACE=1,IO_NOAUTH=2};
struct vnode_attr {uint64_t va_data_size,va_fileid;uint32_t va_uid,va_gid,va_mode;bool supported;};
#define VATTR_INIT(v) std::memset(v,0,sizeof(*(v)))
#define VATTR_WANTED(v,n) (void)(v)
#define VATTR_IS_SUPPORTED(v,n) ((v)->supported)
static std::vector<void *> allocated;static std::vector<IOLock *> locks;
static unsigned allocation_count,fail_alloc;
static void *IOMallocData(size_t n){if(++allocation_count==fail_alloc)return nullptr;void *p=std::malloc(n);if(p)allocated.push_back(p);return p;}
static void IOFreeData(void *p,size_t){for(void *&v:allocated)if(v==p)v=nullptr;std::free(p);}
static IOLock *IOLockAlloc(){if(++allocation_count==fail_alloc)return nullptr;auto *p=new IOLock;locks.push_back(p);return p;}
static void IOLockLock(IOLock *p){p->lock();}static void IOLockUnlock(IOLock *p){p->unlock();}static bool IOLockTryLock(IOLock *p){return p->try_lock();}
static int native_owner_data_status;static bool irq;static boolean_t ml_at_interrupt_context(){return irq;}
struct Peer {
    std::vector<uint8_t> file;EldenSensorPackSeal seal{};unsigned opens=0,closes=0,reads=0,attrs=0;
    int open_error=0,close_error=0,read_error=0,resid=0,authority_error=0,bad_metadata=0;
    bool regular=true,changed_metadata=false,corrupt=false;
    EldenSensorPackCache *cache=nullptr;
    EldenSensorPackBinding *binding=nullptr;uint64_t cookie=0,clock=1000;unsigned bus_sends=0,bus_withdraws=0,wakes=0,completed=0;
    bool bus_withdraw_fail=false;uint8_t *arena=nullptr;
};
static Peer *model;static int file_token,authority_token;
static std::atomic<bool> block_read{false},entered{false},release_read{false};
static vfs_context_t vfs_context_kernel(){return (void *)1;}
static void *vfs_context_ucred(vfs_context_t c){CHECK(c==(void *)1);return (void *)2;}
static void *vfs_context_proc(vfs_context_t c){CHECK(c==(void *)1);return (void *)3;}
static int vnode_open(const char *path,int flags,int mode,int a,vnode_t *out,vfs_context_t c){
    CHECK(!std::strcmp(path,"/private/var/ngpu/sensors/pack.bin")&&flags==(FREAD|O_NOFOLLOW_ANY)&&!mode&&!a&&c==(void *)1);
    model->opens++;if(model->open_error)return model->open_error;*out=&file_token;return 0;
}
static int vnode_isreg(vnode_t p){CHECK(p==&file_token);return model->regular;}
static int vnode_getattr(vnode_t p,vnode_attr *v,vfs_context_t c){
    CHECK(p==&file_token&&c==(void *)1);model->attrs++;
    *v={model->file.size(),44,0,0,0444,true};
    if(model->changed_metadata&&model->attrs>1)v->va_fileid++;
    if(model->bad_metadata==1)v->va_mode=0644;
    if(model->bad_metadata==2)v->va_uid=501;
    if(model->bad_metadata==3)v->supported=false;
    if(model->bad_metadata==4)v->va_data_size++;
    return 0;
}
static int vnode_close(vnode_t p,int flags,vfs_context_t c){CHECK(p==&file_token&&flags==FREAD&&c==(void *)1);model->closes++;return model->close_error;}
static int vn_rdwr(int operation,vnode_t p,char *out,int n,off_t off,int seg,int auth,void *cred,int *resid,void *proc){
    CHECK(operation==UIO_READ&&p==&file_token&&n>0&&n<=65536&&off>=0&&seg==UIO_SYSSPACE&&auth==IO_NOAUTH&&cred==(void *)2&&proc==(void *)3);
    if(block_read.load()){entered.store(true);while(!release_read.load())std::this_thread::yield();}
    if(model->read_error)return model->read_error;CHECK((uint64_t)off+(unsigned)n<=model->file.size());
    std::memcpy(out,model->file.data()+off,(unsigned)n);model->reads++;
    if(model->corrupt)out[0]^=1;*resid=model->resid;return 0;
}
#include "NativePackMethods.inc"
static void put32(std::vector<uint8_t> &b,size_t at,uint32_t v){CHECK(at+4<=b.size());for(unsigned i=0;i<4;i++)b[at+i]=(uint8_t)(v>>(i*8));}
static void put64(std::vector<uint8_t> &b,size_t at,uint64_t v){CHECK(at+8<=b.size());for(unsigned i=0;i<8;i++)b[at+i]=(uint8_t)(v>>(i*8));}
static void hash(const std::vector<uint8_t> &b,size_t at,size_t length,uint8_t *out){CC_SHA256(b.data()+at,(CC_LONG)length,out);}
static void pack(Peer &p){
    std::map<std::string,std::vector<uint8_t>> files{
        {"/.elden/library_path",{'/','v','e','n','d','o','r','/','d','s','p','\n'}},
        {"/vendor/etc/sensors/sns_reg_config",{'M','O','D','E','L'}},
        {"/mnt/vendor/persist/sensors/calibration",std::vector<uint8_t>(200000,0x5a)}};
    std::map<std::string,bool> names;names["/"]=true;
    for(const auto &f:files){names[f.first]=false;std::string parent=f.first;
        while(parent!="/"){auto slash=parent.rfind('/');parent=slash?parent.substr(0,slash):"/";names[parent]=true;}}
    size_t names_bytes=0;for(const auto &n:names)names_bytes+=n.first.size()+1;
    size_t header=32+24*names.size()+names_bytes,total=header;for(const auto &f:files)total+=f.second.size();
    p.file.assign(total,0);std::memcpy(p.file.data(),"SSCPACK1",8);put32(p.file,8,(uint32_t)names.size());put32(p.file,12,(uint32_t)names_bytes);put64(p.file,16,total);
    size_t index=0,name_at=0,data_at=header;
    for(const auto &entry:names){size_t slot=32+index++*24;put32(p.file,slot+16,(uint32_t)name_at);put32(p.file,slot+20,entry.second?1:0);
        std::memcpy(p.file.data()+32+24*names.size()+name_at,entry.first.c_str(),entry.first.size()+1);name_at+=entry.first.size()+1;
        if(!entry.second){const auto &b=files[entry.first];put64(p.file,slot,data_at);put64(p.file,slot+8,b.size());std::memcpy(p.file.data()+data_at,b.data(),b.size());data_at+=b.size();}}
    uint32_t sum=0;for(size_t i=32;i<header;i++)sum+=p.file[i];put32(p.file,24,sum);
    p.seal.version=1;p.seal.bytes=p.file.size();hash(p.file,0,p.file.size(),p.seal.sha256);hash(p.file,32,header-32,p.seal.namespace_sha256);
    std::memset(&p.seal.identity,1,sizeof(p.seal.identity));
    SHA256_CTX registry;SHA256_Init(&registry);
    for(const auto &f:files)if(f.first.rfind("/mnt/vendor/persist/sensors/",0)==0){
        SHA256_Update(&registry,f.first.c_str(),f.first.size()+1);uint8_t length[8];for(unsigned i=0;i<8;i++)length[i]=(uint8_t)(f.second.size()>>(8*i));
        SHA256_Update(&registry,length,8);uint8_t content[32];CC_SHA256(f.second.data(),(CC_LONG)f.second.size(),content);SHA256_Update(&registry,content,32);
    }
    SHA256_Final(p.seal.identity.registry_sha256,&registry);
}
static int authority(void *context,uint64_t owner,uint64_t epoch,const EldenSensorPackIdentity *id,int cleanup){
    CHECK(context==&authority_token&&owner==7&&epoch==9&&id&&(cleanup==0||cleanup==1));
    CHECK(!std::memcmp(id,&model->seal.identity,sizeof(*id)));return model->authority_error;
}
static void bind(Peer &p){model=&p;pack(p);EldenSensorPackAuthority a{&authority_token,7,9,authority};CHECK(!elden_sensor_pack_native_bind(&p.seal,&a,&p.cache));}
static uint64_t get64(const uint8_t *p){uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static void wr64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static int bus_gate(void *p,uint64_t owner,uint64_t epoch,int cleanup){CHECK(p==model&&owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static uint64_t bus_now(void *p){return ((Peer *)p)->clock;}
static void bus_wait(void *p,uint32_t ms){((Peer *)p)->clock+=ms;}
static void bus_wake(void *p){((Peer *)p)->wakes++;}
static void *bus_alloc(void *p,uint32_t n){CHECK(p==model);return IOMallocData(n);}
static int bus_prepare(void *p,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){CHECK(p==model&&owner==7&&epoch==9&&m->session==4&&m->iova==0x20000000&&m->arena==model->arena);return 0;}
static int bus_publish(void *p,uint64_t owner,uint64_t epoch){CHECK(p==model&&owner==7&&epoch==9);return 0;}
static int bus_reserve(void *p,uint64_t owner,uint64_t epoch,uint64_t *out){CHECK(p==model&&owner==7&&epoch==9);model->cookie++;*out=model->cookie<<4|2;return 0;}
static int bus_complete(void *p,uint64_t owner,uint64_t epoch,uint64_t cookie){CHECK(p==model&&owner==7&&epoch==9&&cookie==(model->cookie<<4|2));model->completed++;return 0;}
static int bus_send(void *p,uint64_t owner,uint64_t epoch,const uint8_t *data,uint32_t n){
    CHECK(p==model&&owner==7&&epoch==9&&n==40);model->bus_sends++;
    CHECK(get64(data+8)==(model->cookie<<4|2));
    uint32_t handle=0,sc=0;std::memcpy(&handle,data+16,4);std::memcpy(&sc,data+20,4);
    if(handle==0&&(sc>>24&31)==0){uint32_t value=0x77;std::memcpy(model->arena+0x800,&value,4);value=0;std::memcpy(model->arena+0x804,&value,4);}
    uint8_t response[16]={0};wr64(response,get64(data+8));CHECK(!elden_sensor_pack_binding_receive(model->binding,7,9,response,sizeof(response)));return 0;
}
static int bus_withdraw(void *p,uint64_t owner,uint64_t epoch,const EldenSensorRpcMemory *m){CHECK(p==model&&owner==7&&epoch==9&&m->arena==model->arena);if(model->bus_withdraw_fail)return -16;model->bus_withdraws++;return 0;}
static void bus_trace(void *p,const char *s){CHECK(p==model&&s);}
static void binding(Peer &p){
    model=&p;pack(p);p.arena=(uint8_t *)IOMallocData(0x800000);CHECK(p.arena);std::memset(p.arena,0,0x800000);
    EldenSensorRpcBus bus{&p,7,9,bus_gate,bus_now,bus_send,bus_wait,bus_publish,bus_prepare,bus_reserve,bus_complete,bus_alloc,bus_trace,bus_withdraw,bus_wake};
    EldenSensorRpcMemory memory{p.arena,0x20000000,0x800000,4,4242};EldenSensorPackAuthority a{&authority_token,7,9,authority};
    CHECK(!elden_sensor_pack_binding_open(&bus,&memory,&p.seal,&a,&p.binding));CHECK(!elden_sensor_pack_binding_channel(p.binding,7,9,1));
}
static void reset(){for(auto *p:locks)delete p;locks.clear();for(void *p:allocated)std::free(p);allocated.clear();allocation_count=fail_alloc=0;native_owner_data_status=0;irq=false;block_read=false;entered=false;release_read=false;scenarios++;}
static EldenSensorPackStatus status(Peer &p){EldenSensorPackStatus s{};CHECK(!elden_sensor_pack_native_status(p.cache,&s));return s;}
int main(int argc,char **argv){
    const bool imported=argc>1;std::vector<uint8_t> imported_pack;EldenSensorPackSeal imported_seal{};
    if(imported){CHECK(argc==3);FILE *f=std::fopen(argv[1],"rb");CHECK(f);CHECK(!std::fseek(f,0,SEEK_END));long n=std::ftell(f);CHECK(n>0);imported_pack.resize((size_t)n);std::rewind(f);CHECK(std::fread(imported_pack.data(),1,imported_pack.size(),f)==imported_pack.size());std::fclose(f);
        f=std::fopen(argv[2],"rb");CHECK(f);CHECK(std::fread(&imported_seal,1,sizeof(imported_seal),f)==sizeof(imported_seal));std::fclose(f);}
    reset();Peer p;bind(p);uint64_t size=UINT64_MAX;CHECK(!elden_sensor_pack_native_size(p.cache,7,9,&size)&&size==p.file.size());CHECK(p.opens==1&&p.closes==1&&p.reads==4);
    uint8_t bytes[16];std::memset(bytes,0x33,sizeof(bytes));CHECK(!elden_sensor_pack_native_read(p.cache,7,9,0,bytes,8)&&!std::memcmp(bytes,"SSCPACK1",8));
    CHECK(!elden_sensor_pack_native_size(p.cache,7,9,&size)&&p.opens==1&&p.reads==4);CHECK(status(p).state==PackReady);
    CHECK(elden_sensor_pack_native_read(p.cache,7,10,0,bytes,8)==-116);CHECK(elden_sensor_pack_native_read(p.cache,7,9,p.file.size(),bytes,1)==-22);
    CHECK(!elden_sensor_pack_native_stop(p.cache,7,9)&&status(p).state==PackStopped);CHECK(elden_sensor_pack_native_read(p.cache,7,9,0,bytes,8)==-125);
    reset();Peer pending;bind(pending);native_owner_data_status=1;size=UINT64_MAX;CHECK(elden_sensor_pack_native_size(pending.cache,7,9,&size)==1&&size==UINT64_MAX&&!pending.opens);native_owner_data_status=0;CHECK(!elden_sensor_pack_native_size(pending.cache,7,9,&size));
    for(int bad=1;bad<=4;bad++){reset();Peer m;bind(m);m.bad_metadata=bad;size=0;CHECK(elden_sensor_pack_native_size(m.cache,7,9,&size)==-74&&size==0&&m.opens==1&&m.closes==1&&m.reads==0);CHECK(status(m).state==PackFailed);}
    reset();Peer changed;bind(changed);changed.changed_metadata=true;CHECK(elden_sensor_pack_native_size(changed.cache,7,9,&size)==-116&&changed.cache->bytes);CHECK(status(changed).state==PackFailed);
    reset();Peer corrupt;bind(corrupt);corrupt.corrupt=true;CHECK(elden_sensor_pack_native_size(corrupt.cache,7,9,&size)==-74);unsigned reads=corrupt.reads;CHECK(elden_sensor_pack_native_size(corrupt.cache,7,9,&size)==-74&&corrupt.reads==reads&&corrupt.opens==1);
    reset();Peer namespace_bad;bind(namespace_bad);namespace_bad.cache->seal.namespace_sha256[1]^=1;CHECK(elden_sensor_pack_native_size(namespace_bad.cache,7,9,&size)==-74);
    reset();Peer registry_bad;bind(registry_bad);registry_bad.cache->seal.identity.registry_sha256[2]^=1;registry_bad.seal.identity.registry_sha256[2]^=1;
    CHECK(elden_sensor_pack_native_size(registry_bad.cache,7,9,&size)==-74);
    reset();Peer io;bind(io);io.resid=1;CHECK(elden_sensor_pack_native_size(io.cache,7,9,&size)==-5&&io.cache->bytes);CHECK(status(io).state==PackFailed);
    reset();Peer nofile;bind(nofile);nofile.open_error=2;CHECK(elden_sensor_pack_native_size(nofile.cache,7,9,&size)==-2&&!nofile.closes&&!nofile.reads);
    reset();Peer close;bind(close);close.close_error=5;CHECK(elden_sensor_pack_native_size(close.cache,7,9,&size)==-5&&close.cache->bytes);
    reset();Peer allocation;bind(allocation);fail_alloc=allocation_count+1;CHECK(elden_sensor_pack_native_size(allocation.cache,7,9,&size)==-12);
    reset();Peer stopped;bind(stopped);block_read=true;int load_result=0;std::thread loader([&]{uint64_t n;load_result=elden_sensor_pack_native_size(stopped.cache,7,9,&n);});
    while(!entered.load())std::this_thread::yield();CHECK(elden_sensor_pack_native_stop(stopped.cache,7,9)==-11);release_read=true;loader.join();CHECK(load_result==-125&&stopped.cache->bytes);CHECK(!elden_sensor_pack_native_stop(stopped.cache,7,9));
    reset();Peer loss;bind(loss);CHECK(!elden_sensor_pack_native_size(loss.cache,7,9,&size));native_owner_data_status=1;CHECK(elden_sensor_pack_native_size(loss.cache,7,9,&size)==-125);CHECK(elden_sensor_pack_native_read(loss.cache,7,9,0,bytes,8)==-125);
    reset();Peer wrong;model=&wrong;pack(wrong);EldenSensorPackAuthority a{&authority_token,7,9,authority};wrong.seal.sha256[0]=0;std::memset(wrong.seal.sha256,0,32);CHECK(elden_sensor_pack_native_bind(&wrong.seal,&a,&wrong.cache)==-22&&!wrong.cache);pack(wrong);wrong.authority_error=-13;CHECK(elden_sensor_pack_native_bind(&wrong.seal,&a,&wrong.cache)==-13&&!wrong.cache);
    reset();Peer composed;binding(composed);CHECK(elden_sensor_pack_binding_step(composed.binding)==1);CHECK(elden_sensor_pack_binding_step(composed.binding)==1);
    CHECK(composed.opens==1&&composed.reads==4);EldenSensorRpcStatus rpc{};EldenSensorPackStatus cache_status{};
    CHECK(!elden_sensor_pack_binding_status(composed.binding,&rpc,&cache_status)&&rpc.stage==2&&cache_status.state==PackReady);
    for(unsigned i=0;i<5;i++)CHECK(elden_sensor_pack_binding_step(composed.binding)==1);
    CHECK(composed.bus_sends==5&&composed.completed==5);composed.bus_withdraw_fail=true;
    CHECK(elden_sensor_pack_binding_finish(composed.binding,7,9)==-16&&composed.bus_withdraws==0);
    composed.bus_withdraw_fail=false;CHECK(!elden_sensor_pack_binding_finish(composed.binding,7,9)&&composed.bus_withdraws==1&&composed.wakes>0);
    CHECK(!elden_sensor_pack_binding_status(composed.binding,&rpc,&cache_status)&&rpc.terminal&&cache_status.state==PackStopped);
    CHECK(!elden_sensor_pack_binding_finish(composed.binding,7,9)&&composed.bus_withdraws==1);
    if(imported){reset();Peer own;model=&own;own.file=imported_pack;own.seal=imported_seal;EldenSensorPackAuthority auth{&authority_token,7,9,authority};CHECK(!elden_sensor_pack_native_bind(&own.seal,&auth,&own.cache));CHECK(!elden_sensor_pack_native_size(own.cache,7,9,&size)&&size==imported_pack.size());CHECK(own.opens==1&&own.closes==1);}
    unsigned completed=scenarios;reset();std::printf("PASS %u actual native vnode/SHA256/pack/cache/stop scenarios, %u checks; VFS/authority peers modeled\n",completed,checks.load());return 0;
}
