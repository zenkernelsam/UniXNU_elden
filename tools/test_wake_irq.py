"""Build/test exact owned GPIO162/PDC109 route and compile native XNU adapter."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenWake'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        DRIVER/'Irq.c',DRIVER/'TestIrq.c','-o',out/'test'],'host-build');message=run([out/'test'],'host-run')
    flags_host=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
    run(['xcrun','clang','-std=c11',*flags_host,'-c',DRIVER/'Irq.c','-o',out/'irq-host.o'],'irq-host')
    run(['xcrun','clang++','-std=c++17',*flags_host,'-I',DRIVER,DRIVER/'TestNativeIrq.cpp',out/'irq-host.o','-o',out/'native-test'],'native-host-build')
    native_message=run([out/'native-test'],'native-host-run')
    run(['xcrun','clang','-std=c11',*flags_host,'-I',DRIVER,
        DRIVER/'Flow.c',DRIVER/'Gesture.c',DRIVER/'Irq.c',ROOT/'drivers/EldenNvt36536/Protocol.c',DRIVER/'TestFlow.c','-o',out/'flow-test'],'flow-build')
    flow_message=run([out/'flow-test'],'flow-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Captured input differs')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    cpp=out/'NativeIrq.o';cmd=[*flags,'-mgeneral-regs-only','-Werror','-Wno-error=incompatible-sysroot','-I',DRIVER,DRIVER/'NativeIrq.cpp','-o',cpp]
    run(cmd,'native-build',remap(job['directory']))
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'Irq.o';flow=out/'Flow.o';gesture=out/'Gesture.o'
    for source,target in [(DRIVER/'Irq.c',obj),(DRIVER/'Flow.c',flow),(DRIVER/'Gesture.c',gesture)]:
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
            '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-Wno-error=incompatible-sysroot','-c',source,'-o',target],source.stem+'-native')
    objects={}
    for p in (obj,cpp,flow,gesture):
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',p],p.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar IRQ native')
        if any(k.endswith('/__mod_init_func') for k in sections(p)):raise ValueError('Automatic wake initializer')
        objects[p.name]={'sha256':sha(p),'imports':[n for n,v in global_symbols(p).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenWake.a',obj,cpp,flow,gesture],'archive')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'objects':objects,'archive_sha256':sha(out/'libEldenWake.a'),
        'source_sha256':{p.name:sha(p) for p in [DRIVER/'Irq.c',DRIVER/'Irq.h',DRIVER/'NativeIrq.cpp',DRIVER/'NativeIrq.h',DRIVER/'NativeIrqMethods.inc',DRIVER/'TestIrq.c',DRIVER/'TestNativeIrq.cpp',DRIVER/'Flow.c',DRIVER/'Flow.h',DRIVER/'TestFlow.c']},
        'native_command':list(map(str,cmd)),'actual_route_methods':True,'OS_MMIO_GIC_modeled':True,
        'native_host_output':native_message,'actual_native_handler_tested_with_peers':True,
        'flow_host_output':flow_message,'actual_flow_protocol_gesture_route_composition':True,
        'worker_power_resume_connected':False,'full_link':False,'hardware':False},indent=2)+'\n');print(message+native_message+flow_message)
if __name__=='__main__':main()
