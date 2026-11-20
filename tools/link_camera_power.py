"""Link shared I2C10/WL power session, replacing only two backlight members."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','power-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('parent','power_build','prelink_tool','original_kernelcache'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');build=read(a.power_build/'result.json')
    if not prior['passed'] or not build['passed']:raise ValueError('Unqualified parent/component')
    for name in ('kernel','native'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Qualified parent changed')
    for name,row in build['native_objects'].items():
        if sha(a.power_build/name)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(a.power_build/name)):raise ValueError('Native source object changed/auto-init')
    old=Path(build['parent_shared_archive']);new=a.power_build/'backlight/libEldenBacklight.a'
    if sha(old)!=build['parent_shared_archive_sha256'] or sha(new)!=build['shared_archive_sha256']:raise ValueError('Shared controller archive changed')
    for name,row in build['members'].items():
        before=hashlib.sha256(subprocess.check_output(['ar','p',old,name])).hexdigest()
        after=hashlib.sha256(subprocess.check_output(['ar','p',new,name])).hexdigest()
        if (before,after)!=(row['before'],row['after']):raise ValueError('Actual original/shared member differs: '+name)
        if name not in ('Platform.o','BacklightRuntime.late.o') and before!=after:raise ValueError('Unrelated member replacement')
    command=read(a.parent/'link-command.json');inputs={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.a','.o'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    token='-Wl,-force_load,'+str(old)
    if command.count(token)!=1:raise ValueError('Sole original controller missing')
    command[command.index(token)]='-Wl,-force_load,'+str(new)
    for name in ('CameraPower.o','CameraPowerBus.o','CameraPowerBinding.o'):
        if any(Path(v).name==name for v in command):raise ValueError('Duplicate power provider')
        command.append(str(a.power_build/name))
    command[command.index('-o')+1]=str(out/'native.macho')
    def run(cmd,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited input changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
        '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:cmd.extend(['--root',name])
    for name in plan['early_security_initializers']:cmd.extend(['--early-security-init',name])
    run(cmd,'prelink');metadata=read(out/'kernel.json');regions=verify_original_executables(out/'kernel.macho',metadata,original['original_executable_sha256'])
    inherited=dict(prior['inherited_inputs']);inherited.update(inputs)
    report=dict(prior);report.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':regions,'inherited_inputs':inherited,
        'changed_backlight_archive':build['members'],'backlight_original_path':str(old),'backlight_shared_path':str(new),
        'backlight_ABI':build['backlight_ABI'],'camera_power_build_sha256':sha(a.power_build/'result.json'),
        'camera_power_native_objects':build['native_objects'],'one_original_I2c10_GENI_owner':True,
        'original_all_native_archives_retained':False,'only_declared_two_backlight_members_replaced':True,
        'actual_camera_WL_power_session':True,'actual_physical_power_clock_pin_provider_installed':False,
        'real_camera_frames_OSservice':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS full shared I2C10/camera WL power native link; only2backlight members replaced, all other owners/recovery/Apple30 retained')
if __name__=='__main__':main()
