/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual native MMIO/GIC + shared context/batch + retained original IOKit
 * allocator methods. Physical mappings/MMIO/GIC/PAR/authority are modeled. */
#include "SharedNative.h"
#include "Peripheral.h"
#include "Irqs.h"
#include "DmaRoute.h"
#include "EldenTouchBoard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"native shared line%u %s\n",__LINE__,#x);std::exit(1);}}while(0)
alignas(4096) static uint32_t regs[0x100000/4],gic[0x10000/4];
static uint64_t clock_us;static bool at_interrupt;static int early_closed,admit_error,irq_fail,visibility_error,bank_error;
static unsigned actual_writes,actual_reads,mapping_reads,global_attaches,context_attaches,visible,irq_masks,fail_read_at,mutate_at;
static unsigned registered_intids[1020];static void (*handlers[1020])(void *);static void *cookies[1020];
static std::vector<void *> allocations;static uint32_t armcpu_gic_ready=1;
using IOReturn=int;static bool ml_at_interrupt_context(){return at_interrupt;}
static void *IOMallocData(size_t bytes){void *p=std::calloc(1,bytes);if(p)allocations.push_back(p);return p;}
static void IODelay(unsigned us){clock_us+=us;}
static int shared_native_read32(uintptr_t base,size_t off,uint32_t *out){CHECK(out&&(base==(uintptr_t)regs||base==(uintptr_t)gic)&&!(off&3));actual_reads++;clock_us++;
    if(actual_reads==fail_read_at)return -5;if(mutate_at&&actual_reads==mutate_at)regs[0x81c/4]=0x80001234;
    size_t size=base==(uintptr_t)regs?sizeof(regs):sizeof(gic);CHECK(off<=size-4);*out=((uint32_t *)base)[off/4];return 0;}
static int shared_native_read64(uintptr_t base,size_t off,uint64_t *out){CHECK(out&&base==(uintptr_t)regs&&!(off&7)&&off<=sizeof(regs)-8);actual_reads++;clock_us++;
    if(actual_reads==fail_read_at)return -5;*out=(uint64_t)regs[off/4]|((uint64_t)regs[off/4+1]<<32);return 0;}
static int shared_native_write32(uintptr_t base,size_t off,uint32_t value){CHECK((base==(uintptr_t)regs||base==(uintptr_t)gic)&&!(off&3));actual_writes++;clock_us++;
    if(base==(uintptr_t)gic){CHECK(off>=0x280&&off<0x300);gic[(off-0x80)/4]&=~value;return 0;}
    CHECK(off<sizeof(regs)&&off!=0&&off!=0x80000&&off!=0x81c&&off!=0xc1c);
    if(off>=0x80000&&off%4096==0x58)regs[off/4]&=~value;else regs[off/4]=value;return 0;}
static int shared_native_write64(uintptr_t base,size_t off,uint64_t value){CHECK(base==(uintptr_t)regs&&off>=0x87000&&off<=sizeof(regs)-8&&!(off&7));actual_writes++;clock_us++;
    CHECK((off&4095)==0x20||(off&4095)==0x28);regs[off/4]=(uint32_t)value;regs[off/4+1]=(uint32_t)(value>>32);return 0;}
static void shared_native_barrier(){clock_us++;}
static uint64_t shared_native_now(){return ++clock_us;}
static uint64_t shared_native_affinity(){return 0x80010100u&UINT64_C(0xff00ffffff);}
static IOReturn armcpu_spi_attach(uint32_t id,bool edge,void(*handler)(void*),void *cookie,uint32_t *flags,uint64_t *router){
    CHECK(id>=32&&id<1020&&!edge&&handler&&cookie&&flags&&router);CHECK(!registered_intids[id]);
    if(irq_fail==1)return -19;registered_intids[id]=1;handlers[id]=handler;cookies[id]=cookie;
    id==97?global_attaches++:context_attaches++;gic[(0x100+(id/32)*4)/4]|=1u<<(id%32);
    *flags=irq_fail==2?0xa007:0xa003;*router=shared_native_affinity();return 0;
}
static IOReturn armcpu_spi_mask(uint32_t id,bool mask){CHECK(id<1020&&registered_intids[id]);irq_masks++;
    uint32_t *p=&gic[(0x100+(id/32)*4)/4];if(mask)*p&=~(1u<<(id%32));else *p|=1u<<(id%32);return 0;}
