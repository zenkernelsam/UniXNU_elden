"""Build current exact NVT protocol for replacing one inherited touch object."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('source','base_native','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);spec=json.loads((a.source/'source.json').read_text())
    for n,d in spec['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Derived sleep source changed')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/EldenNvt36536/Protocol.c'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,r in cap['files'].items():
        if sha(Path(remap(n)))!=r['sha256']:raise ValueError('Captured header/source differs')
    cmd=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MMD','-MP'):i+=1;continue
        cmd.append(remap(v));i+=1
    obj=out/'EldenNvtProtocol.o';cmd+=['-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'Protocol.c','-o',obj]
    r=subprocess.run(list(map(str,cmd)),cwd=remap(job['directory']),capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr)
    if r.returncode or b'warning:' in r.stdout+r.stderr:raise ValueError('Protocol native build failed/warning; retained log')
    r=subprocess.run([str(a.llvm/'llvm-objdump'),'--disassemble','--no-show-raw-insn',str(obj)],capture_output=True,text=True,check=True);(out/'asm.log').write_text(r.stdout)
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',r.stdout,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Vector protocol code')
    if any(k.endswith('/__mod_init_func') for k in sections(obj)):raise ValueError('Protocol automatic initializer')
    (out/'result.json').write_text(json.dumps({'passed':True,'object_sha256':sha(obj),'source_sha256':sha(a.source/'Protocol.c'),
        'source_record_sha256':sha(a.source/'source.json'),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe],
        'hardware':False,'command':list(map(str,cmd))},indent=2)+'\n');print('PASS: current exact NVT low-power/protocol object, scalar and warning-free')
if __name__=='__main__':main()
