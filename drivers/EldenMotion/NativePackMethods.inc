/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Real vnode/SHA/cache methods. Tests model VFS/ownership, use actual SHA256
 * and the actual source-derived pack parser. Neither is hardware acceptance. */
struct EldenSensorPackCache {
    EldenSensorPackSeal seal;EldenSensorPackAuthority authority;
    IOLock *lock;uint8_t *bytes;uint32_t state,reads,stopping;int error;
};
namespace {
enum {PackBound=1,PackReady=2,PackFailed=3,PackStopped=4,PackChunk=65536};
const char pack_path[]="/private/var/ngpu/sensors/pack.bin";
struct PackFileIdentity {uint64_t size,fileid;uint32_t uid,gid,mode;};
static bool digest_nonzero(const uint8_t *b){unsigned bits=0;for(unsigned i=0;i<32;i++)bits|=b[i];return bits!=0;}
static bool equal_digest(const uint8_t *a,const uint8_t *b){unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=a[i]^b[i];return diff==0;}
static bool owner(EldenSensorPackCache *c,uint64_t id,uint64_t epoch){return c&&id==c->authority.owner&&epoch==c->authority.epoch;}
static int pack_admission(EldenSensorPackCache *c,int cleanup){
    if(!cleanup&&__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -125;
    int rc=c->authority.admit(c->authority.context,c->authority.owner,c->authority.epoch,&c->seal.identity,cleanup);
    return rc<=0?rc:-13;
}
static int identity(vnode_t file,PackFileIdentity *out){
    if(!vnode_isreg(file))return -74;
    struct vnode_attr va;VATTR_INIT(&va);
    VATTR_WANTED(&va,va_data_size);VATTR_WANTED(&va,va_fileid);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_gid);VATTR_WANTED(&va,va_mode);
    if(vnode_getattr(file,&va,vfs_context_kernel())||!VATTR_IS_SUPPORTED(&va,va_data_size)||!VATTR_IS_SUPPORTED(&va,va_fileid)||
       !VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_gid)||!VATTR_IS_SUPPORTED(&va,va_mode))return -74;
    if(!va.va_data_size||va.va_data_size>0x4000000u||!va.va_fileid||va.va_uid||va.va_gid||(va.va_mode&07777)!=0444)return -74;
    *out={va.va_data_size,va.va_fileid,va.va_uid,va.va_gid,(uint32_t)va.va_mode};return 0;
}
static bool same_file(const PackFileIdentity &a,const PackFileIdentity &b){return a.size==b.size&&a.fileid==b.fileid&&a.uid==b.uid&&a.gid==b.gid&&a.mode==b.mode;}
static void hash_bytes(SHA256_CTX *h,const uint8_t *p,uint64_t bytes){
    for(uint64_t offset=0;offset<bytes;){uint32_t n=bytes-offset>PackChunk?PackChunk:(uint32_t)(bytes-offset);SHA256_Update(h,p+offset,n);offset+=n;}
}
static int check_library(const uint8_t *data,uint64_t size){
    if(!size||size>=192)return -74;uint32_t n=(uint32_t)size;if(data[n-1]=='\n')n--;if(!n)return -74;
    for(uint32_t at=0;at<n;){uint32_t start=at;while(at<n&&data[at]!=';'){if(data[at]<32||data[at]>=127) return -74;at++;}
        uint32_t length=at-start;char canonical[SSP_PATH];if(!length||elden_sensor_pack_path((const char *)data+start,length,canonical,sizeof canonical)!=(int)length)return -74;
        if(memcmp(canonical,data+start,length))return -74;
        bool known=false;const char *const roots[]={"/vendor/dsp","/vendor/lib/rfsa/adsp","/odm/lib/rfsa/adsp"};
        for(const char *root:roots){size_t count=strlen(root);if(length>=count&&!memcmp(data+start,root,count)&&(length==count||data[start+count]=='/'))known=true;}
        if(!known)return -74;if(at<n){at++;if(at==n)return -74;}
    }
    return 0;
}
static bool file_scope(const char *path){
    if(!strcmp(path,"/.elden/library_path"))return true;
    const char *const prefixes[]={"/vendor/etc/sensors/","/mnt/vendor/persist/sensors/","/sys/devices/soc0/","/vendor/dsp/","/vendor/lib/rfsa/adsp/","/odm/lib/rfsa/adsp/"};
    for(const char *prefix:prefixes)if(!strncmp(path,prefix,strlen(prefix)))return true;return false;
}
static int check_contents(EldenSensorPackCache *c){
    SspHeader header={};SspPack pack={};
    if(elden_sensor_pack_header(c->bytes,SSP_HEADER,&header)||header.total!=c->seal.bytes||
       elden_sensor_pack_open(&pack,&header,c->bytes+SSP_HEADER))return -74;
    SHA256_CTX hash;SHA256_Init(&hash);uint32_t table_bytes=elden_sensor_pack_table_bytes(&header);
    for(uint32_t offset=0;offset<table_bytes;){uint32_t n=table_bytes-offset>PackChunk?PackChunk:table_bytes-offset;SHA256_Update(&hash,c->bytes+SSP_HEADER+offset,n);offset+=n;}
    uint8_t digest[32];SHA256_Final(digest,&hash);
    if(!equal_digest(digest,c->seal.namespace_sha256))return -74;
    const char *const directories[]={"/vendor/etc/sensors","/mnt/vendor/persist/sensors"};
    for(const char *path:directories){
        int index=elden_sensor_pack_find(&pack,path);SspEntry e={};if(index<0)return -2;elden_sensor_pack_entry(&pack,(uint32_t)index,&e);
        if(!(e.flags&SSP_DIRECTORY))return -74;
    }
    const char *const required_files[]={"/vendor/etc/sensors/sns_reg_config","/.elden/library_path"};
    for(const char *path:required_files){
        int index=elden_sensor_pack_find(&pack,path);SspEntry e={};if(index<0)return -2;elden_sensor_pack_entry(&pack,(uint32_t)index,&e);
        if((e.flags&SSP_DIRECTORY)||!e.size)return -74;
        if(!strcmp(path,"/.elden/library_path")&&check_library(c->bytes+e.offset,e.size))return -74;
    }
    bool calibration=false;SHA256_CTX registry;SHA256_Init(&registry);
    for(uint32_t i=0;i<pack.count;i++){
        SspEntry e={};elden_sensor_pack_entry(&pack,i,&e);const char prefix[]="/mnt/vendor/persist/sensors/";
        if(!(e.flags&SSP_DIRECTORY)&&!file_scope(e.name))return -74;
        if(!(e.flags&SSP_DIRECTORY)&&!strncmp(e.name,prefix,sizeof(prefix)-1)){
            if(e.size)calibration=true;uint8_t length[8];for(unsigned j=0;j<8;j++)length[j]=(uint8_t)(e.size>>(8*j));
            SHA256_Update(&registry,e.name,strlen(e.name)+1);SHA256_Update(&registry,length,sizeof length);
            SHA256_CTX content;SHA256_Init(&content);hash_bytes(&content,c->bytes+e.offset,e.size);uint8_t file_digest[32];SHA256_Final(file_digest,&content);
            SHA256_Update(&registry,file_digest,sizeof file_digest);
        }
    }
    uint8_t registry_digest[32];SHA256_Final(registry_digest,&registry);
    if(!equal_digest(registry_digest,c->seal.identity.registry_sha256))return -74;return calibration?0:-2;
}
static int load(EldenSensorPackCache *c){
    if(c->error)return c->error;if(c->state==PackReady)return native_owner_data_status==0?pack_admission(c,0):-125;
    if(native_owner_data_status!=0)return 1;int rc=pack_admission(c,0);if(rc)return rc;
    vnode_t file=NULLVP;vfs_context_t context=vfs_context_kernel();
    rc=vnode_open(pack_path,FREAD|O_NOFOLLOW_ANY,0,0,&file,context);
    PackFileIdentity before={},after={};
    if(rc)rc=-2;
    if(!rc)rc=identity(file,&before);
    if(!rc&&before.size!=c->seal.bytes)rc=-74;
    if(!rc){c->bytes=(uint8_t *)IOMallocData((vm_size_t)before.size);if(!c->bytes)rc=-12;}
    SHA256_CTX hash;if(!rc)SHA256_Init(&hash);
    for(uint64_t offset=0;!rc&&offset<c->seal.bytes;){
        rc=pack_admission(c,0);if(rc)break;if(native_owner_data_status!=0){rc=-125;break;}
        uint32_t n=c->seal.bytes-offset>PackChunk?PackChunk:(uint32_t)(c->seal.bytes-offset);int resid=0;
        int error=vn_rdwr(UIO_READ,file,(caddr_t)(c->bytes+offset),(int)n,(off_t)offset,UIO_SYSSPACE,IO_NOAUTH,vfs_context_ucred(context),&resid,vfs_context_proc(context));
        if(error||resid){rc=-5;break;}
        if(c->reads==UINT32_MAX){rc=-75;break;}c->reads++;SHA256_Update(&hash,c->bytes+offset,n);offset+=n;
    }
    if(!rc){uint8_t digest[32];SHA256_Final(digest,&hash);if(!equal_digest(digest,c->seal.sha256))rc=-74;}
    if(!rc)rc=identity(file,&after);
    if(!rc&&!same_file(before,after))rc=-116;
    if(file){int closed=vnode_close(file,FREAD,context);if(closed&&!rc)rc=-5;}
    if(!rc)rc=check_contents(c);
    if(!rc)rc=pack_admission(c,0);
    if(!rc&&native_owner_data_status!=0)rc=-125;
    if(rc){c->error=rc;c->state=PackFailed;return rc;}
    c->state=PackReady;return 0;
}
}
extern "C" int elden_sensor_pack_native_bind(const EldenSensorPackSeal *seal,const EldenSensorPackAuthority *a,EldenSensorPackCache **out){
    if(!seal||!a||!out||*out||seal->version!=1||seal->bytes<SSP_HEADER||seal->bytes>0x4000000u||!a->owner||!a->epoch||!a->admit||ml_at_interrupt_context())return -22;
    const uint8_t *identity_digests[]={seal->identity.device_sha256,seal->identity.build_sha256,seal->identity.adsp_sha256,seal->identity.adsp_dtb_sha256,seal->identity.registry_sha256,seal->sha256,seal->namespace_sha256};
    for(const auto *digest:identity_digests)if(!digest_nonzero(digest))return -22;
    int rc=a->admit(a->context,a->owner,a->epoch,&seal->identity,0);if(rc)return rc<0?rc:-13;
    auto *c=(EldenSensorPackCache *)IOMallocData(sizeof(EldenSensorPackCache));if(!c)return -12;bzero(c,sizeof(*c));
    c->seal=*seal;c->authority=*a;c->lock=IOLockAlloc();if(!c->lock){IOFreeData(c,sizeof(*c));return -12;}
    c->state=PackBound;*out=c;return 0;
}
extern "C" int elden_sensor_pack_native_size(EldenSensorPackCache *c,uint64_t id,uint64_t epoch,uint64_t *out){
    if(!owner(c,id,epoch))return -116;if(!out||ml_at_interrupt_context())return -22;
    if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -125;
    IOLockLock(c->lock);int rc=load(c);if(!rc)rc=pack_admission(c,0);if(!rc)*out=c->seal.bytes;IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_pack_native_read(EldenSensorPackCache *c,uint64_t id,uint64_t epoch,uint64_t offset,void *out,uint32_t length){
    if(!owner(c,id,epoch))return -116;if(!out||!length||length>PackChunk||ml_at_interrupt_context())return -22;
    if(offset>c->seal.bytes||length>c->seal.bytes-offset)return -22;
    if(__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE))return -125;
    IOLockLock(c->lock);int rc=c->state==PackReady?pack_admission(c,0):c->error?c->error:-19;
    if(!rc&&native_owner_data_status!=0)rc=-125;
    if(!rc)memcpy(out,c->bytes+offset,length);IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_pack_native_stop(EldenSensorPackCache *c,uint64_t id,uint64_t epoch){
    if(!owner(c,id,epoch))return -116;__atomic_store_n(&c->stopping,1u,__ATOMIC_RELEASE);
    if(ml_at_interrupt_context()||!IOLockTryLock(c->lock))return -11;
    int rc=pack_admission(c,1);if(!rc)c->state=PackStopped;IOLockUnlock(c->lock);return rc;
}
extern "C" int elden_sensor_pack_native_status(EldenSensorPackCache *c,EldenSensorPackStatus *out){
    if(!c||!out||ml_at_interrupt_context())return -22;if(!IOLockTryLock(c->lock))return -11;
    EldenSensorPackStatus s={c->state,c->reads,__atomic_load_n(&c->stopping,__ATOMIC_ACQUIRE),c->error,c->state==PackReady?c->seal.bytes:0,c->authority.owner,c->authority.epoch};
    *out=s;IOLockUnlock(c->lock);return 0;
}
