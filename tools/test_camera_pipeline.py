"""Test source-derived Gen4 CAMSS core and compile scalar ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','base-native','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.llvm=a.llvm.resolve(strict=True);a.base_native=a.base_native.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Actual pipeline source changed')
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source]
    run(['xcrun','clang',*flags,a.source/'Pipeline.c',DRIVER/'TestPipeline.c','-o',out/'test'],'host-build')
    host=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'CameraPipeline.o'
    # Native MMIO/GIC adapter's actual methods run against original core.
    run(['xcrun','clang',*flags,'-c',a.source/'Pipeline.c','-o',out/'pipeline-host.o'],'core-host')
    run(['xcrun','clang',*flags,'-DELDEN_CAMERA_PIPELINE_PEER_API=1','-Wno-unused-function','-c',DRIVER/'TestPipeline.c','-o',out/'peer-host.o'],'peer-host')
    run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,
         DRIVER/'TestNativePipeline.cpp',out/'pipeline-host.o',out/'peer-host.o','-o',out/'native-test'],'native-host-build')
    cases=('normal','front','map-fail','attach-fail','deny','unexpected-irq','drain-fail','active-irq')
    native_host={name:run([out/'native-test',name],'native-host-'+name) for name in cases}
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Pipeline.c','-o',obj],'native-build')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Camera core must remain scalar')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic camera startup')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Original native headers changed')
    cxx=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        cxx.append(remap(v));i+=1
    native=out/'NativePipeline.o';command=[*cxx,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'NativePipeline.cpp','-o',native]
    r=subprocess.run(list(map(str,command)),cwd=remap(job['directory']),capture_output=True);(out/'native-platform.log').write_bytes(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Native pipeline build failed; original log retained')
    binding=out/'CaptureBinding.o';run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'CaptureBinding.c','-o',binding],'binding-native')
    for path in (native,binding):
        asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',path],path.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Native camera code must remain scalar')
        if any(n.endswith('/__mod_init_func') for n in sections(path)):raise ValueError('Native camera automatic initializer')
    result={'passed':True,'host_output':host,'object_sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe],
        'native_host_cases':native_host,
        'native_objects':{path.name:{'sha256':sha(path),'imports':[n for n,v in global_symbols(path).items() if not v[0]&0xe]} for path in (obj,native,binding)},
        'source_record_sha256':sha(a.source/'source.json'),'actual_PHY_CSID_VFE_queue_methods':True,'hardware':False,'MMIO_power_DMA_IRQ_peers_modeled':True,'full_link':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(host)
if __name__=='__main__':main()
