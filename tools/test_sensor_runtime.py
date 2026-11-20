"""Execute actual native sensor runtime lifecycle and compile changed ARM64 units."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import global_symbols,sections
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','base-native','llvm','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('source','base_native','llvm'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Runtime source changed: '+name)
    def run(command,label,cwd=None):
        result=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=90)
        (out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; original log retained')
        return result.stdout.decode()
    host=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run([*host,DRIVER/'TestSensorRuntime.cpp','-o',out/'test'],'host-build')
    cases=('reject-config','early-stop','memory-fail','qrtr-fail','endpoint-fail','binding-fail','attach-fail','thread-fail',
           'pack-not-ready','normal','stop-during-wait','observer-wake','motion-fail','retirement-fail','listener-cleanup','stop-during-motion-start','adopt','adopt-mismatch','adopt-not-ready')
    results={name:run([out/'test',name],name) for name in cases}
    checks=sum(int(re.search(r': (\d+) checks',value)[1]) for value in results.values())
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Original XNU headers changed')
    flags=[];i=0
    while i<len(job['arguments']):
        value=job['arguments'][i]
        if value in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if value==job['file'] or value in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(value));i+=1
    objects={}
    for name in ('SensorRuntimeNative','NativeSsc'):
        obj=out/(name+'.o');command=[*flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/(name+'.cpp'),'-o',obj]
        run(command,'native-'+name,remap(job['directory']));objects[obj.name]={'sha256':sha(obj),'native_command':list(map(str,command))}
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'MemoryBinding.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
         '-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,
         '-c',a.source/'MemoryBinding.c','-o',obj],'native-MemoryBinding');objects[obj.name]={'sha256':sha(obj)}
    for name,row in objects.items():
        obj=out/name;text=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],obj.stem+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',text,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP in native runtime')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic runtime initializer')
        row['imports']=[n for n,v in global_symbols(obj).items() if not v[0]&0xe]
    result={'passed':True,'cases':results,'scenarios':len(cases),'checks':checks,'objects':objects,
        'source_record_sha256':sha(a.source/'source.json'),'actual_native_runtime_methods':True,
        'lower_ADSP_SMMU_pack_SSC_OS_provider_peers_modeled':True,'hardware':False,'full_link':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'passed':True,'scenarios':len(cases),'checks':checks,'objects':len(objects)}))
if __name__=='__main__':main()
