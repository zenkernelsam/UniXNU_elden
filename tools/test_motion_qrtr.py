"""Run actual existing QRTR owner/client/SSC methods and ARM64 object builds."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
from replay_xnu_objects import sections, global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','base-native','llvm','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('source','base_native','llvm'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared QRTR source changed: '+name)
    def run(command,label,cwd=None,timeout=90):
        result=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=timeout)
        (out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; original log retained')
        return result.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    for name in ('ShengQrtr','QcomQrtr','QrtrPorts','QrtrTransport'):
        run(['xcrun','clang','-std=c11',*flags,'-c',a.source/(name+'.c'),'-o',out/(name+'-host.o')],name+'-host')
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestQrtrPorts.cpp',
         *[out/(name+'-host.o') for name in ('ShengQrtr','QcomQrtr','QrtrPorts','QrtrTransport')],'-o',out/'test'],'host-build')
    cases=('legacy','qualified','flow-debt','callbacks','send-race','retirement','server-generation','stale-server-delete',
           'link-change','foreign-hello','rx-boundary','malformed','capacity','adapter','stale-identity','pad-overflow')
    results={name:run([out/'test',name],name,timeout=25) for name in cases}
    checks=sum(int(re.search(r': (\d+) checks',value)[1]) for value in results.values())
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/AdspProbe.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Original qualified headers changed')
    flags=[];i=0
    while i<len(job['arguments']):
        value=job['arguments'][i]
        if value in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if value==job['file'] or value in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(value));i+=1
    native_command=[*flags,'-I',a.source,'-I',ROOT/'soc/sm8750/include','-Werror','-mgeneral-regs-only',a.source/'AdspProbe.cpp','-o',out/'AdspProbe.o']
    run(native_command,'native-adsp',remap(job['directory']))
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    for name in ('ShengQrtr','QcomQrtr','QrtrPorts','QrtrTransport'):
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
             '-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror',
             '-DKERNEL=1','-I',a.source,'-c',a.source/(name+'.c'),'-o',out/(name+'.o')],'native-'+name)
    objects={}
    for name in ('AdspProbe','ShengQrtr','QcomQrtr','QrtrPorts','QrtrTransport'):
        obj=out/(name+'.o');asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic QRTR startup')
        objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
    result={'passed':True,'scenarios':len(cases),'checks':checks,'case_output':results,'objects':objects,
            'actual_original_QRTR_owner_ports_SSC_adapter':True,'original_ADSP_queue_locks_physical_retirement_modeled':True,
            'source_record_sha256':sha(a.source/'source.json'),'native_command':list(map(str,native_command)),
            'actual_owner_receive_intents_or_retirement_installed':False,'hardware':False,'full_link':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'passed':True,'scenarios':len(cases),'checks':checks,'objects':len(objects),'hardware':False}))
if __name__=='__main__':main()
