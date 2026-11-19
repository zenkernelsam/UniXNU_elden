"""Execute actual sensor memory/PAR/table methods and compile scalar ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','base-native','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--pack-source',type=Path,required=True)
    a=p.parse_args()
    for n in ('source','base_native','llvm','pack_source'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Prepared memory input changed')
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source];host=[]
    sources=[a.source/n for n in record['sources'] if n.endswith('.c')]
    original_tables=[p for p in sources if p.name!='MemoryBinding.c']
    for source in sources:
        obj=out/(source.stem+'-host.o');run(['xcrun','clang','-std=c11',*flags,'-c',source,'-o',obj],source.stem+'-host');host.append(obj)
    allocator_objects=[p for p in host if p.name!='MemoryBinding-host.o']
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestNativeMemory.cpp',*allocator_objects,'-o',out/'test'],'host-build');host_output=run([out/'test'],'host-run')
    pack_record=json.loads((a.pack_source/'source.json').read_text())
    for n,d in pack_record['sources'].items():
        if sha(a.pack_source/n)!=d:raise ValueError('Qualified pack source changed')
    stack_objects=list(host)
    for n in ('RpcCore.c','RpcPack.c','SensorRpc.c','PackBinding.c'):
        obj=out/('stack-'+Path(n).stem+'.o');run(['xcrun','clang','-std=c11',*flags,'-I',a.pack_source,'-c',a.pack_source/n,'-o',obj],'stack-'+Path(n).stem);stack_objects.append(obj)
    run(['xcrun','clang++','-std=c++17','-Wno-deprecated-declarations',*flags,'-I',a.pack_source,'-I',DRIVER,'-DELDEN_TEST_MEMORY=1',DRIVER/'TestNativePack.cpp',*stack_objects,'-o',out/'stack-test'],'stack-host-build')
    stack_output=run([out/'stack-test'],'stack-host-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Captured original headers changed')
    native_flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        native_flags.append(remap(v));i+=1
    obj=out/'NativeMemory.o';command=[*native_flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'NativeMemory.cpp','-o',obj]
    run(command,'native-memory',remap(job['directory']));objects=[obj];resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    for source in sources:
        obj=out/(source.stem+'.o');run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
          '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',source,'-o',obj],source.stem+'-native');objects.append(obj)
    result_objects={}
    for obj in objects:
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],obj.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic memory/route startup')
        result_objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenSensorMemory.a',*objects],'archive')
    result={'passed':True,'host_output':host_output,'stack_output':stack_output,'objects':result_objects,'archive_sha256':sha(out/'libEldenSensorMemory.a'),'source_record_sha256':sha(a.source/'source.json'),
            'actual_native_methods_and_4K_tables':True,'IOKit_cache_PAR_shared_MMIO_modeled':True,'shared_SMMU_route_provider_installed':False,'hardware':False,'full_link':False,'native_command':list(map(str,command))}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(host_output+stack_output)
if __name__=='__main__':main()
