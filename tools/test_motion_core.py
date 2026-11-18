"""Test actual source-derived SSC codec/math and original-parser regressions."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);r=json.loads((a.source/'source.json').read_text())
    for name,digest in r['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Motion input changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
    run([*flags,'-I',a.source,a.source/'Core.c',DRIVER/'TestCore.c','-o',out/'test'],'host-build');msg=run([out/'test'],'host-run')
    run([*flags,'-I',DRIVER/'upstream',DRIVER/'upstream/T970Ssc.c',DRIVER/'TestOriginal.c','-o',out/'original'],'original-build');old=run([out/'original'],'original-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'MotionCore.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Core.c','-o',obj],'native-build')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('FP/vector code refused')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'original_regression':old,'object_sha256':sha(obj),
        'source_record_sha256':sha(a.source/'source.json'),'source_protocol_and_integer_math':True,'native_transport_or_HID_connected':False,
        'fake_gravity_copied':False,'hardware':False,'full_link':False},indent=2)+'\n');print(msg+old)
if __name__=='__main__':main()
