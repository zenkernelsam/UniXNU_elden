"""Execute actual SSC client/codec under sanitizers and compile scalar ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','motion-core','llvm','base-native','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.motion_core=a.motion_core.resolve(strict=True);a.base_native=a.base_native.resolve(strict=True);a.llvm=a.llvm.resolve(strict=True)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);r=json.loads((a.source/'source.json').read_text())
    for n in ('Orientation.c','Orientation.h'):
        if n not in r['sources']:raise ValueError('Orientation source omitted from SSC set: '+n)
    for n,d in r['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('SSC source changed: '+n)
    core=json.loads((a.motion_core/'source.json').read_text())
    for n,d in core['sources'].items():
        if sha(a.motion_core/n)!=d:raise ValueError('Motion core changed')
    def run(cmd,label):
        result=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; retained original log')
        return result.stdout.decode()
    sources=[a.source/n for n in ('Ssc.c','SscCodec.c','SscWire.c','SscPb.c')]
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,
         *sources,a.motion_core/'Core.c',DRIVER/'TestSsc.c','-o',out/'test'],'host-build')
    host=run([out/'test'],'host-run')
    host_flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    host_objects=[]
    for source in [*sources,a.motion_core/'Core.c']:
        obj=out/(source.stem+'-host.o');run(['xcrun','clang','-std=c11',*host_flags,'-c',source,'-o',obj],source.stem+'-host');host_objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*host_flags,DRIVER/'TestNativeSsc.cpp',*host_objects,'-o',out/'native-test'],'native-host-build')
    native_host=run([out/'native-test'],'native-host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();objects={}
    for source in [*sources,a.source/'Orientation.c']:
        obj=out/(source.stem+'.o')
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
             '-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',source,'-o',obj],source.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],source.stem+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected FP/SIMD')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic sensor startup')
        objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
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
    obj=out/'NativeSsc.o';command=[*flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'NativeSsc.cpp','-o',obj]
    result=subprocess.run(list(map(str,command)),cwd=remap(job['directory']),capture_output=True)
    (out/'worker-native.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError('worker-native failed; retained original log')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'worker-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Native worker FP/SIMD')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic worker initialization')
    objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenMotionSsc.a',*[out/n for n in objects]],'archive')
    result={'passed':True,'host_output':host,'native_host_output':native_host,'objects':objects,'archive_sha256':sha(out/'libEldenMotionSsc.a'),
            'source_record_sha256':sha(a.source/'source.json'),'motion_core_record_sha256':sha(a.motion_core/'source.json'),
            'actual_methods_executed':True,'transport_power_clock_HID_modeled':True,'hardware':False,'full_link':False,
            'shared_QRTR_provider_installed':False,'fake_samples_in_native_input':False,'native_command':list(map(str,command))}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(host+native_host)
if __name__=='__main__':main()
