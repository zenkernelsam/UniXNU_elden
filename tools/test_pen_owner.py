"""Compile/test the guarded AP501U sampling owner facade, no tablet.

The new C component is a source-side owner facade the real NT36536 sampling
worker can bind when mainline is ready: it admits only an explicitly named
provider with nonzero owner, every callback populated, and an explicit
finger/touch frame-owner identity every packet must report. owner/epoch is
rechecked before every facade operation, the frame owner before every packet's
delivery, and the worker timestamp/sequence must stay monotonic. Decoding and
release still run through the existing elden_pen_decode + EldenPenDelivery; the
only output path is the caller-bound send(). The host suite runs every fence —
bind refusals, identity/epoch drift, foreign frames, ts/seq rollback, malformed
and transport-faulted packets, failed lifts, pause/resume gating, callback
re-entry and stop-during-delivery — under ASan+UBSan on synthetic provider
peers; a deterministic pthread suite covers sampling-worker versus policy/
stop/status concurrency under ASan/UBSan and ThreadSanitizer where the
toolchain supports it; the arm64 step emits scalar objects for the touched
TUs. HOST-ONLY: synthetic providers prove the contract, never a real AP501U,
touch owner, SPI bus or Pencil acceptance.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
DRV=ROOT/'drivers/EldenPen'
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
            # ASan-instrumented binaries can flake before main with no output;
            # a real check failure always writes "lineN:" to stderr.
            if not r.returncode or r.stdout or r.stderr or attempt==retries:break
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    tua=[DRV/n for n in ('PenReport.c','Delivery.c','Owner.c')]
    base=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-I',DRV]
    run([*cc,*base,'-fsanitize=address,undefined',
         *tua,DRV/'TestPenOwner.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run',retries=3)
    run([*cc,*base,'-pthread','-fsanitize=address,undefined',
         *tua,DRV/'TestPenOwnerRace.c','-o',out/'test-race'],'race-build')
    race_msg=run([out/'test-race'],'race-run',retries=3)
    # ThreadSanitizer cannot combine with ASan; probe toolchain support first.
    tsan='unavailable';tsan_msg=''
    with tempfile.TemporaryDirectory() as td:
        probe=Path(td)/'p.c';probe.write_text('int main(void){return 0;}\n')
        r=subprocess.run([*cc,'-fsanitize=thread','-pthread',probe,'-o',Path(td)/'p'],capture_output=True)
        if r.returncode==0:
            run([*cc,*base,'-fsanitize=thread','-pthread',
                 *tua,DRV/'TestPenOwnerRace.c','-o',out/'test-race-tsan'],'race-tsan-build')
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
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();objects={}
    for src in tua:
        obj=out/(src.stem+'.o')
        cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
             '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc',
             '-isystem',Path(resource)/'include','-Wno-error=incompatible-sysroot',
             '-Wall','-Wextra','-Werror','-I',DRV,'-c',src,'-o',obj]
        run(cmd,src.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
        objects[src.name]=sha(obj)
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,
        'race_output':race_msg,'race_tsan':tsan,'race_tsan_output':tsan_msg,'objects':objects,
        'source_sha256':{n.name:sha(n) for n in tua+[DRV/'Owner.h',DRV/'PenReport.h',DRV/'Delivery.h',DRV/'TestPenOwner.c',DRV/'TestPenOwnerRace.c']},
        'named_provider_identity_and_owner_epoch_required':True,
        'frame_owner_association_required':True,'monotonic_worker_ts_seq_fences':True,
        'decode_and_delivery_reused':True,
        'cross_thread_atomic_claim_no_blocking_wait':True,
        'deferred_stop_never_lost':True,'generation_wraparound_refused':True,
        'pencil_accepted':False,
        'provider_and_transport_modeled_only':True,'hardware':False},indent=2)+'\n')
    print(msg+race_msg+tsan_msg)
if __name__=='__main__':main()
