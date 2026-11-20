"""Compose real shared WL power into the already qualified camera stack test."""
from pathlib import Path
import hashlib
HERE=Path(__file__).resolve().parent

def derive():
    source=(HERE/'TestCaptureStack.cpp').read_bytes()
    text=source.decode().replace('\r\n','\n');changes=[]
    def patch(old,new,why):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Actual capture test anchor changed: '+why)
        text=text.replace(old,new);changes.append(why)
    patch('#include "NativePipeline.h"','#include "NativePipeline.h"\n#include "PowerBinding.h"\n#include "Platform.h"\n#include "Aw99706.h"','original capture test adds actual powered binding')
    patch('void *elden_camera_cci_peer_create(unsigned,int);',
          'void *elden_camera_i2c10_peer_create(EldenBlPlatform *);unsigned elden_camera_i2c10_peer_stat(void *,unsigned);void elden_camera_i2c10_peer_destroy(void *);\nvoid *elden_camera_cci_peer_create(unsigned,int);','actual original GENI shared peer')
    patch('struct SensorState {unsigned sensor=0,rails=0,clock=0,reset=1;};',
          'struct SensorState {unsigned sensor=0,rails=0,clock=0,reset=1;};\nstatic SensorState *sensor_states;static EldenCameraPowerSession shared_power;static void *i2c_peer;',
          'same original power owner and real front/rear states')
    at='static int sensor_resource(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on)'
    patch(at,'''static int power_admit(void *,uint64_t owner,uint64_t epoch,int cleanup){CHECK(owner==7&&epoch==9&&(cleanup==0||cleanup==1));return 0;}
static int power_inactive(void *,uint64_t owner,uint64_t epoch,unsigned sensor){CHECK(owner==7&&epoch==9&&sensor<2);
    if(!sensor_states||!sensor_states[sensor].reset||sensor_states[sensor].clock||elden_camera_cci_peer_stat(cci_peers[sensor],2))return -16;
    EldenCameraPipelineStatus status{};int rc=elden_camera_native_pipeline_status(sensor,&status);if(rc)return rc;
    return status.state==EldenPipeReady||status.stopped?0:-16;}
static int power_unused(void *,uint64_t owner,uint64_t epoch,unsigned ldo){CHECK(owner==7&&epoch==9&&ldo>=1&&ldo<=7);return 0;} // model has no lens consumer
static int power_delay(void *,unsigned us){CHECK(us==150);IODelay(us);return 0;}
'''+at,'inactive proof samples actual CCI sensor and real pipeline state; physical inputs modeled')
    old='if(r==EldenCameraReset)s->reset=(unsigned)on;else if(r==EldenCameraClock)s->clock=(unsigned)on;else if(on)s->rails|=1u<<(r-EldenCameraDovdd);else s->rails&=~(1u<<(r-EldenCameraDovdd));return 0;'
    patch(old,'if(r==EldenCameraReset)s->reset=(unsigned)on;else if(r==EldenCameraClock)s->clock=(unsigned)on;else CHECK(false);return 0;',
          'board stub supplies only physical reset/MCLK; rail stub forbidden')
    patch('SensorState states[2];EldenCameraCapture captures[2]{};',
          '''SensorState states[2];sensor_states=states;EldenCameraPoweredCapture captures[2]{};
    Context shared_context;shared_context.serial=IOLockAlloc();i2c_peer=elden_camera_i2c10_peer_create(&shared_context.platform);
    CHECK(!elden_bl_platform_acquire(&shared_context.platform));shared_bus_publish(&shared_context);
    EldenCameraPowerOwner power_owner{nullptr,7,9,power_admit,power_inactive,power_delay,power_unused};
    CHECK(!elden_camera_power_session_open(&shared_power,&power_owner));''','real retained shared bus/WL session replaces fake supply callbacks')
    patch('CHECK(!elden_camera_capture_bind(&captures[target],&board,target));CHECK(!elden_camera_capture_probe(&captures[target])&&!states[target].rails&&!states[target].clock&&states[target].reset);',
          'CHECK(!elden_camera_powered_capture_bind(&captures[target],&shared_power,&board,target));CHECK(!elden_camera_powered_capture_probe(&captures[target])&&!elden_camera_i2c10_peer_stat(i2c_peer,6)%0x80&&!states[target].clock&&states[target].reset);',
          'actual original sensor probe restores real WL refs/selectors')
    patch('&captures[target].sensor.core','&captures[target].capture.sensor.core','same retained sensor core')
    patch('CHECK(!elden_camera_capture_start(&captures[target])&&states[target].rails==7&&states[target].clock&&!states[target].reset);',
          'CHECK(!elden_camera_powered_capture_start(&captures[target])&&(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x40)&&states[target].clock&&!states[target].reset);',
          'actual WL rails enabled before physical sensor')
    patch('CHECK(elden_camera_capture_stop(&captures[target])==-16&&states[target].rails==7&&states[target].clock);',
          'CHECK(elden_camera_powered_capture_stop(&captures[target])==-16&&(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x40)&&states[target].clock);',
          'failed real receiver drain retains shared WL power')
    patch('CHECK(!elden_camera_capture_stop(&captures[target])&&!states[target].rails&&!states[target].clock&&states[target].reset);',
          'CHECK(!elden_camera_powered_capture_stop(&captures[target])&&!(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x7f)&&!states[target].clock&&states[target].reset);',
          'actual sensor receiver drain before last real WL ref withdrawal')
    patch('CHECK(!elden_camera_capture_stop(&captures[target]));','CHECK(!elden_camera_powered_capture_stop(&captures[target]));','idempotent capture cleanup')
    patch('CHECK(captures[target].sensor.stopped);','CHECK(captures[target].capture.sensor.stopped);','actual CCI final stop ledger')
    patch('    for(auto *p:locks)delete p;',
          '    CHECK(shared_bus_release(&shared_context)==-16&&lease);CHECK(!elden_camera_power_session_stop(&shared_power));CHECK(!lease&&!elden_camera_i2c10_peer_stat(i2c_peer,6));\n    std::free(shared_power.bus.port);elden_camera_i2c10_peer_destroy(i2c_peer);\n    for(auto *p:locks)delete p;',
          'complete WL restoration and original deferred backlight release')
    # Test-only CCI type spelling keeps public C linkage at global scope.
    patch('#include "NativePipelineMethods.inc"',
          '''#include "NativePipelineMethods.inc"
namespace {
static unsigned lease=1;
struct Context {EldenBlPlatform platform{};IOLock *serial=nullptr;uint32_t shared_camera_refs=0;
    bool shared_closing=false,shared_published=false,shared_released=false;};
}
#include "SharedI2c10Methods.inc"
using SharedContext=Context;''',
          'execute actual shared methods with distinct original CCI/SE2 internal types')
    patch('#include "NativeCciMethods.inc"',
          '#define Context CciContext\n#include "NativeCciMethods.inc"\n#undef Context',
          'test-only type spelling avoids independent CCI/SE2 Context collision')
    patch('Context shared_context;','SharedContext shared_context;','use sole SE2 shared Context')
    patch('CHECK(!elden_camera_power_session_open(&shared_power,&power_owner));',
          '''CHECK(!elden_camera_power_session_open(&shared_power,&power_owner));
    EldenAw99706 backlight{};EldenBacklightIo backlight_io{&shared_context,shared_backlight_read,shared_backlight_write};
    CHECK(!elden_aw99706_inspect(&backlight,&backlight_io));CHECK(!elden_aw99706_set_level(&backlight,511));
    CHECK(!elden_aw99706_set_level(&backlight,49));''','actual brightness read/write shares engine with powered capture')
    # Parenthesize actual disabled-channel check without precedence ambiguity.
    patch('!elden_camera_i2c10_peer_stat(i2c_peer,6)%0x80','!(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x7f)','actual observed no WL channel')
    patch('PASS composed Gen4 front/rear capture %s:', 'PASS powered Gen4 front/rear capture %s:','accurate result label')
    return text.encode(),{'original_test_sha256':hashlib.sha256(source).hexdigest(),'transformations':changes,'physical_clock_pins_MMIO_GIC_DMA_modeled':True,'hardware':False}
