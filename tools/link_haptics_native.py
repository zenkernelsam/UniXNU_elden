"""Link haptics over the RTC/pen image, preserving all other native/core inputs.

Replace the exact touch platform/worker and recovery objects, never add duplicate
owners. This is an offline kernel link; no boot packaging or device operation.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defs(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','base-native','pen-build','haptics-build','recovery-parent','prelink-tool','original-kernelcache','output'):
        p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','base_native','pen_build','haptics_build','recovery_parent','prelink_tool','original_kernelcache'):
        setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.haptics_build/'result.json')
    if not prior['passed'] or not built['compiled']:raise ValueError('RTC/haptics build incomplete')
    for name in ('native','kernel'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Parent image changed')
    for name,row in built['objects'].items():
        if sha(a.haptics_build/(Path(name).stem+'.o'))!=row['object_sha256']:raise ValueError('Haptics object changed')
    library=a.haptics_build/'libEldenHaptics.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('Haptics archive changed')
    if sha(a.haptics_build/'EldenNvtPlatform.o')!=built['touch_platform_sha256']:raise ValueError('Touch platform changed')
    command=read(a.parent/'link-command.json');inherited={}
    for arg in command:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inherited[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inherited[line]=sha(Path(line))
    def run(cmd,label,cwd=None):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; inspect original log')
    def replace_archive(suffix,replacements,label):
        args=[arg for arg in command if arg.startswith('-Wl,-force_load,') and arg.endswith('/'+suffix)]
        if len(args)!=1:raise ValueError('One archive required: '+suffix)
        old=Path(args[0].split(',',2)[2]);names=[n for n in subprocess.check_output(['ar','t',old],text=True).splitlines() if not n.startswith('__.SYMDEF')]
        if len(set(names))!=len(names) or not set(replacements)<=set(names):raise ValueError('Archive membership changed')
        folder=out/label;folder.mkdir();records={}
        for name in names:
            if Path(name).name!=name:raise ValueError('Unexpected member path')
            data=subprocess.check_output(['ar','p',old,name]);before=hashlib.sha256(data).hexdigest()
            if name in replacements:
                new,expected=replacements[name];original=expected.read_bytes()
                if data!=original and data!=original+b'\n'*(-len(original)%8):raise ValueError('Original member differs: '+name)
                data=new.read_bytes()
            (folder/name).write_bytes(data);records[name]={'before':before,'after':hashlib.sha256(data).hexdigest()}
        archive=folder/suffix;run(['ar','rcs',archive,*names],label+'-archive',folder)
        for name in names:
            data=(folder/name).read_bytes();stored=subprocess.check_output(['ar','p',archive,name])
            if stored!=data and stored!=data+b'\n'*(-len(data)%8):raise ValueError('Archive readback differs')
        command[command.index(args[0])]='-Wl,-force_load,'+str(archive);return records
    cap=read(a.base_native/'capture.json');results=read(a.base_native/'build/results.json')
    matches=[i for i,j in enumerate(cap['jobs']) if j['file'].endswith('/EldenNvt36536/Platform.c')]
    if len(matches)!=1:raise ValueError('One original platform job required')
    old_platform=Path(results[str(matches[0])]['output'])
    if sha(old_platform)!=results[str(matches[0])]['sha256']:raise ValueError('Original touch platform changed')
    new_platform=a.haptics_build/'EldenNvtPlatform.o'
    if defs(old_platform)-defs(new_platform) or defs(new_platform)-defs(old_platform)!={'_elden_touch_shared_gcc_contract','_sm8750_gcc_vote_owner'}:
        raise ValueError('Unexpected shared touch platform exports')
    pen=read(a.pen_build/'result.json');old_runtime=a.pen_build/'EldenTouchRuntime.o'
    if sha(old_runtime)!=pen['objects']['EldenTouchRuntime.cpp']['sha256']:raise ValueError('Original pen runtime changed')
    new_runtime=a.haptics_build/'EldenTouchRuntime.o'
    if defs(new_runtime)!=defs(old_runtime) or set(global_symbols(new_runtime))-set(global_symbols(old_runtime))!={'_elden_haptics_native_start'}:
        raise ValueError('Unexpected touch startup delta')
    display=replace_archive('libNativeLegacyDisplay.late.a',{'EldenNvtPlatform.o':(new_platform,old_platform),'EldenNvtRuntime.o':(new_runtime,old_runtime)},'display')
    old_source=a.parent/'EldenSdamRecovery.inc';text=old_source.read_text();anchor='  int rtcStop=elden_rtc_stop_for_reboot();'
    if text.count(anchor)!=1:raise ValueError('Actual RTC recovery entry differs')
    source=out/'EldenSdamRecovery.inc'
    source.write_text('extern "C" int elden_haptics_stop_for_reboot(void);\n'+text.replace(anchor,
        '  int hapticStop=elden_haptics_stop_for_reboot();\n'
        '  eldenSdamLog(hapticStop?"\\nELDEN_HAPTICS stop unconfirmed; maps retained to reset\\n":"\\nELDEN_HAPTICS stopped; maps retained\\n");\n'+anchor))
    overlay=read(a.parent/'ShengUfs.overlay.json');changed=[]
    def visit(nodes):
        for node in nodes:
            if node.get('type')=='file' and node.get('name','').endswith('/EldenSdamRecovery.inc'):
                if Path(node['external-contents']).read_bytes()!=old_source.read_bytes():raise ValueError('Actual recovery overlay differs')
                node['external-contents']=str(source);changed.append(node['name'])
            if 'contents' in node:visit(node['contents'])
    visit(overlay['roots'])
    if not changed:raise ValueError('Recovery overlay absent')
    vfs=out/'ShengUfs.overlay.json';vfs.write_text(json.dumps(overlay,indent=2)+'\n');cmd=list(prior['recovery_compile_command'])
    cmd[cmd.index('-ivfsoverlay')+1]=str(vfs);cmd[cmd.index('-o')+1]=str(out/'ShengUfs.o')
    cwd=read(a.recovery_parent/'result.json')['native_objects']['ShengUfs']['cwd'];run(cmd,'recovery-compile',cwd)
    old_ufs=a.parent/'ShengUfs.o';new_ufs=out/'ShengUfs.o'
    if sha(old_ufs)!=prior['recovery_object_sha256']:raise ValueError('RTC recovery object changed')
    if defs(old_ufs)!=defs(new_ufs) or set(global_symbols(new_ufs))-set(global_symbols(old_ufs))!={'_elden_haptics_stop_for_reboot'}:
        raise ValueError('Unexpected recovery symbol delta')
    if sections(old_ufs).get('__DATA/__mod_init_func')!=sections(new_ufs).get('__DATA/__mod_init_func'):raise ValueError('Recovery initializers changed')
    ufs=replace_archive('libShengUfs.a',{'ShengUfs.o':(new_ufs,old_ufs)},'ufs')
    command[command.index('-o')+1]=str(out/'native.macho');command+=['-Wl,-force_load,'+str(library)]
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link',out)
    for name,digest in inherited.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited core/archive input changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS image changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
             '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:prelink+=['--root',name]
    for name in plan['early_security_initializers']:prelink+=['--early-security-init',name]
    run(prelink,'prelink',out);metadata=read(out/'kernel.json')
    count=verify_original_executables(out/'kernel.macho',metadata,original['original_executable_sha256'])
    report={'passed':True,'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),
            'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':count,'display_members':display,'ufs_members':ufs,
            'haptics_report_sha256':sha(a.haptics_build/'result.json'),'calibration_present':built['calibration_present'],
            'touch_shared_guard_integrated':True,'startup_hook_integrated':True,'haptics_before_rtc_before_ufs':True,
            'recovery_object_sha256':sha(new_ufs),'recovery_compile_command':cmd,'recovery_compile_cwd':cwd,
            'inherited_inputs':inherited,'hardware':False,'boot_packaged':False,
            'legacy_flags_unchanged':{n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: actual haptics/startup/shared touch/recovery link; all30 original Apple regions preserved')
if __name__=='__main__':main()
