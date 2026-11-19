"""Link owned wake IRQ/flow library over the current fixed Actuator/RTC/pen image.

No touch startup/PM/worker hooks are added here. Original GPIO/SPI consumers and
all Apple regions remain inherited. This is a component link, not a wake feature.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections,global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','wake-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','wake_build','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');wake=read(a.wake_build/'result.json')
    if not prior['passed'] or not wake['passed']:raise ValueError('Previous component incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for n,row in wake['objects'].items():
        if sha(a.wake_build/n)!=row['sha256']:raise ValueError('Wake object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(a.wake_build/n)):raise ValueError('Automatic wake initialization refused')
    library=a.wake_build/'libEldenWake.a'
    if sha(library)!=wake['archive_sha256']:raise ValueError('Wake archive changed')
    cmd=read(a.parent/'link-command.json');inherited={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inherited[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inherited[line]=sha(Path(line))
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(library)]
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inherited.items():
        if sha(Path(n))!=d:raise ValueError('Inherited input changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS image changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
         '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:cmd+=['--root',n]
    for n in plan['early_security_initializers']:cmd+=['--early-security-init',n]
    run(cmd,'prelink');meta=read(out/'kernel.json')
    count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    # The RTC audit needs the actual unchanged recovery compile identity.
    report={'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':count,
        'wake_report_sha256':sha(a.wake_build/'result.json'),'inherited_inputs':inherited,
        'recovery_object_sha256':prior['recovery_object_sha256'],'recovery_compile_command':prior['recovery_compile_command'],
        'recovery_compile_cwd':prior['recovery_compile_cwd'],'native_GPIO_PDC_library_linked':True,
        'touch_worker_PM_hooked':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: wake IRQ/flow component link; all30 Apple regions and original owners retained; no automatic wake hooks')
if __name__=='__main__':main()
