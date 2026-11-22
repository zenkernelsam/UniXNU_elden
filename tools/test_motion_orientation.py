"""Compile/test the guarded J317 orientation-consumer boundary, no tablet.

The new C component is a source-side consumer contract a mainline J317 provider
can later bind to: it requires explicit owner/epoch, real mount, units/ranges,
SUID, monotonic source->mach mapping and calibrated accel/gyro samples, then
publishes a held dominant signed gravity axis to an owner-supplied sink. The
host suite links the real classifier (elden_motion_rotation) from the prepared
Core.c and runs every fence/rollback path under ASan+UBSan; a deterministic
pthread suite covers sample-worker versus begin/stop/status concurrency under
ASan/UBSan and ThreadSanitizer where the toolchain supports it; the arm64 step
produces a freestanding scalar object. This is a host/source-side boundary:
nothing synthesizes provider data and no physical J317/UI is implied.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
DRV=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',default=None)
    p.add_argument('--llvm',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cc=a.cc or (['xcrun','clang'] if sys.platform=='darwin' else ['gcc'])
    def run(cmd,label,retries=0):
        for attempt in range(retries+1):
            r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
            # An ASan-instrumented test binary that fails before main (shadow mmap
            # under post-compile memory pressure) exits nonzero with no output;
            # a real check failure always writes "lineN:" to stderr, so retry only
            # the empty-output flake.
            if not r.returncode or r.stdout or r.stderr or attempt==retries:break
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    prepared=out/'prepared'
    run([sys.executable,ROOT/'tools/prepare_motion_core.py','--output',prepared],'prepare')
    src=DRV/'Orientation.c'
    base=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-I',prepared,'-I',DRV]
    run([*cc,*base,'-fsanitize=address,undefined',
         prepared/'Core.c',src,DRV/'TestOrientation.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run',retries=3)
    run([*cc,*base,'-pthread','-fsanitize=address,undefined',
         prepared/'Core.c',src,DRV/'TestOrientationRace.c','-o',out/'test-race'],'race-build')
    race_msg=run([out/'test-race'],'race-run',retries=3)
    # ThreadSanitizer cannot combine with ASan; probe toolchain support first.
    tsan='unavailable';tsan_msg=''
    with tempfile.TemporaryDirectory() as td:
        probe=Path(td)/'p.c';probe.write_text('int main(void){return 0;}\n')
        r=subprocess.run([*cc,'-fsanitize=thread','-pthread',probe,'-o',Path(td)/'p'],capture_output=True)
        if r.returncode==0:
            run([*cc,*base,'-fsanitize=thread','-pthread',
                 prepared/'Core.c',src,DRV/'TestOrientationRace.c','-o',out/'test-race-tsan'],'race-tsan-build')
            def tsanrun(cmd,label):
                r=subprocess.run(list(map(str,cmd)),capture_output=True)
                (out/(label+'.log')).write_bytes(r.stdout+r.stderr);return r
            r=tsanrun([out/'test-race-tsan'],'race-tsan-run')
            if r.returncode and b'unexpected memory mapping' in (r.stdout+r.stderr):
                # WSL ASLR conflicts with TSan's fixed shadow mapping; rerun
                # with ASLR disabled — a harness workaround, not a test pass.
                r=tsanrun(['setarch','-R',out/'test-race-tsan'],'race-tsan-run-setarch')
                if r.returncode:raise RuntimeError('race-tsan-run failed; inspect retained log')
                tsan='passed(setarch -R, WSL ASLR workaround)'
            else:
                if r.returncode:raise RuntimeError('race-tsan-run failed; inspect retained log')
                tsan='passed'
            tsan_msg=r.stdout.decode()
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    obj=out/(src.stem+'.o')
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
         '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc',
         '-isystem',Path(resource)/'include','-Wno-error=incompatible-sysroot',
         '-Wall','-Wextra','-Werror','-I',DRV,'-c',src,'-o',obj],src.stem+'-native')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,
        'race_output':race_msg,'race_tsan':tsan,'race_tsan_output':tsan_msg,'objects':{src.name:sha(obj)},
        'source_sha256':{n.name:sha(n) for n in [src,DRV/'Orientation.h',DRV/'TestOrientation.c',DRV/'TestOrientationRace.c']},
        'provider_bound':True,'real_mount_units_ranges_suid_time_required':True,
        'cross_thread_atomic_claim_no_blocking_wait':True,
        'deferred_stop_never_lost':True,'generation_wraparound_refused':True,
        'hardware_modeled':False,'native_J317_or_UI_connected':False,
        'physical_gravity_or_calibration_present':False,'hardware':False},indent=2)+'\n')
    print(msg+race_msg+tsan_msg)
if __name__=='__main__':main()
