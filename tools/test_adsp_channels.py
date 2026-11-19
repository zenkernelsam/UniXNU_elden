"""Execute actual original ADSP registry/GLINK close methods and compile ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','base-native','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('source','base_native','llvm'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Source input changed: '+n)
    def run(cmd,label,cwd=None):
        result=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; original output retained')
        return result.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run(['xcrun','clang','-std=c11',*flags,'-c',a.source/'QcomGlink.c','-o',out/'Glink-host.o'],'glink-host')
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestAdspChannels.cpp',out/'Glink-host.o','-o',out/'test'],'host-build')
    host=run([out/'test'],'host-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/AdspProbe.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Original capture source changed')
    native_flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        native_flags.append(remap(v));i+=1
    # Exact local generated headers take priority over inherited include roots.
    adsp=out/'AdspProbe.o';command=[*native_flags,'-I',a.source,'-I',ROOT/'soc/sm8750/include','-Werror','-mgeneral-regs-only',a.source/'AdspProbe.cpp','-o',adsp]
    run(command,'native-adsp',remap(job['directory']))
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();glink=out/'QcomGlink.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
      '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-DKERNEL=1','-I',a.source,'-c',a.source/'QcomGlink.c','-o',glink],'native-glink')
    objects={}
    for obj in (adsp,glink):
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],obj.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic transport initialization')
        objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
    r={'passed':True,'host_output':host,'objects':objects,'source_record_sha256':sha(a.source/'source.json'),
       'actual_original_registry_and_GLlNK_methods':True,'SMEM_locks_scheduling_doorbells_modeled':True,'hardware':False,
       'physical_DSP_DMA_stop':False,'full_link':False,'native_command':list(map(str,command))}
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(host)
if __name__=='__main__':main()
