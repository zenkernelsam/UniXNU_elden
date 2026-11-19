"""Test actual tagged Actuator ring and compile a scalar ARM64 kernel object."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics/actuator'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['source','llvm','output']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Source identity differs')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,a.source/'Ring.c',DRIVER/'TestRing.c','-o',out/'test'],'host-build')
    message=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'ActuatorRing.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Ring.c','-o',obj],'native-build')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar ring code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'object_sha256':sha(obj),'source_record_sha256':sha(a.source/'source.json'),
        'shared_memory_identity_tested':True,'native_stream_connected':False,'hardware':False},indent=2)+'\n');print(message)
if __name__=='__main__':main()
