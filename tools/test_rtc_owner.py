"""Compile/test the guarded RTC persistence/alarm owner facade, no tablet.

The new C component is a source-side owner facade a mainline RTC clock owner
can later bind to a real named NVMEM offset cell and the real PMK8550 0x62/1
alarm owner. It requires explicit named-provider identity, nonzero owner/epoch,
serialized callbacks and real stop/retire fences before the existing
elden_rtc_core_bind_persistence/elden_rtc_core_bind_alarm run; every call
re-checks owner/epoch and rejects stale generations before provider I/O, and
alarm acknowledge is callable only from the owner-supplied IRQ path. The host
suite links the real persistence/alarm core from the prepared Core.c and runs
every fence/uncertainty path under ASan+UBSan; the arm64 step produces a
freestanding scalar object. This is a host/source-side boundary: it chooses no
cell, SDAM offset, bank or IRQ, synthesizes no provider data, and implies no
physical NVMEM/alarm/RTC hardware.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys
ROOT=Path(__file__).resolve().parents[1]
DRV=ROOT/'drivers/EldenRtc'
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
    run([sys.executable,ROOT/'tools/prepare_elden_rtc.py','--output',prepared],'prepare')
    src=DRV/'Owner.c'
    run([*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-I',prepared,'-I',DRV,prepared/'Core.c',src,DRV/'TestOwner.c','-o',out/'test'],'host-build')
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
        'source_sha256':{n.name:sha(n) for n in [src,DRV/'Owner.h',DRV/'TestOwner.c']},
        'provider_bound':True,'named_provider_identity_and_owner_epoch_required':True,
        'alarm_ack_irq_path_only':True,'no_cell_sdam_bank_irq_chosen':True,
        'hardware_modeled':False,'physical_nvmem_alarm_or_rtc_present':False,'hardware':False},indent=2)+'\n')
    print(msg)
if __name__=='__main__':main()
