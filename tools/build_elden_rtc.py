"""Build native RTC, source-derived platform and shared gate in real XNU context."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.base_native=a.base_native.resolve(strict=True);a.llvm=a.llvm.resolve(strict=True)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);cap=json.loads((a.base_native/'capture.json').read_text());record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('RTC source changed')
    job=next(x for x in cap['jobs'] if x['file'].endswith('/ArmDTPlatform.cpp'));old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Captured source/header changed')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    def run(cmd,name,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; inspect retained compile log')
        return r.stdout.decode()
    records={};resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    for name in ('ArmDTPlatform.cpp','Native.cpp','Core.c','Spmi.c','TransactionGate.c'):
        source=a.source/name;obj=out/(source.stem+'.o')
        if source.suffix=='.cpp':cmd=[*flags,'-I',str(a.source),'-Werror','-mgeneral-regs-only',str(source),'-o',str(obj)];cwd=remap(job['directory'])
        else:cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
                  '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',source,'-o',obj];cwd=None
        run(cmd,source.stem+'-compile',cwd);dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],source.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar RTC code')
        symbols=global_symbols(obj);records[name]={'source_sha256':sha(source),'object_sha256':sha(obj),'imports':[n for n,v in symbols.items() if not v[0]&0xe],
                'exports':[n for n,v in symbols.items() if v[0]&0xe],'command':list(map(str,cmd)),'sections':{k:len(v) for k,v in sections(obj).items()}}
    run(['ar','rcs',out/'libEldenRtc.a',out/'Native.o',out/'Core.o',out/'Spmi.o'],'archive')
    (out/'result.json').write_text(json.dumps({'compiled':True,'objects':records,'archive_sha256':sha(out/'libEldenRtc.a'),
        'source_record_sha256':sha(a.source/'source.json'),'separate_replacements':['ArmDTPlatform.o','TransactionGate.o'],
        'persistent_clock_verified':False,'hardware':False,'full_link':False},indent=2)+'\n');print('PASS: RTC native/actual platform and shared gate compile; full link pending')
if __name__=='__main__':main()
