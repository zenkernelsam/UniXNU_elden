"""Compile/test the guarded display/system-PM owner facade, no tablet.

The C component is a source-side owner contract mainline can later bind to the
real panel/rail/system power provider required by the existing touch wake
flow. It requires explicit named-provider identity, nonzero owner/epoch, an
asynchronous wake request plus an explicit panel/rail readiness result before
the existing bounded elden_touch_wake_request resume path is invoked, and
fences synchronous self-ack, stale owner/epoch, duplicate/out-of-order
readiness, callbacks after stop and retirement while a callback is active.
Cross-thread paths (touch worker vs provider thread vs policy threads) use a
non-blocking atomic claim — refusals, never waits — so no deadlock is
introduced. The host suite runs every fence under ASan+UBSan, a deterministic
pthread concurrency suite under ASan/UBSan and under ThreadSanitizer where the
toolchain supports it; the arm64 step produces a freestanding scalar object.
This is a host/source-side boundary: it chooses no panel register, rail value,
epoch or readiness, synthesizes no provider data, and implies no physical
display/system power hardware.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
DRV=ROOT/'drivers/EldenWake'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',default=None)
    p.add_argument('--llvm',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cc=a.cc or (['xcrun','clang'] if sys.platform=='darwin' else ['gcc'])
    def run(cmd,label,retries=0,fail=True):
        for attempt in range(retries+1):
            r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
            # An ASan-instrumented test binary that fails before main (shadow mmap
            # under post-compile memory pressure) exits nonzero with no output;
            # a real check failure always writes "lineN:" to stderr, so retry only
            # the empty-output flake.
            if not r.returncode or r.stdout or r.stderr or attempt==retries:break
        if fail and r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r
    src=DRV/'PowerOwner.c'
    base=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-I',DRV]
    run([*cc,*base,'-fsanitize=address,undefined',src,DRV/'TestPowerOwner.c','-o',out/'test'],'host-build')
    r=run([out/'test'],'host-run',retries=3);msg=r.stdout.decode()
    run([*cc,*base,'-pthread','-fsanitize=address,undefined',src,DRV/'TestPowerOwnerRace.c',
         '-o',out/'test-race'],'race-build')
    r=run([out/'test-race'],'race-run',retries=3);race_msg=r.stdout.decode()
    # ThreadSanitizer cannot combine with ASan; probe toolchain support first.
    tsan='unavailable';tsan_msg=''
    with tempfile.TemporaryDirectory() as td:
        probe=Path(td)/'p.c';probe.write_text('int main(void){return 0;}\n')
        if run([*cc,'-fsanitize=thread','-pthread',probe,'-o',Path(td)/'p'],'tsan-probe',fail=False).returncode==0:
            run([*cc,*base,'-fsanitize=thread','-pthread',src,DRV/'TestPowerOwnerRace.c',
                 '-o',out/'test-race-tsan'],'race-tsan-build')
            r=run([out/'test-race-tsan'],'race-tsan-run',retries=1,fail=False)
            if r.returncode and b'unexpected memory mapping' in (r.stdout+r.stderr):
                # WSL ASLR conflicts with TSan's fixed shadow mapping; rerun
                # with ASLR disabled — a harness workaround, not a test pass.
                r=run(['setarch','-R',out/'test-race-tsan'],'race-tsan-run-setarch',fail=False)
                if r.returncode:raise RuntimeError('race-tsan-run failed; inspect retained log')
                tsan='passed(setarch -R, WSL ASLR workaround)'
            else:
                if r.returncode:raise RuntimeError('race-tsan-run failed; inspect retained log')
                tsan='passed'
            tsan_msg=r.stdout.decode()
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').stdout.decode().strip()
    obj=out/(src.stem+'.o')
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
         '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc',
         '-isystem',Path(resource)/'include','-Wno-error=incompatible-sysroot',
         '-Wall','-Wextra','-Werror','-I',DRV,'-c',src,'-o',obj],src.stem+'-native')
    r=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',r.stdout.decode(),re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'race_output':race_msg,
        'race_tsan':tsan,'race_tsan_output':tsan_msg,'objects':{src.name:sha(obj)},
        'source_sha256':{n.name:sha(n) for n in [src,DRV/'PowerOwner.h',DRV/'TestPowerOwner.c',DRV/'TestPowerOwnerRace.c']},
        'provider_bound':True,'named_provider_identity_and_owner_epoch_required':True,
        'asynchronous_ready_result_required':True,'synchronous_self_ack_refused':True,
        'cross_thread_atomic_claim_no_blocking_wait':True,'single_outstanding_request':True,
        'resume_path_injected_existing_request_boundary_preserved':True,
        'no_panel_rail_epoch_readiness_chosen':True,
        'hardware_modeled':False,'physical_power_provider_present':False,'hardware':False},indent=2)+'\n')
    print(msg+race_msg+tsan_msg)
if __name__=='__main__':main()
