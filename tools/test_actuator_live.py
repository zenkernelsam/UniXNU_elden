"""Test actual realtime ring/LUT/GENI/AW bridge; native OS providers remain separate."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['source','haptics-source','firmware','llvm','output']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.haptics_source=a.haptics_source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    records={}
    for root in [a.source,a.haptics_source]:
        r=json.loads((root/'source.json').read_text());records[str(root)]=sha(root/'source.json')
        for name,digest in r['sources'].items():
            if sha(root/name)!=digest:raise ValueError('Input changed')
    fw=json.loads((a.haptics_source/'source.json').read_text())['upstream']['firmware']
    if sha(a.firmware)!=fw['sha256']:raise ValueError('Original firmware changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    h=a.haptics_source/'haptics' if (a.haptics_source/'haptics').is_dir() else a.haptics_source
    sources=[a.source/'Live.c',a.source/'Ring.c',a.source/'SampleMap.c']+[h/n for n in ['AwChip.c','GeniCore.c','Bus.c']]
    includes=['-I',a.source,'-I',h]
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*includes,*sources,DRIVER/'actuator/TestLive.c','-o',out/'test'],'host-build')
    message=run([out/'test',a.firmware],'host-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'ActuatorLive.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror',*includes,'-c',a.source/'Live.c','-o',obj],'native-build')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar live bridge')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'object_sha256':sha(obj),'source_records':records,
        'actual_ring_LUT_GENI_AW_composed':True,'physical_OS_peers_modeled':True,'native_Actuator_provider_connected':False,'hardware':False},indent=2)+'\n');print(message)
if __name__=='__main__':main()
