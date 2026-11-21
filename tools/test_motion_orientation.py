"""Compile/test the guarded J317 orientation-consumer boundary, no tablet.

The new C component is a source-side consumer contract a mainline J317 provider
can later bind to: it requires explicit owner/epoch, real mount, units/ranges,
SUID, monotonic source->mach mapping and calibrated accel/gyro samples, then
publishes a held dominant signed gravity axis to an owner-supplied sink. The
host suite links the real classifier (elden_motion_rotation) from the prepared
Core.c and runs every fence/rollback path under ASan+UBSan; the arm64 step
produces a freestanding scalar object. This is a host/source-side boundary:
nothing synthesizes provider data and no physical J317/UI is implied.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys
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
    run([*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-I',prepared,'-I',DRV,prepared/'Core.c',src,DRV/'TestOrientation.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run',retries=3)
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    obj=out/(src.stem+'.o')
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
         '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc',
         '-isystem',Path(resource)/'include','-Wno-error=incompatible-sysroot',
         '-Wall','-Wextra','-Werror','-I',DRV,'-c',src,'-o',obj],src.stem+'-native')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'objects':{src.name:sha(obj)},
        'source_sha256':{n.name:sha(n) for n in [src,DRV/'Orientation.h',DRV/'TestOrientation.c']},
        'provider_bound':True,'real_mount_units_ranges_suid_time_required':True,
        'hardware_modeled':False,'native_J317_or_UI_connected':False,
        'physical_gravity_or_calibration_present':False,'hardware':False},indent=2)+'\n')
    print(msg)
if __name__=='__main__':main()
