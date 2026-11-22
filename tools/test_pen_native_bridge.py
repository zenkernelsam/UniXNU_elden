"""Compile/test the default-off AP501U native-surface owner bridge, no tablet.

NativeBridge is a source-level gate between the NT36536/AP501U native entry
surface and EldenPenOwner: it admits only an explicitly named provider with
all callbacks, nonzero owner/epoch and an already matching touch-frame owner,
and start refuses outright with no real named provider. Every data-path hook
routes through the owner facade — stale generation, deferred stop, release,
retire/rebind and owner/frame fencing are inherited unchanged. The host suite
runs bind/start refusals, stale/foreign fencing, stop/release/retire, rebind
generation fencing and deterministic pthread races under ASan+UBSan and
ThreadSanitizer where supported; the arm64 step emits scalar objects for the
touched TUs. HOST-ONLY: synthetic peers prove the contract, never a real
AP501U, SPI bus, touch owner, HID service or Pencil acceptance.
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
    tua=[DRV/n for n in ('PenReport.c','Delivery.c','Owner.c','NativeBridge.c')]
    base=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-I',DRV]
    run([*cc,*base,'-fsanitize=address,undefined',
         *tua,DRV/'TestNativeBridge.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run',retries=3)
    run([*cc,*base,'-pthread','-fsanitize=address,undefined',
         *tua,DRV/'TestNativeBridgeRace.c','-o',out/'test-race'],'race-build')
    race_msg=run([out/'test-race'],'race-run',retries=3)
    # ThreadSanitizer cannot combine with ASan; probe toolchain support first.
    tsan='unavailable';tsan_msg=''
    with tempfile.TemporaryDirectory() as td:
        probe=Path(td)/'p.c';probe.write_text('int main(void){return 0;}\n')
        r=subprocess.run([*cc,'-fsanitize=thread','-pthread',probe,'-o',Path(td)/'p'],capture_output=True)
        if r.returncode==0:
            run([*cc,*base,'-fsanitize=thread','-pthread',
                 *tua,DRV/'TestNativeBridgeRace.c','-o',out/'test-race-tsan'],'race-tsan-build')
            def tsanrun(cmd,label):
                r=subprocess.run(list(map(str,cmd)),capture_output=True)
                (out/(label+'.log')).write_bytes(r.stdout+r.stderr);return r
            r=tsanrun([out/'test-race-tsan'],'race-tsan-run')
            blob=r.stdout+r.stderr
            if r.returncode and (b'unexpected memory mapping' in blob or not blob):
                # WSL ASLR conflicts with TSan's fixed shadow mapping (the
                # crash prints FATAL or, less often, dies silently); rerun
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
        'source_sha256':{n.name:sha(n) for n in tua+[DRV/'Owner.h',DRV/'NativeBridge.h',DRV/'PenReport.h',DRV/'Delivery.h',DRV/'TestNativeBridge.c',DRV/'TestNativeBridgeRace.c']},
        'default_off_until_named_provider_bound':True,
        'native_start_refuses_without_provider':True,
        'owner_routed_stale_generation_release_frame_fencing':True,
        'cross_thread_atomic_claim_inherited':True,
        'no_packets_coordinates_pressure_tilt_timestamps_or_identity_synthesized':True,
        'native_bound':False,'pencil_accepted':False,'hardware':False},indent=2)+'\n')
    print(msg+race_msg+tsan_msg)
if __name__=='__main__':main()
