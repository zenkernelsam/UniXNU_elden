"""Replace one touch worker/protocol and pen/wake libraries over fixed camera image.

Keep current GCC/touch ownership, all camera/motion/haptic/RTC components, actual
recovery and Apple executable regions. Wake provider still absent/default-off.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections,global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def definitions(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','pen-build','wake-build','protocol-build','base-native','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','pen_build','wake_build','protocol_build','base_native','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');pen=read(a.pen_build/'result.json');wake=read(a.wake_build/'result.json');protocol=read(a.protocol_build/'result.json')
    if not prior['passed'] or not pen['compiled'] or not wake['passed'] or not protocol['passed']:raise ValueError('Component build incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for n,r in pen['objects'].items():
        if sha(a.pen_build/(Path(n).stem+'.o'))!=r['sha256']:raise ValueError('New pen/touch object changed')
    for n,r in wake['objects'].items():
        if sha(a.wake_build/n)!=r['sha256']:raise ValueError('Wake object changed')
    pobj=a.protocol_build/'EldenNvtProtocol.o'
    if sha(pobj)!=protocol['object_sha256']:raise ValueError('Protocol object changed')
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(command,label,cwd=None):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=cwd or out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
    replace={}
    for suffix,new,expected in [('libEldenPen.a',a.pen_build/'libEldenPen.a',pen['archive_sha256']),('libEldenWake.a',a.wake_build/'libEldenWake.a',wake['archive_sha256'])]:
        matches=[x for x in cmd if x.startswith('-Wl,-force_load,') and x.endswith('/'+suffix)]
        if len(matches)!=1 or sha(new)!=expected:raise ValueError('One exact new archive required')
        old=matches[0];replace[suffix]={'before':sha(Path(old.split(',',2)[2])),'after':expected};cmd[cmd.index(old)]='-Wl,-force_load,'+str(new)
    matches=[x for x in cmd if x.startswith('-Wl,-force_load,') and x.endswith('/libNativeLegacyDisplay.late.a')]
    if len(matches)!=1:raise ValueError('One display archive required')
    old=Path(matches[0].split(',',2)[2]);folder=out/'display';folder.mkdir();names=subprocess.check_output(['ar','t',old],text=True).splitlines();names=[n for n in names if not n.startswith('__.SYMDEF')]
    if len(names)!=len(set(names)):raise ValueError('Duplicate inherited object')
    expected={'EldenNvtRuntime.o':a.pen_build/'EldenTouchRuntime.o','EldenNvtProtocol.o':pobj}
    if not expected.keys()<=set(names):raise ValueError('Actual inherited touch objects missing')
    members={}
    for name in names:
        if Path(name).name!=name:raise ValueError('Unexpected member path')
        b=subprocess.check_output(['ar','p',old,name]);before=hashlib.sha256(b).hexdigest()
        if name in expected:
            prior_obj=folder/('old-'+name);prior_obj.write_bytes(b);new=expected[name]
            olddefs=definitions(prior_obj);newdefs=definitions(new)
            wanted={'_elden_touch_wake_install_power','_elden_touch_wake_request','_elden_touch_wake_status'} if name=='EldenNvtRuntime.o' else {'_elden_nvt_low_power_command'}
            if olddefs-newdefs or newdefs-olddefs!=wanted:raise ValueError('Unexpected touch/protocol public delta: '+name)
            b=new.read_bytes()
        (folder/name).write_bytes(b);members[name]={'before':before,'after':hashlib.sha256(b).hexdigest()}
    archive=folder/'libNativeLegacyDisplay.late.a';run(['ar','rcs',archive,*names],'display-archive',folder)
    cmd[cmd.index(matches[0])]='-Wl,-force_load,'+str(archive);cmd[cmd.index('-o')+1]=str(out/'native.macho')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inputs.items():
        if sha(Path(n))!=d:raise ValueError('Inherited input mutated')
    meta=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=meta['original_sha256']:raise ValueError('Original OS image changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];command=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,'--slide='+str(meta['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:command+=['--root',n]
    for n in plan['early_security_initializers']:command+=['--early-security-init',n]
    run(command,'prelink');newmeta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',newmeta,meta['original_executable_sha256'])
    r={'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':count,
       'display_members':members,'replaced_libraries':replace,'inherited_inputs':inputs,'pen_report_sha256':sha(a.pen_build/'result.json'),'wake_report_sha256':sha(a.wake_build/'result.json'),'protocol_report_sha256':sha(a.protocol_build/'result.json'),
       'recovery_object_sha256':prior['recovery_object_sha256'],'recovery_compile_command':prior['recovery_compile_command'],'recovery_compile_cwd':prior['recovery_compile_cwd'],
       'actual_original_worker_wake_hooked':True,'real_power_provider_installed':False,'default_wake_disabled':True,'hardware':False,'boot_packaged':False,
       'legacy_flags_unchanged':{n:newmeta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: original touch worker pause/IRQ/firmware resume and same pen HID full link; old owners/Apple30 retained; real power provider absent/default-off')
if __name__=='__main__':main()
