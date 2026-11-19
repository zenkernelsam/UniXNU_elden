"""Execute actual retained FastRPC/listener/pack paths and compile scalar ARM64."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.llvm=a.llvm.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('RPC source changed: '+n)
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; original output retained')
        return r.stdout.decode()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run(['xcrun','clang',*flags,DRIVER/'TestSensorRpc.c',a.source/'RpcPack.c',a.source/'SensorRpc.c','-o',out/'test'],'host-build');host=run([out/'test'],'host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();objects={}
    for n in ('RpcCore.c','RpcPack.c','SensorRpc.c'):
        source=a.source/n;obj=out/(source.stem+'.o')
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
             '-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',source,'-o',obj],source.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],source.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected FP/SIMD')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic sensor RPC startup')
        objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenSensorRpc.a',*[out/n for n in objects]],'archive')
    result={'passed':True,'host_output':host,'objects':objects,'archive_sha256':sha(out/'libEldenSensorRpc.a'),'source_record_sha256':sha(a.source/'source.json'),
            'source_derived_actual_method_tests':True,'transport_DMA_clock_registry_files_modeled':True,'actual_owned_DMA_or_GLINK_provider':False,
            'actual_OEM_registry_calibration_pack':False,'hardware':False,'full_link':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(host)
if __name__=='__main__':main()
