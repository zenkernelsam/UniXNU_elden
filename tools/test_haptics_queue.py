"""Test actual haptic queue/session/budget semantics and compile its ARM64 object."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--llvm',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);driver=ROOT/'drivers/EldenHaptics'
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',driver/'Queue.c',driver/'TestQueue.c','-o',out/'test'],'host-build')
    message=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'Queue.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-c',driver/'Queue.c','-o',obj],'native')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'disassembly');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected vector code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),
        'sources':{name:hashlib.sha256((driver/name).read_bytes()).hexdigest() for name in ['Queue.c','Queue.h','TestQueue.c']},
        'callbacks_modeled':True,'native_worker_connected':False,'hardware':False},indent=2)+'\n');print(message)
if __name__=='__main__':main()
