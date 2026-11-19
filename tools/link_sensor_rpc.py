"""Link actual source-derived FastRPC/readonly service over accepted SSC image.

No owner startup, GLINK registration, mapped arena or real calibration pack.
Existing input-sleep/SSC/haptics/RTC/camera and all recovery remain exact inputs.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','rpc-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','rpc_build','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.rpc_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Parent/RPC incomplete')
    for n in ('native','kernel'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Inherited image changed')
    for n,row in built['objects'].items():
        if sha(a.rpc_build/n)!=row['sha256']:raise ValueError('RPC object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(a.rpc_build/n)):raise ValueError('Automatic RPC startup')
    library=a.rpc_build/'libEldenSensorRpc.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('RPC archive changed')
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(library)]
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original output retained')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inputs.items():
        if sha(Path(n))!=d:raise ValueError('Existing owner object/archive changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={k:prior[k] for k in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd')}
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':count,'inherited_inputs':inputs,'rpc_build_report_sha256':sha(a.rpc_build/'result.json'),'readonly_RPC_core_provider_binding':True,
        'actual_GLINK_DMA_provider':False,'actual_OEM_registry_calibration_pack':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: source-derived FastRPC/readonly pack/provider full link; original SSC/input-sleep/peripherals/Apple30 retained; no startup')
if __name__=='__main__':main()