#include "SharedNativeMethods.inc"
extern "C" {
void elden_smmu_test_platform_reset(void);
int elden_camera_dma_peer_prepare(const EldenCameraDmaConfig *,const EldenCameraDmaOwner *,EldenCameraNativeDma **);
int elden_camera_dma_peer_backing(const EldenCameraDmaView *);void elden_camera_dma_peer_reset(void);
int elden_sensor_memory_peer_prepare(const EldenSensorMemoryConfig *,const EldenSensorMemoryOwner *,EldenSensorNativeMemory **);
int elden_sensor_memory_peer_backing(const EldenSensorMemoryView *);void elden_sensor_memory_peer_reset(void);
}
struct Physical {EldenCameraDmaView camera={};EldenSensorMemoryView sensor={};uint32_t idle[3]={1,1,1};EldenTouchDmaRouteOwner touch={};unsigned touch_held=0;};
static Physical physical;
static int authority_admit(void *p,uint64_t o,uint64_t e,int cleanup){CHECK(p==&physical&&o==7&&e==9&&(cleanup==0||cleanup==1));return cleanup?0:admit_error;}
static int authority_maps(void *p,uint64_t o,uint64_t e,const EldenSmmuNativeMapping *s,const EldenSmmuNativeMapping *g,int cleanup){CHECK(p==&physical&&o==7&&e==9&&cleanup<=1);mapping_reads++;
    return s->physical==0x15000000&&s->mapping==(uintptr_t)regs&&s->bytes==sizeof(regs)&&g->physical==0x16000000&&g->mapping==(uintptr_t)gic&&g->bytes==sizeof(gic)?0:-116;}
static int authority_early(void *p,uint64_t o,uint64_t e){CHECK(p==&physical&&o==7&&e==9);return early_closed?-16:0;}
static int authority_idle(void *p,uint64_t o,uint64_t e,unsigned group){CHECK(p==&physical&&o==7&&e==9&&group<3);return physical.idle[group]?0:-16;}
static int authority_memory(void *p,uint64_t o,uint64_t e,unsigned group,const EldenSmmuSharedMemory *m,int cleanup){CHECK(p==&physical&&o==7&&e==9&&m&&group<3&&cleanup<=1);
    return group==1?elden_camera_dma_peer_backing(&physical.camera):group==2?elden_sensor_memory_peer_backing(&physical.sensor):-13;}
static int authority_bank(void *p,uint64_t o,uint64_t e,unsigned group,const EldenSmmuSharedLease *l,uint32_t actlr,int cleanup){CHECK(p==&physical&&o==7&&e==9&&group<3&&l&&l->bank>=7&&l->asid&&l->route_epoch==9&&cleanup<=1);
    return bank_error?bank_error:actlr==0x52?0:-116;}
static int authority_touch(void *p,uint64_t o,uint64_t e,uint32_t off,uint32_t base,uint32_t *out){CHECK(p==&physical&&o==7&&e==9);return elden_touch_route_expected(&physical.touch,37,off,base,out);}
static int peripheral_admit(void *p,uint64_t o,uint64_t e,unsigned group,int cleanup){CHECK(group==1||group==2);return authority_admit(p,o,e,cleanup);}
static int camera_memory(void *p,const EldenCameraDmaView *view,int cleanup){CHECK(p==&physical&&view&&cleanup<=1);
    int rc=elden_camera_dma_peer_backing(view);if(!rc)physical.camera=*view;return rc;}
static int sensor_memory(void *p,const EldenSensorMemoryView *view,int cleanup){CHECK(p==&physical&&view&&cleanup<=1);
    int rc=elden_sensor_memory_peer_backing(view);if(!rc)physical.sensor=*view;return rc;}
