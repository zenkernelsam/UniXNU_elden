"""Link concrete sensor IOKit memory/table/RPC binding over accepted pack image.

No new SMMU/GLINK owner startup, fixed PA, route writes or candidate capture.
Existing recovery/input-sleep/camera/haptics/RTC/SSC owners remain exact inputs.
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
    for n in ('parent','memory-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('parent','memory_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.memory_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Parent/memory incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for name,row in built['objects'].items():
        if sha(a.memory_build/name)!=row['sha256']:raise ValueError('Memory object changed')
        if any(n.endswith('/__mod_init_func') for n in sections(a.memory_build/name)):raise ValueError('Automatic memory startup')
    library=a.memory_build/'libEldenSensorMemory.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('Memory archive changed')
    cmd=read(a.parent/'link-command.json');inputs={}
    for argument in cmd:
        path=Path(argument.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(library)]
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Existing owner/object changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={k:prior[k] for k in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd')}
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':count,'inherited_inputs':inputs,'memory_report_sha256':sha(a.memory_build/'result.json'),
        'actual_IOKit_high_PA_to_low_IOVA_and_pack_RPC_binding':True,'actual_SMMU_GLINK_route_provider':False,'actual_Y700_capture':False,
        'hardware':False,'boot_packaged':False,'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual retained sensor memory/4K table/cache/RPC full link; Apple30/all owners retained; no new startup')
if __name__=='__main__':main()
