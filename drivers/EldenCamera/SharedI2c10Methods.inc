/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included in original BacklightRuntime.cpp. Context and mappings stay to
 * reset after publication; no borrow can race disposal into freed storage. */
struct EldenCameraI2c10Port {
    EldenCameraI2c10Authority authority;void *bus;uint32_t closed;
};
namespace {
static Context *camera_shared_bus;static uint32_t camera_shared_port_attempted;
static EldenCameraI2c10Port *camera_shared_port;
static void shared_bus_publish(Context *c){c->shared_published=true;__atomic_store_n(&camera_shared_bus,c,__ATOMIC_RELEASE);}
static int shared_backlight_read(void *p,uint8_t reg,uint8_t *out){auto *c=(Context *)p;
    IOLockLock(c->serial);int rc=c->shared_closing?-125:elden_bl_platform_read(&c->platform,reg,out);IOLockUnlock(c->serial);return rc;}
static int shared_backlight_write(void *p,uint8_t reg,uint8_t value){auto *c=(Context *)p;
    IOLockLock(c->serial);int rc=c->shared_closing?-125:elden_bl_platform_write(&c->platform,reg,value);IOLockUnlock(c->serial);return rc;}
static int shared_bus_release_locked(Context *c){
    if(c->shared_released)return 0;
    // The camera power owner must close only after restoring all WL channels.
    // A live borrow prevents releasing shared bus clocks/engine underneath it.
    int rc=c->shared_camera_refs?-16:elden_bl_platform_release(&c->platform);
    if(!rc){c->shared_released=true;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}
    return rc;
}
static int shared_bus_release(Context *c){
    if(!c->serial)return elden_bl_platform_release(&c->platform);
    IOLockLock(c->serial);c->shared_closing=true;
    int rc=shared_bus_release_locked(c);
    IOLockUnlock(c->serial);return rc;
}
static int shared_camera_identity(EldenCameraI2c10Port *p,uint64_t owner,uint64_t epoch){return p&&p==__atomic_load_n(&camera_shared_port,__ATOMIC_ACQUIRE)&&p->authority.owner==owner&&p->authority.epoch==epoch;}
static int shared_camera_transfer(EldenCameraI2c10Port *p,uint64_t owner,uint64_t epoch,uint8_t reg,uint8_t *value,bool write,int cleanup){
    if(!shared_camera_identity(p,owner,epoch)||!value||(cleanup!=0&&cleanup!=1)||ml_at_interrupt_context())return -116;
    auto *c=(Context *)p->bus;if(!c||!c->serial)return -19;IOLockLock(c->serial);
    int rc=p->closed||(!cleanup&&c->shared_closing)?-125:p->authority.admit(p->authority.context,owner,epoch,cleanup);
    if(rc>0)rc=-13;
    if(!rc)rc=write?elden_bl_platform_i2c10_write(&c->platform,reg,*value):elden_bl_platform_i2c10_read(&c->platform,reg,value);
    IOLockUnlock(c->serial);return rc;
}
}
extern "C" int elden_camera_i2c10_open(const EldenCameraI2c10Authority *a,EldenCameraI2c10Port **out){
    if(!a||!out||*out||!a->owner||!a->epoch||!a->admit||!a->retired||ml_at_interrupt_context())return -22;
    int rc=a->admit(a->context,a->owner,a->epoch,0);if(rc)return rc<0?rc:-13;
    auto *c=__atomic_load_n(&camera_shared_bus,__ATOMIC_ACQUIRE);if(!c||!c->serial)return -19;
    IOLockLock(c->serial);
    if(c->shared_closing||c->shared_released||!c->platform.acquired||c->platform.failed||c->platform.quarantined){IOLockUnlock(c->serial);return -19;}
    rc=a->admit(a->context,a->owner,a->epoch,0);if(rc){IOLockUnlock(c->serial);return rc<0?rc:-13;}
    uint32_t none=0;if(!__atomic_compare_exchange_n(&camera_shared_port_attempted,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){IOLockUnlock(c->serial);return -16;}
    auto *p=(EldenCameraI2c10Port *)IOMalloc(sizeof(EldenCameraI2c10Port));if(!p){IOLockUnlock(c->serial);return -12;}
    *p={*a,c,0};__atomic_store_n(&camera_shared_port,p,__ATOMIC_RELEASE);c->shared_camera_refs=1;*out=p;IOLockUnlock(c->serial);return 0;
}
extern "C" int elden_camera_i2c10_read(EldenCameraI2c10Port *p,uint64_t owner,uint64_t epoch,uint8_t reg,uint8_t *out,int cleanup){return shared_camera_transfer(p,owner,epoch,reg,out,false,cleanup);}
extern "C" int elden_camera_i2c10_write(EldenCameraI2c10Port *p,uint64_t owner,uint64_t epoch,uint8_t reg,uint8_t value,int cleanup){return shared_camera_transfer(p,owner,epoch,reg,&value,true,cleanup);}
extern "C" int elden_camera_i2c10_close(EldenCameraI2c10Port *p,uint64_t owner,uint64_t epoch){
    if(!shared_camera_identity(p,owner,epoch)||ml_at_interrupt_context())return -116;auto *c=(Context *)p->bus;IOLockLock(c->serial);
    int rc=p->closed?0:p->authority.admit(p->authority.context,owner,epoch,1);if(rc>0)rc=-13;
    if(!rc&&!p->closed){rc=p->authority.retired(p->authority.context,owner,epoch);if(rc>0)rc=-13;}
    if(!rc&&!p->closed){p->closed=1;c->shared_camera_refs=0;}
    // Backlight may already have completed its tick/restore/worker cleanup.
    // Finish its deferred, real release exactly once after camera retirement;
    // a failed controller release retains the maps/lease and cannot reopen.
    if(!rc&&c->shared_closing)rc=shared_bus_release_locked(c);
    IOLockUnlock(c->serial);return rc;
}