static int camera_visible(void *p,const EldenCameraDmaView *view,const EldenCameraBuffer *b){CHECK(p==&physical&&view&&b&&b->mapping&&b->bytes);visible++;return visibility_error;}
static uint32_t touch_read(void *p,unsigned off){CHECK(p==&physical&&physical.touch_held&&off<sizeof(regs));return regs[off/4];}
static int touch_write(void *p,unsigned off,uint32_t value){CHECK(p==&physical&&physical.touch_held&&(off==0x81c||off==0xc1c));regs[off/4]=value;return 0;}
static int touch_claim(void *p,uint64_t epoch){CHECK(p==&physical&&epoch==37&&!physical.touch_held);physical.touch_held=1;return 0;}
static void touch_drop(void *p,uint64_t epoch){CHECK(p==&physical&&epoch==37&&physical.touch_held);physical.touch_held=0;}
static int touch_idle(void *p){CHECK(p==&physical&&physical.touch_held);return 0;}
static void setup(){elden_smmu_test_platform_reset();memset(regs,0,sizeof(regs));memset(gic,0,sizeof(gic));
    regs[0]=ELDEN_TOUCH_DMA_CR0;regs[0x20/4]=ELDEN_TOUCH_DMA_ID0;regs[0x24/4]=ELDEN_TOUCH_DMA_ID1;regs[0x28/4]=ELDEN_TOUCH_DMA_ID2;
    const uint32_t active[]=ELDEN_TOUCH_DMA_ACTIVE_SMR;
    for(unsigned i=0;i<132;i++){regs[(0x800+4*i)/4]=i<7?active[i]:0;regs[(0xc00+4*i)/4]=i<7?i:ELDEN_TOUCH_DMA_UNUSED_S2CR;}
    for(unsigned bank=0;bank<83;bank++){regs[(0x1000+4*bank)/4]=bank<7?0x1f000:0;regs[(0x1800+4*bank)/4]=bank<7?1:0;regs[(0x80000+4096*bank+4)/4]=0x52;}
    regs[0x80000/4]=0xe0;}
