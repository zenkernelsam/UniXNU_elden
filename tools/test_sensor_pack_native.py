"""Execute actual native sensor-pack VFS/cache methods and compile scalar ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--host-fixture',type=Path,help='builder positive-host-only folder; no production header allowed')
    a=p.parse_args()
    for key in ('source','base_native','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Sensor-pack source changed')
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    host_flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run(['xcrun','clang','-std=c11',*host_flags,'-c',a.source/'RpcPack.c','-o',out/'pack-host.o'],'pack-host')
    host_objects=[out/'pack-host.o']
    for n in ('RpcCore','SensorRpc','PackBinding'):
        obj=out/(n+'-host.o');run(['xcrun','clang','-std=c11',*host_flags,'-c',a.source/(n+'.c'),'-o',obj],n+'-host');host_objects.append(obj)
    run(['xcrun','clang++','-std=c++17','-Wno-deprecated-declarations',*host_flags,DRIVER/'TestNativePack.cpp',*host_objects,'-o',out/'test'],'host-build')
    host_command=[out/'test']
    if a.host_fixture:
        fixture=a.host_fixture.resolve(strict=True);meta=json.loads((fixture/'record.json').read_text())
        if meta.get('host_fixture') is not True or meta.get('production_header_emitted') is not False or (fixture/'EldenSensorPackInputs.h').exists():raise ValueError('Only explicitly host-only pack fixtures accepted')
        if sha(fixture/'pack.bin')!=meta['seal']['sha256']:raise ValueError('Host fixture pack changed')
        host_command+=[fixture/'pack.bin',fixture/'seal.host-only.bin']
    host=run(host_command,'host-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Captured XNU source differs')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    kernel_header=project/'records/cpu-full-20261111/tree/.cache/xnu-work/bsd'
    if not (kernel_header/'sys/vnode.h').is_file():raise ValueError('Exact BSD vnode headers absent')
    obj=out/'NativePack.o';cmd=[*flags,'-I',kernel_header,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'NativePack.cpp','-o',obj]
    run(cmd,'native-build',remap(job['directory']))
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected FP/SIMD')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic sensor file publication')
    objects={obj.name:{'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}}
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();binding_obj=out/'PackBinding.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'PackBinding.c','-o',binding_obj],'binding-native')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',binding_obj],'binding-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops) or any(n.endswith('/__mod_init_func') for n in sections(binding_obj)):raise ValueError('Binding scalar/init qualification failed')
    objects[binding_obj.name]={'sha256':sha(binding_obj),'imports':[n for n,v in global_symbols(binding_obj).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenSensorPack.a',obj,binding_obj],'archive')
    r={'passed':True,'host_output':host,'objects':objects,
       'archive_sha256':sha(out/'libEldenSensorPack.a'),'source_record_sha256':sha(a.source/'source.json'),'actual_native_methods_tested':True,
       'actual_SHA256_and_source_pack':True,'VFS_transport_identity_peers_modeled':True,'actual_Android_registry_pack':False,'hardware':False,'full_link':False,
       'host_test_command':list(map(str,host_command)),'host_fixture_imported':bool(a.host_fixture),'native_command':list(map(str,cmd))}
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(host)
if __name__=='__main__':main()
