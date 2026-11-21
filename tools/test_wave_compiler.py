"""Compile/test the source-side continuous waveform->RTP-chunk compiler, no tablet.

The new C component converts a parsed recipe to bounded dual-motor RTP chunks.
Host coverage runs the contract/refusal suite under ASan+UBSan; the arm64 step
produces a freestanding scalar object with the same profile as the other
haptics sources. This is a host/source-side qualification: no chip is fed and
no measured calibration is implied.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys
ROOT=Path(__file__).resolve().parents[1]
DRV=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',default=None)
    p.add_argument('--llvm',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cc=a.cc or (['xcrun','clang'] if sys.platform=='darwin' else ['gcc'])
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    deps=[DRV/'WaveCompiler.c',DRV/'SampleMap.c']
    run([*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-I',DRV,*deps,DRV/'TestWaveCompiler.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    objects={}
    for src in deps:
        obj=out/(src.stem+'.o')
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
             '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc',
             '-isystem',Path(resource)/'include','-Wno-error=incompatible-sysroot',
             '-Wall','-Wextra','-Werror','-I',DRV,'-c',src,'-o',obj],src.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
        objects[src.name]=sha(obj)
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'objects':objects,
        'source_sha256':{n.name:sha(n) for n in deps+[DRV/'TestWaveCompiler.c',DRV/'WaveCompiler.h']},
        'hardware_modeled':False,'native_RTP_profile_connected':False,
        'waveform_semantics_qualified':False,'measured_calibration_present':False,'hardware':False},indent=2)+'\n')
    print(msg)
if __name__=='__main__':main()