int main(int argc,char **argv){CHECK(argc==2);setup();const char *test=argv[1];
    EldenSmmuSharedConfig cfg={7,9,6,4,{{0,0},{0x20000000,0x8000000},{0x40001000,0x800000}}};
    EldenSmmuNativeAuthority owner={&physical,7,9,authority_admit,authority_maps,authority_early,authority_idle,authority_memory,authority_bank,authority_touch};
    EldenSmmuNativeMapping smmu={0x15000000,(uintptr_t)regs,sizeof(regs)},irq_map={0x16000000,(uintptr_t)gic,sizeof(gic)};
    EldenSmmuNative *native=nullptr;
    if(!strcmp(test,"early"))early_closed=1;if(!strcmp(test,"bank"))bank_error=-13;
    if(!strcmp(test,"irq"))irq_fail=1;if(!strcmp(test,"irq-flags"))irq_fail=2;if(!strcmp(test,"mapping"))smmu.physical++;
    int rc=elden_smmu_native_open(&cfg,&owner,&smmu,&irq_map,&native);
    if(early_closed||bank_error||irq_fail||strcmp(test,"mapping")==0){CHECK(rc<0&&!actual_writes);}
    else {
        CHECK(!rc&&native&&global_attaches==1&&context_attaches==2);EldenSmmuShared *shared=nullptr;CHECK(!elden_smmu_native_shared(native,7,9,&shared));
        EldenSmmuPeripheral adapter={};EldenSmmuPeripheralOps ops={&physical,peripheral_admit,camera_memory,camera_visible,sensor_memory};
        auto wrong=cfg;wrong.sensor_session++;CHECK(elden_smmu_peripheral_open(&adapter,shared,&wrong,&ops)==-116);
        wrong=cfg;wrong.apertures[2].iova+=4096;CHECK(elden_smmu_peripheral_open(&adapter,shared,&wrong,&ops)==-116);
        CHECK(!elden_smmu_peripheral_open(&adapter,shared,&cfg,&ops));EldenCameraDmaOwner camera_owner;EldenSensorMemoryOwner sensor_owner;
        CHECK(!elden_smmu_camera_owner(&adapter,&camera_owner));CHECK(!elden_smmu_sensor_owner(&adapter,&sensor_owner));
        EldenCameraDmaConfig camera_cfg={0x20000000,{2,2}};EldenSensorMemoryConfig sensor_cfg={0x40001000,0x800000,4,4242};
        EldenCameraNativeDma *camera=nullptr;EldenSensorNativeMemory *sensor=nullptr;
        CHECK(!elden_camera_dma_peer_prepare(&camera_cfg,&camera_owner,&camera));CHECK(!elden_sensor_memory_peer_prepare(&sensor_cfg,&sensor_owner,&sensor));
        CHECK(elden_camera_dma_admit(camera,7,9,0)==-19&&elden_sensor_memory_native_admit(sensor,7,9,0)==-19);
        CHECK(elden_smmu_peripheral_activate(&adapter,7,9)==-19);CHECK(elden_camera_dma_activate(camera,7,9)==-19);CHECK(elden_sensor_memory_native_activate(sensor,7,9)==-19);
        if(!strcmp(test,"foreign")){regs[0x81c/4]=0x80001234;unsigned before=actual_writes;CHECK(elden_smmu_shared_commit(shared,7,9)<0&&actual_writes==before);}
        else {
            CHECK(!elden_smmu_shared_commit(shared,7,9));CHECK(!elden_smmu_peripheral_activate(&adapter,7,9));
            CHECK(!elden_camera_dma_activate(camera,7,9));CHECK(!elden_sensor_memory_native_activate(sensor,7,9));
            EldenTouchRouteIo touch_io={&physical,touch_read,touch_write,touch_claim,touch_drop,touch_idle};
            CHECK(!elden_touch_route_attach(&physical.touch,&touch_io,37,0x180000000,0x180004000,16384));early_closed=1;
            EldenCameraDmaView view={};CHECK(!elden_camera_dma_view(camera,7,9,&view));CHECK(!elden_sensor_memory_native_publish(sensor,7,9));
            auto b=view.slots[0].buffer;CHECK(!elden_camera_dma_buffer(camera,7,9,0,&b,0));
            if(!strcmp(test,"visibility"))visibility_error=-71;
            rc=elden_camera_dma_buffer(camera,7,9,0,&b,1);CHECK(rc==(visibility_error?-71:0));CHECK(visible==1);
            if(!strcmp(test,"global-fault")){regs[0x48/4]=2;at_interrupt=true;handlers[97](cookies[97]);at_interrupt=false;
                CHECK(elden_sensor_memory_native_admit(sensor,7,9,0)<0);EldenSmmuNativeStatus status={};CHECK(!elden_smmu_native_status(native,&status)&&status.global_fault&&status.status[0]==2);
                CHECK(elden_smmu_shared_retire(shared,7,9,1)<0);}
            else if(!strcmp(test,"context-fault")){unsigned bank=native->leases[1].bank,id=elden_smmu_context_intids[bank];regs[(0x80000+4096*bank+0x58)/4]=0x20;
                at_interrupt=true;handlers[id](cookies[id]);at_interrupt=false;CHECK(elden_camera_dma_admit(camera,7,9,0)<0);
                CHECK(regs[(0x80000+4096*bank+0x58)/4]==0x20);}
            else if(!strcmp(test,"tlb")){unsigned bank=native->leases[1].bank;regs[(0x80000+4096*bank+0x7f4)/4]=1;
                CHECK(elden_camera_dma_stop(camera,7,9)<0);CHECK(!native->drained[0]);
                CHECK(!elden_sensor_memory_native_stop(sensor,7,9)&&!native->drained[0]);
                regs[(0x80000+4096*bank+0x7f4)/4]=0;CHECK(!elden_camera_dma_stop(camera,7,9));
                CHECK(!elden_sensor_memory_native_stop(sensor,7,9)&&native->drained[0]);}
            else {
                physical.idle[1]=0;unsigned before=actual_writes;CHECK(elden_camera_dma_stop(camera,7,9)==-16&&actual_writes==before);
                physical.idle[1]=1;CHECK(!elden_camera_dma_stop(camera,7,9));CHECK(!native->drained[0]&&native->drained[2]);
                CHECK(!elden_sensor_memory_native_stop(sensor,7,9));CHECK(native->drained[0]);CHECK(!elden_camera_dma_stop(camera,7,9));
                CHECK(!elden_sensor_memory_native_stop(sensor,7,9));CHECK(!elden_touch_route_observe(&physical.touch,37));CHECK(!elden_touch_route_detach(&physical.touch,37));
                CHECK(regs[0x80000/4]==0xe0&&regs[0x81c/4]==0&&regs[0xc1c/4]==0);
            }
        }
    }
    elden_camera_dma_peer_reset();elden_sensor_memory_peer_reset();for(void *p:allocations)std::free(p);
    std::printf("PASS shared-native %s: %u checks; actual allocator/batch/touch/MMIO/GIC, physical peers modeled\n",test,checks);return 0;
}
