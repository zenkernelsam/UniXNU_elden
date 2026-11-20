"""Compose real board/native clocks into the already tested powered capture."""
from pathlib import Path
import hashlib
from PowerStackSource import derive as power_stack
HERE=Path(__file__).resolve().parent

def derive():
    raw,previous=power_stack();text=raw.decode();changes=[]
    def patch(before,after,why):
        nonlocal text
        if text.count(before)!=1:raise ValueError('Powered capture test anchor differs: '+why)
        text=text.replace(before,after);changes.append(why)
    patch('#include "PowerBinding.h"','#include "PowerBinding.h"\n#include "Board.h"','actual board/native clocks')
    patch('void *elden_camera_i2c10_peer_create(EldenBlPlatform *);',
          'void elden_camera_clock_peer_setup(void);int elden_camera_clock_peer_read(unsigned,uint32_t,uint32_t *);int elden_camera_clock_peer_write(unsigned,uint32_t,uint32_t);\nvoid elden_camera_clock_peer_delay(unsigned);uint64_t elden_camera_clock_peer_now(void);EldenCameraClockIo elden_camera_clock_peer_io(void);\nint elden_camera_clock_peer_reset(unsigned);int elden_camera_clock_peer_mclk(unsigned);\nvoid *elden_camera_i2c10_peer_create(EldenBlPlatform *);','independent clock HW peer with actual original methods')
    patch('static void IODelay(unsigned us){for(auto p:cci_peers)',
          'static void IODelay(unsigned us){elden_camera_clock_peer_delay(us);for(auto p:cci_peers)','one clock progression for actual core and CSI peers')
    patch('#include "NativePipelineMethods.inc"','''#include "NativePipelineMethods.inc"
namespace {
struct CameraClockWindow {unsigned space=9;
    bool map(uint64_t pa,uint32_t bytes){mappings++;if(pa==0xade0000){CHECK(bytes==0x20000);space=0;}
        else if(pa==0x1760000){CHECK(bytes==0x6000);space=1;}else{CHECK(pa==0xf100000&&bytes==0x78000);space=2;}return true;}
    int read(uint32_t reg,uint32_t *out){return elden_camera_clock_peer_read(space,reg,out);}
    int write(uint32_t reg,uint32_t value){return elden_camera_clock_peer_write(space,reg,value);}
};
static uint64_t camera_clock_now(void *){return elden_camera_clock_peer_now();}
}
#include "NativeClockMethods.inc"''','actual native clock methods alongside original CCI/receiver')
    patch('if(!sensor_states||!sensor_states[sensor].reset||sensor_states[sensor].clock||elden_camera_cci_peer_stat(cci_peers[sensor],2))return -16;',
          'if(!sensor_states||!elden_camera_clock_peer_reset(sensor)||elden_camera_clock_peer_mclk(sensor)||elden_camera_cci_peer_stat(cci_peers[sensor],2))return -16;',
          'WL inactivity samples actual clock/reset peer, not fake sensor state')
    old='''static int sensor_resource(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned r,int on){auto *s=(SensorState *)p;CHECK(owner==7&&epoch==9&&sensor==s->sensor&&r<=EldenCameraAvdd);
    if(r==EldenCameraReset)s->reset=(unsigned)on;else if(r==EldenCameraClock)s->clock=(unsigned)on;else CHECK(false);return 0;}'''
    patch(old,'''static unsigned receiver_refs[2];
static int receiver_resources(void *,const EldenCameraPipelineConfig *cfg,int action){CHECK(cfg->owner==7&&cfg->epoch==9&&cfg->sensor<2);
    if(action==1){CHECK(!receiver_refs[cfg->sensor]);receiver_refs[cfg->sensor]=1;}
    else if(action==2)CHECK(receiver_refs[cfg->sensor]);else{CHECK(!elden_camera_cci_peer_stat(cci_peers[cfg->sensor],2));receiver_refs[cfg->sensor]=0;}return 0;}
static int board_admit(void *,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){CHECK(owner==7&&epoch==9&&sensor<2&&op<=3);return 0;}
static int board_delay(void *,unsigned us){IODelay(us);return 0;}''','no fake physical reset/MCLK callback remains')
    patch('static int sensor_admit(void *p,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned op){auto *s=(SensorState *)p;CHECK(s->sensor==sensor&&owner==7&&epoch==9&&op<=3);return 0;}\n','',
          'board admission uses actual original entry provider')
    patch('static int sensor_delay(void *,unsigned us){IODelay(us);return 0;}\n','', 'board carries real delay')
    patch('EldenCameraCciProvider cci{nullptr,7,9,cci_admit,cci_clock};EldenCameraNativePipelineOwner owner{nullptr,pipeline_owner,pipeline_owner_buffer,pipeline_owner_drain};',
          'EldenCameraBoard camera_board{};', 'actual Board owns CCI/receiver provider')
    patch('SensorState states[2];sensor_states=states;EldenCameraPoweredCapture captures[2]{};',
          'SensorState states[2];sensor_states=states;','board holds real powered captures')
    patch('CHECK(!elden_aw99706_set_level(&backlight,49));',
          '''CHECK(!elden_aw99706_set_level(&backlight,49));
    elden_camera_clock_peer_setup();auto cio=elden_camera_clock_peer_io();
    EldenCameraNativeClockOwner clock_owner{cio.context,7,9,cio.admit,cio.lock,cio.controllers,cio.dependency,cio.consumer_idle,cio.sensor_idle,cio.root_unused,cio.domain_unused,cio.pins_unused};
    EldenCameraNativeClocks *native_clock=nullptr;CHECK(!elden_camera_native_clocks_open(&clock_owner,&native_clock));
    CHECK(!elden_camera_native_pins_prepare(native_clock,7,9));
    EldenCameraBoardOwner board_owner{nullptr,7,9,board_admit,board_delay,pipeline_owner_drain,pipeline_owner_buffer,receiver_resources};
    CHECK(!elden_camera_board_bind(&camera_board,native_clock,&shared_power,&board_owner));''','real native graph then actual camera board binding')
    patch('states[target].sensor=target;CHECK(!elden_camera_native_cci_open(&cci,target));EldenCameraPipelineConfig cfg{};',
          'states[target].sensor=target;EldenCameraPipelineConfig cfg{};','real board prepare owns CCI opening')
    patch('CHECK(!elden_camera_native_pipeline_open(&cfg,&owner));EldenCameraSensorProvider board{&states[target],7,9,sensor_admit,sensor_resource,sensor_delay,nullptr,nullptr};',
          '''EldenCameraBoardClockPlan plan{};for(unsigned i=0;i<EldenCamClockCount;i++)plan.hz[i]=400000000;
        plan.hz[EldenCamClockCpasAhb]=80000000;plan.hz[EldenCamClockVfeLiteAhb]=80000000;
        plan.hz[EldenCamClockCpasFastAhb]=300000000;plan.hz[EldenCamClockVfe0FastAhb]=plan.hz[EldenCamClockVfe1FastAhb]=plan.hz[EldenCamClockVfe2FastAhb]=300000000;
        plan.hz[EldenCamClockVfe0]=plan.hz[EldenCamClockVfe1]=plan.hz[EldenCamClockVfe2]=480000000;
        plan.hz[EldenCamClockCpasVfe0]=plan.hz[EldenCamClockCpasVfe1]=plan.hz[EldenCamClockCpasVfe2]=480000000;
        plan.hz[EldenCamClockCamnocNrtAxi]=plan.hz[EldenCamClockCamnocRtAxi]=200000000;plan.hz[EldenCamClockQdssDebugXo]=19200000;
        if(!target){auto bad=plan;bad.hz[EldenCamClockCsiphy3Timer]=0;
            CHECK(elden_camera_board_prepare(&camera_board,&cfg,&bad)==-22&&!receiver_refs[0]&&!camera_board.attempted[0]);
            bad=plan;bad.hz[EldenCamClockCpasAhb]=400000000;
            CHECK(elden_camera_board_prepare(&camera_board,&cfg,&bad)==-34&&!receiver_refs[0]&&!camera_board.attempted[0]);}
        CHECK(!elden_camera_board_prepare(&camera_board,&cfg,&plan));''','source-supported explicit HOSTONLY rate plan, actual board/CCI/receiver')
    patch('CHECK(!elden_camera_powered_capture_bind(&captures[target],&shared_power,&board,target));CHECK(!elden_camera_powered_capture_probe(&captures[target])',
          'CHECK(!elden_camera_board_probe(&camera_board,target)','no duplicate capture binding')
    patch('&captures[target].capture.sensor.core','&camera_board.captures[target].capture.sensor.core','real board capture storage')
    patch('elden_camera_powered_capture_start(&captures[target])','elden_camera_board_start(&camera_board,target)','actual board start wrapper')
    # All two stop call sites use same real board wrapper (normal/failure).
    for before,after in [('CHECK(elden_camera_powered_capture_stop(&captures[target])==-16', 'CHECK(elden_camera_board_stop(&camera_board,target)==-16'),
                         ('CHECK(!elden_camera_powered_capture_stop(&captures[target])&&','CHECK(!elden_camera_board_stop(&camera_board,target)&&'),
                         ('CHECK(!elden_camera_powered_capture_stop(&captures[target]));','CHECK(!elden_camera_board_stop(&camera_board,target));')]:patch(before,after,'real board cleanup wrapper')
    patch('CHECK(captures[target].capture.sensor.stopped);','CHECK(camera_board.captures[target].capture.sensor.stopped);','actual capture/CCI stop ledger')
    for before,after in [('!states[target].clock&&states[target].reset','!elden_camera_clock_peer_mclk(target)&&elden_camera_clock_peer_reset(target)'),
                         ('states[target].clock&&!states[target].reset','elden_camera_clock_peer_mclk(target)&&!elden_camera_clock_peer_reset(target)'),
                         ('&&states[target].clock);','&&elden_camera_clock_peer_mclk(target));')]:
        text=text.replace(before,after)
    patch('CHECK(!elden_camera_power_session_stop(&shared_power));','CHECK(!elden_camera_board_shutdown(&camera_board));','real both-camera/clock/pin/WL shutdown')
    patch('std::free(shared_power.bus.port);','std::free(native_clock);std::free(shared_power.bus.port);','HOSTONLY teardown of retained test allocations')
    # Original provider functions now live in the actual Board adapters.
    for signature in ('static int cci_admit(','static uint32_t cci_clock(','static int pipeline_owner('):
        at=text.index(signature);end=text.index('{',at)+1;depth=1
        while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
        text=text[:at]+text[end:]
    # Prepare/probe/queue BOTH cameras, start BOTH, then exercise one failed
    # physical drain. A fault closes new starts but must still retire existing
    # peer references safely. Never clear the production board error latch.
    patch('        CHECK(!elden_camera_board_start(&camera_board,target)',
          '    }\n    for(unsigned target=0;target<2;target++){\n        CHECK(!elden_camera_board_start(&camera_board,target)',
          'both targets prepare before either stream/error closes admission')
    patch('        if(drain_fail){CHECK(elden_camera_board_stop(&camera_board,target)',
          '    }\n    for(unsigned target=0;target<2;target++){\n        EldenCameraPipelineStatus status{};\n        if(drain_fail){CHECK(elden_camera_board_stop(&camera_board,target)',
          'both real streams run before fault/peer-safe stop and shared shutdown')
    patch('CHECK(!elden_camera_board_stop(&camera_board,target)&&!(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x7f)&&!elden_camera_clock_peer_mclk(target)&&elden_camera_clock_peer_reset(target));',
          'CHECK(!elden_camera_board_stop(&camera_board,target)&&!(elden_camera_i2c10_peer_stat(i2c_peer,6)&(target?0x6u:0x9u))&&!elden_camera_clock_peer_mclk(target)&&elden_camera_clock_peer_reset(target));',
          'own DVDD/AVDD off while peer/LDO7 supply stays live')
    patch('PASS powered Gen4 front/rear capture %s:', 'PASS actual board-clock Gen4 front/rear capture %s:','precise result label')
    return text.encode(),{'parent_test_sha256':hashlib.sha256(raw).hexdigest(),'original_power_stack':previous,'transformations':changes,
        'physical_PLL_RPMh_MMIO_IRQ_DMA_peers_modeled':True,'hardware':False}
