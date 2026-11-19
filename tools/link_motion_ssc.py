"""Link owned native SSC client over latest input-sleep image, preserving owners.

Installs recovery stop before haptics/RTC/UFS. Shared QRTR/board/PD providers
remain absent, so no sensor auto-start, samples or orientation claim follows.
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
    for key in ('parent','ssc-build','motion-core-build','recovery-source','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','ssc_build','motion_core_build','recovery_source','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.ssc_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Parent/SSC incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for n,row in built['objects'].items():
        if sha(a.ssc_build/n)!=row['sha256']:raise ValueError('SSC object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(a.ssc_build/n)):raise ValueError('Automatic sensor initialization')
    library=a.ssc_build/'libEldenMotionSsc.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('SSC archive changed')
    core=read(a.motion_core_build/'result.json');core_obj=a.motion_core_build/'MotionCore.o'
    if not core['passed'] or sha(core_obj)!=core['object_sha256'] or core['source_record_sha256']!=built['motion_core_record_sha256']:raise ValueError('Actual SSC sample core input differs')
    if any(k.endswith('/__mod_init_func') for k in sections(core_obj)):raise ValueError('Automatic motion core initialization')
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(command,label,cwd=None):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=cwd or out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; retained original log')
    old_source=a.recovery_source/'EldenSdamRecovery.inc';text=old_source.read_text();anchor='  int hapticStop=elden_haptics_stop_for_reboot();'
    if text.count(anchor)!=1:raise ValueError('Recovery entry changed')
    recovery=out/'EldenSdamRecovery.inc';recovery.write_text('extern "C" int elden_motion_native_ssc_recovery_stop(void);\n'+text.replace(anchor,
        '  int motionStop=elden_motion_native_ssc_recovery_stop();\n'
        '  eldenSdamLog(motionStop?"\\nELDEN_MOTION stopped; providers retained to reset\\n":"\\nELDEN_MOTION stop unconfirmed; providers retained to reset\\n");\n'+anchor))
    overlay=read(a.recovery_source/'ShengUfs.overlay.json');changes=[]
    def visit(nodes):
        for node in nodes:
            if node.get('type')=='file' and node.get('name','').endswith('/EldenSdamRecovery.inc'):
                if Path(node['external-contents']).read_bytes()!=old_source.read_bytes():raise ValueError('Actual recovery overlay differs')
                node['external-contents']=str(recovery);changes.append(node['name'])
            if 'contents' in node:visit(node['contents'])
    visit(overlay['roots'])
    if not changes:raise ValueError('Recovery overlay absent')
    vfs=out/'ShengUfs.overlay.json';vfs.write_text(json.dumps(overlay,indent=2)+'\n');compile=list(prior['recovery_compile_command'])
    if Path(compile[compile.index('-ivfsoverlay')+1]).read_bytes()!=(a.recovery_source/'ShengUfs.overlay.json').read_bytes():raise ValueError('Current recovery command/input differs')
    compile[compile.index('-ivfsoverlay')+1]=str(vfs);compile[compile.index('-o')+1]=str(out/'ShengUfs.o')
    run(compile,'recovery-compile',prior['recovery_compile_cwd']);old=a.recovery_source/'ShengUfs.o';new=out/'ShengUfs.o'
    if sha(old)!=prior['recovery_object_sha256']:raise ValueError('Recovery parent object changed')
    if definitions(old)!=definitions(new) or set(global_symbols(new))-set(global_symbols(old))!={'_elden_motion_native_ssc_recovery_stop'}:raise ValueError('Unexpected recovery symbol delta')
    if sections(old).get('__DATA/__mod_init_func')!=sections(new).get('__DATA/__mod_init_func'):raise ValueError('Recovery initializer count changed')
    matches=[v for v in cmd if v.startswith('-Wl,-force_load,') and v.endswith('/libShengUfs.a')]
    if len(matches)!=1:raise ValueError('One original UFS archive required')
    archive=Path(matches[0].split(',',2)[2]);folder=out/'ufs';folder.mkdir()
    names=subprocess.check_output(['ar','t',archive],text=True).splitlines();names=[n for n in names if not n.startswith('__.SYMDEF')]
    if len(names)!=len(set(names)) or 'ShengUfs.o' not in names:raise ValueError('Unexpected UFS member list')
    members={}
    for name in names:
        if Path(name).name!=name:raise ValueError('Unexpected member path')
        b=subprocess.check_output(['ar','p',archive,name]);before=hashlib.sha256(b).hexdigest()
        if name=='ShengUfs.o':
            if before!=sha(old):raise ValueError('Actual archived UFS differs')
            b=new.read_bytes()
        (folder/name).write_bytes(b);members[name]={'before':before,'after':hashlib.sha256(b).hexdigest()}
    linked=folder/'libShengUfs.a';run(['ar','rcs',linked,*names],'ufs-archive',folder)
    cmd[cmd.index(matches[0])]='-Wl,-force_load,'+str(linked);cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=[str(core_obj),'-Wl,-force_load,'+str(library)]
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inputs.items():
        if sha(Path(n))!=d:raise ValueError('Inherited input changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':count,'inherited_inputs':inputs,'ufs_members':members,'display_members':prior['display_members'],
        'ssc_build_report_sha256':sha(a.ssc_build/'result.json'),'motion_core_object_sha256':sha(core_obj),'recovery_object_sha256':sha(new),'recovery_compile_command':compile,'recovery_compile_cwd':prior['recovery_compile_cwd'],
        'native_manager_HID_calls_linked':True,'shared_QRTR_provider_installed':False,'real_board_clock_mount_provider_installed':False,
        'sensor_PD_file_service_installed':False,'device_orientation_service':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: owned SSC worker/strict attributes/HID/recovery full link; all Apple30 and existing input-sleep/peripherals retained')
if __name__=='__main__':main()
