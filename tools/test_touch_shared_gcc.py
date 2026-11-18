"""Test the actual revised touch platform with the captured original GENI core."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.base_native=a.base_native.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(x for x in cap['jobs'] if x['file'].endswith('/GeniSpi.c'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    geni=Path(remap(job['file']));header=geni.with_suffix('.h')
    for original in (job['file'],str(Path(job['file']).with_suffix('.h'))):
        if sha(Path(remap(original)))!=cap['files'][original]['sha256']:raise ValueError('Original GENI input changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout.decode()
    driver=ROOT/'drivers/EldenNvt36536'
    include=['-I',driver,'-I',ROOT/'package','-I',ROOT/'soc/sm8750/include','-I',geni.parent]
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*include,
         driver/'Platform.c',geni,driver/'TestPlatform.c','-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'EldenNvtPlatform.o'
    cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror',*include,'-c',driver/'Platform.c','-o',obj]
    run(cmd,'native-build');dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected vector code')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'object_sha256':sha(obj),
        'sources':{str(p.relative_to(ROOT)):sha(p) for p in [driver/'Platform.c',driver/'Platform.h',driver/'TestPlatform.c']},
        'original_geni_sha256':sha(geni),'command':list(map(str,cmd)),'hardware':False,'MMIO_peers_modeled':True},indent=2)+'\n');print(msg)
if __name__=='__main__':main()
