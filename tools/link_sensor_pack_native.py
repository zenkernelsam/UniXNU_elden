"""Link actual sealed pack cache/RPC binding over latest accepted sensor image."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','pack-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','pack_build','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.pack_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Parent or pack incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for n,row in built['objects'].items():
        if sha(a.pack_build/n)!=row['sha256']:raise ValueError('New native pack object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(a.pack_build/n)):raise ValueError('Automatic pack initialization')
    library=a.pack_build/'libEldenSensorPack.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('Pack archive changed')
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(library)]
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited object/archive changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];command=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:command+=['--root',n]
    for n in plan['early_security_initializers']:command+=['--early-security-init',n]
    run(command,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={k:prior[k] for k in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd')}
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
                  'original_regions_preserved':count,'inherited_inputs':inputs,'native_pack_build_report_sha256':sha(a.pack_build/'result.json'),
                  'actual_native_VFS_hash_cache_and_RPC_binding':True,'original_rpc_readonly_pack_inputs_retained':True,'actual_Android_capture_identity_or_pack':False,
                  'native_GLINK_DMA_provider_installed':False,'hardware':False,'boot_packaged':False,
                  'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual native sealed pack cache/RPC binding full link; Apple30/all existing owners retained; no default pack/startup')
if __name__=='__main__':main()
