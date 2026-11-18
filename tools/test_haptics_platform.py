"""Check the actual SE6 platform and compile its scalar ARM64 code. No device IO."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Generated source changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-I',a.source,a.source/'Platform.c',ROOT/'drivers/EldenHaptics/TestPlatform.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'Platform.o'
    cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Platform.c','-o',obj]
    run(cmd,'native-build');dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'source_record_sha256':sha(a.source/'source.json'),
        'source_sha256':sha(a.source/'Platform.c'),'object_sha256':sha(obj),'command':list(map(str,cmd)),
        'hardware':False,'MMIO_peers_modeled':True,'native_worker_connected':False},indent=2)+'\n');print(msg)
if __name__=='__main__':main()
