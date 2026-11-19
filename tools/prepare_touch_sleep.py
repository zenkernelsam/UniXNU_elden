"""Derive default-off wake flow hooks from an already qualified pen touch worker.

Preserve the original normal sampling, firmware and DMA/SMMU code. All wake/SPI
execution stays on the original thread; PM/provider commands are acknowledged.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];WAKE=ROOT/'drivers/EldenWake'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('pen-source','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.pen_source=a.pen_source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=json.loads((a.pen_source/'source.json').read_text())
    for n,d in spec['sources'].items():
        if sha(a.pen_source/n)!=d:raise ValueError('Qualified pen parent changed')
        shutil.copyfile(a.pen_source/n,out/n)
    body=(out/'EldenTouchRuntime.cpp').read_text();before=sha(out/'EldenTouchRuntime.cpp');changes=[]
    def once(old,new):
        nonlocal body
        if body.count(old)!=1:raise ValueError('Touch sleep anchor changed: '+old[:70])
        body=body.replace(old,new);changes.append({'before':old,'after':new})
    once('#include "EldenPenNative.h"','#include "EldenPenNative.h"\n#include "Flow.h"\n#include "NativeIrq.h"\n#include "TouchPower.h"')
    once('#include <kern/sched_prim.h>','#include <kern/sched_prim.h>\n#include <IOKit/IOLocks.h>\nextern "C" boolean_t ml_at_interrupt_context(void);')
    once('bool dma_mode=false,streaming=false;unsigned penErrors=0;',
        'bool dma_mode=false,streaming=false;unsigned penErrors=0;\n#include "TouchFields.inc"')
    once('static void finish(Context *c,int error){','''#include "TouchCoordinator.inc"
static void finish(Context *c,int error){
    int wakeStopped=wake_stop(c);
    if(wakeStopped){__atomic_store_n(&input_release_error,wakeStopped,__ATOMIC_RELEASE);c->platform.quarantined=true;}''')
    once('    if(released==0){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}',
        '    if(released==0&&!wakeStopped){if(!c->wake_enabled)delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}')
    once('    c->streaming=true;','''    int wakeBind=wake_bind(c,&image);
    if(wakeBind){log("\\nELDEN_WAKE coordinator refused=%d; maps retained\\n",wakeBind);finish(c,wakeBind);return;}
    c->streaming=true;''')
    once('    while(elden_touch_run_state(&run_control)==EldenTouchRunning){\n        uint8_t packet',
        '''    while(elden_touch_run_state(&run_control)==EldenTouchRunning){
        int wakeRound=wake_round(c);
        if(wakeRound<0){
            if(c->wake_flow.state==EldenWakeFlowHeld||__atomic_load_n(&c->wake_closed,__ATOMIC_ACQUIRE)){finish(c,wakeRound);return;}
            continue; // a refused power request is not a failed SPI transaction
        }
        if(wakeRound>0)continue; // paused/gesture wait; never feed normal HID
        uint8_t packet''')
    once('    unsigned state=elden_touch_run_request_stop(&run_control);',
        '''    unsigned state=elden_touch_run_request_stop(&run_control);
    Context *wakeContext=__atomic_load_n(&wake_touch_active,__ATOMIC_ACQUIRE);
    wake_signal(wakeContext);
    if(wakeContext&&wakeContext->wake_enabled){
        // The worker must acknowledge real finger/pen release and IRQ/SPI
        // stop BEFORE the terminal finger HID service is withdrawn.
        for(unsigned n=0;n<300&&state==EldenTouchStopping;n++){IOSleep(10);state=elden_touch_run_state(&run_control);}
        if(state!=EldenTouchStopped)return 0; // keep sender/services for unconfirmed actual release
    }''')
    # Preserve the already-qualified independent default-off haptics startup.
    anchor='extern "C" void elden_touch_runtime_start(void){'
    once(anchor,anchor+'\n    elden_haptics_native_start(); // existing independent default-off worker')
    body='extern "C" void elden_haptics_native_start(void);\n'+body
    (out/'EldenTouchRuntime.cpp').write_text(body)
    for n in ['Flow.h','Flow.c','Gesture.h','Gesture.c','NativeIrq.h','TouchPower.h','TouchFields.inc','TouchCoordinator.inc']:shutil.copyfile(WAKE/n,out/n)
    # The captured v152 Protocol header predates the OEM command. Use current
    # exact protocol declaration/source; final link replaces that object once.
    for n in ['Protocol.h','Protocol.c']:shutil.copyfile(ROOT/'drivers/EldenNvt36536'/n,out/n)
    (out/'source.json').write_text(json.dumps({'qualified':True,'source_parent':sha(a.pen_source/'source.json'),'parent_touch_sha256':before,
        'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'exact_changes':changes,
        'real_power_provider_installed':False,'default_wake_disabled':True,'hardware':False},indent=2)+'\n')
    print('Prepared exact pen-preserving original touch worker with default-off serial wake flow and actual pause/stop hooks; real power provider absent')
if __name__=='__main__':main()
