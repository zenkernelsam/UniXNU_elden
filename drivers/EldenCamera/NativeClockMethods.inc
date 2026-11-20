/* SPDX-License-Identifier: GPL-2.0-only */
struct EldenCameraNativeClocks {
    EldenCameraNativeClockOwner owner;EldenCameraClocks clocks{};CameraClockWindow windows[3];
    IOLock *serial=nullptr;uint32_t prepared=0;int open_error=0;
};
namespace {
static EldenCameraNativeClocks *camera_clock_context;static uint32_t camera_clock_attempt;
static bool clock_identity(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch){return p&&p==__atomic_load_n(&camera_clock_context,__ATOMIC_ACQUIRE)&&p->owner.owner==owner&&p->owner.epoch==epoch;}
static int clock_admit(void *v,uint64_t owner,uint64_t epoch,unsigned op){auto *p=(EldenCameraNativeClocks *)v;
    if(!clock_identity(p,owner,epoch)||ml_at_interrupt_context()||op>2)return -116;int rc=p->owner.admit(p->owner.context,owner,epoch,op);return rc<=0?rc:-13;}
static int clock_lock(void *v,uint64_t owner,uint64_t epoch,int on){auto *p=(EldenCameraNativeClocks *)v;return p->owner.lock(p->owner.context,owner,epoch,on);}
static int clock_read(void *v,unsigned space,uint32_t reg,uint32_t *out){auto *p=(EldenCameraNativeClocks *)v;
    if(!elden_camera_clock_register_allowed(space,reg,0))return -13;return p->windows[space].read(reg,out);}
static int clock_write(void *v,unsigned space,uint32_t reg,uint32_t value){auto *p=(EldenCameraNativeClocks *)v;
    // Exact generated graph limits CAMCC/BIST RCG/branch/domain registers.
    // TLMM permits only these dedicated sensor/MCLK/CCI pins, not shared
    // bus8/9, global PMIC126, radio, display or GPIO interrupt registers.
    if(!elden_camera_clock_register_allowed(space,reg,1))return -13;
    return p->windows[space].write(reg,value);
}
static int clock_delay(void *,unsigned us){if(us>2000)return -22;IODelay(us);return 0;}
static int clock_controllers(void *v,uint64_t owner,uint64_t epoch,int action){auto *p=(EldenCameraNativeClocks *)v;return p->owner.controllers(p->owner.context,owner,epoch,action);}
static int clock_dependency(void *v,uint64_t owner,uint64_t epoch,const EldenCameraClockRequest *r,int action,uint32_t *hz){auto *p=(EldenCameraNativeClocks *)v;return p->owner.dependency(p->owner.context,owner,epoch,r,action,hz);}
static int clock_idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned clock){auto *p=(EldenCameraNativeClocks *)v;return p->owner.consumer_idle(p->owner.context,owner,epoch,sensor,clock);}
static int clock_sensor_idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){auto *p=(EldenCameraNativeClocks *)v;return p->owner.sensor_idle(p->owner.context,owner,epoch,sensor);}
static int clock_root_idle(void *v,uint64_t owner,uint64_t epoch,unsigned root){auto *p=(EldenCameraNativeClocks *)v;return p->owner.root_unused(p->owner.context,owner,epoch,root);}
static int clock_domain_idle(void *v,uint64_t owner,uint64_t epoch,unsigned domain){auto *p=(EldenCameraNativeClocks *)v;return p->owner.domain_unused(p->owner.context,owner,epoch,domain);}
static int clock_pins_idle(void *v,uint64_t owner,uint64_t epoch,unsigned sensor){auto *p=(EldenCameraNativeClocks *)v;return p->owner.pins_unused(p->owner.context,owner,epoch,sensor);}
static int clock_context_status(EldenCameraNativeClocks *p){return p&&p==__atomic_load_n(&camera_clock_context,__ATOMIC_ACQUIRE)&&p->serial&&p->prepared&&!ml_at_interrupt_context()?0:-19;}
}
extern "C" int elden_camera_native_clocks_open(const EldenCameraNativeClockOwner *owner,EldenCameraNativeClocks **out){
    if(!owner||!out||*out||!owner->owner||!owner->epoch||!owner->admit||!owner->lock||!owner->controllers||!owner->dependency||
       !owner->consumer_idle||!owner->sensor_idle||!owner->root_unused||!owner->domain_unused||!owner->pins_unused||ml_at_interrupt_context())return -22;
    int rc=owner->admit(owner->context,owner->owner,owner->epoch,EldenCamClockAcquire);if(rc)return rc<=0?rc:-13;
    uint32_t zero=0;if(!__atomic_compare_exchange_n(&camera_clock_attempt,&zero,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    auto *p=(EldenCameraNativeClocks *)IOMalloc(sizeof(EldenCameraNativeClocks));if(!p)return -12;
    *p={};p->owner=*owner;p->serial=IOLockAlloc();__atomic_store_n(&camera_clock_context,p,__ATOMIC_RELEASE);*out=p;
    if(!p->serial||!p->windows[0].map(0xade0000,0x20000)||!p->windows[1].map(0x1760000,0x6000)||!p->windows[2].map(0xf100000,0x78000))return p->open_error=-12;
    EldenCameraClockIo io={p,owner->owner,owner->epoch,clock_admit,clock_lock,clock_read,clock_write,clock_delay,camera_clock_now,
        clock_controllers,clock_dependency,clock_idle,clock_sensor_idle,clock_root_idle,clock_domain_idle,clock_pins_idle};
    IOLockLock(p->serial);rc=elden_camera_clocks_open(&p->clocks,&io);p->prepared=1;p->open_error=rc;IOLockUnlock(p->serial);return rc;
}
extern "C" int elden_camera_native_clock(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned clock,uint32_t hz,int on){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);
    rc=elden_camera_clock_resource(&p->clocks,owner,epoch,sensor,clock,hz,on);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_domain(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned domain,int on){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);
    rc=elden_camera_domain_resource(&p->clocks,owner,epoch,sensor,domain,on);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_pins_prepare(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);rc=elden_camera_pins_prepare(&p->clocks,owner,epoch);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_sensor_reset(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch,unsigned sensor,int asserted){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);rc=elden_camera_sensor_reset(&p->clocks,owner,epoch,sensor,asserted);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_clock_observe(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch,unsigned clock,uint32_t *hz){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);rc=elden_camera_clock_observe(&p->clocks,owner,epoch,clock,hz);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_clock_observe_cleanup(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch,unsigned clock,uint32_t *hz){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);
    rc=elden_camera_clock_observe_cleanup(&p->clocks,owner,epoch,clock,hz);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_clocks_status(EldenCameraNativeClocks *p,EldenCameraClockStatus *out){
    int rc=clock_context_status(p);if(rc)return rc;IOLockLock(p->serial);rc=elden_camera_clocks_status(&p->clocks,out);IOLockUnlock(p->serial);return rc;}
extern "C" int elden_camera_native_clocks_stop(EldenCameraNativeClocks *p,uint64_t owner,uint64_t epoch){
    int rc=clock_context_status(p);if(rc||!clock_identity(p,owner,epoch))return rc?rc:-116;IOLockLock(p->serial);rc=elden_camera_clocks_stop(&p->clocks);IOLockUnlock(p->serial);return rc;}
