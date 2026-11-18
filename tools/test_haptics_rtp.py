"""Compile/test the actual source-derived AW/GENI/RTP composition, no tablet."""
from pathlib import Path
import argparse,hashlib,json,subprocess,re
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','firmware','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    r=json.loads((a.source/'source.json').read_text());spec=r['upstream']['firmware']
    if sha(a.firmware)!=spec['sha256']:raise ValueError('Original firmware changed')
    for name,digest in r['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared source changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    files=[a.source/n for n in ['GeniCore.c','Bus.c','AwChip.c','RtpStream.c']]
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,*files,ROOT/'drivers/EldenHaptics/TestRtp.c','-o',out/'test'],'host-build')
    msg=run([out/'test',a.firmware],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();objects={}
    for src in files:
        obj=out/(src.stem+'.o');run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
            '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',src,'-o',obj],src.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
        objects[src.name]=sha(obj)
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'objects':objects,'source_record_sha256':sha(a.source/'source.json'),
        'hardware_modeled':True,'native_RTP_profile_connected':False,'waveform_semantics_qualified':False,'hardware':False},indent=2)+'\n');print(msg)
if __name__=='__main__':main()
