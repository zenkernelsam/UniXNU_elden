"""Test actual original camera clock/domain/pin functions and scalar OS17 core."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);report=json.loads((a.source/'source.json').read_text())
    for name,digest in report['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared source changed: '+name)
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; log retained')
        return r.stdout.decode()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source]
    run(['xcrun','clang',*flags,a.source/'Clock.c',ROOT/'drivers/EldenCamera/TestClocks.c','-o',out/'test'],'host-build')
    host=run([out/'test'],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'CameraClocks.o'
    run(['xcrun','clang',*flags,'-c',a.source/'Clock.c','-o',out/'clock-host.o'],'core-host')
    run(['xcrun','clang',*flags,'-DELDEN_CAMERA_CLOCK_PEER_API=1','-Dmain=previous_clock_main','-c',ROOT/'drivers/EldenCamera/TestClocks.c',
         '-o',out/'peer-host.o'],'peer-host')
    run(['xcrun','clang++','-std=c++17',*flags[1:],ROOT/'drivers/EldenCamera/TestNativeClocks.cpp',out/'clock-host.o',out/'peer-host.o',
         '-o',out/'native-test'],'native-host')
    nativecases={name:run([out/'native-test',name],'native-'+name) for name in ('normal','map-fail','irq')}
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Clock.c','-o',obj],'native-build')
    asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Native camera clock core not scalar')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic camera clock initializer')
    record={'passed':True,'host_output':host,'object_sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe],
        'actual_native_method_cases':nativecases,
        'prepared_sha256':sha(a.source/'source.json'),'actual_original_regmap_RCG_branch_GDSC_pin_methods':True,
        'physical_parents_RPMh_regmap_owner_modeled':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(host)
if __name__=='__main__':main()
