"""Exercise exact derived CCI protocol with bounded MMIO/IRQ peers and build ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','core-source','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('CCI source changed')
    for key in ('core_source','base_native','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    core=json.loads((a.core_source/'source.json').read_text())
    for n,d in core['sources'].items():
        if sha(a.core_source/n)!=d:raise ValueError('Sensor core source changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined',
        '-I',a.source,a.source/'Cci.c',DRIVER/'TestCci.c','-o',out/'test'],'host-build');message=run([out/'test'],'host-run')
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source,'-I',a.core_source]
    units=[]
    for path,name in [(a.source/'Cci.c','cci-host.o'),(DRIVER/'TestCciPeer.c','peer-host.o'),(a.core_source/'Core.c','core-host.o'),(a.source/'SensorBinding.c','binding-host.o')]:
        target=out/name;run(['xcrun','clang','-std=c11',*flags,'-c',path,'-o',target],Path(name).stem);units.append(target)
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestNativeCci.cpp',*units,'-o',out/'native-test'],'native-host-build')
    native_message=run([out/'native-test'],'native-host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'CameraCci.o'
    cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Cci.c','-o',obj]
    run(cmd,'native-build');dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Vector/FP CCI refused')
    if any(k.endswith('/__mod_init_func') for k in sections(obj)):raise ValueError('CCI automatic initializer')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Captured source/header differs')
    cxx=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        cxx.append(remap(v));i+=1
    native=out/'NativeCci.o';native_cmd=[*cxx,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'NativeCci.cpp','-o',native]
    r=subprocess.run(list(map(str,native_cmd)),cwd=remap(job['directory']),capture_output=True);(out/'native-platform.log').write_bytes(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Native platform build failed; retained log')
    if b'warning:' in r.stdout+r.stderr:raise ValueError('New native CCI warning refused')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',native],'native-platform-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar native CCI')
    if any(k.endswith('/__mod_init_func') for k in sections(native)):raise ValueError('Native CCI automatic initialization')
    binding=out/'SensorBinding.o';binding_cmd=list(cmd)
    binding_cmd[binding_cmd.index(str(a.source/'Cci.c')) if str(a.source/'Cci.c') in binding_cmd else binding_cmd.index(a.source/'Cci.c')]=a.source/'SensorBinding.c'
    binding_cmd[binding_cmd.index('-o')+1]=binding;run(binding_cmd,'binding-native')
    bdis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',binding],'binding-native-asm')
    bops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',bdis,re.M)
    if not bops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in bops):raise ValueError('Non-scalar camera binding')
    if any(k.endswith('/__mod_init_func') for k in sections(binding)):raise ValueError('Binding automatic initialization')
    run(['ar','rcs',out/'libEldenCameraCci.a',obj,native,binding],'archive')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'object_sha256':sha(obj),'source_record_sha256':sha(a.source/'source.json'),
        'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe],'actual_extracted_CCI_transfers':True,'MMIO_IRQ_power_peers_modeled':True,
        'native_host_output':native_message,'native_CCI_platform_compiled':True,'native_CCI_object_sha256':sha(native),
        'actual_native_methods_core_CCI_composed_with_peers':True,'core_record_sha256':sha(a.core_source/'source.json'),
        'archive_sha256':sha(out/'libEldenCameraCci.a'),'native_imports':[n for n,v in global_symbols(native).items() if not v[0]&0xe],
        'binding_object_sha256':sha(binding),'binding_imports':[n for n,v in global_symbols(binding).items() if not v[0]&0xe],
        'hardware':False,'native_command':list(map(str,cmd)),'native_platform_command':list(map(str,native_cmd))},indent=2)+'\n');print(message+native_message)
if __name__=='__main__':main()
