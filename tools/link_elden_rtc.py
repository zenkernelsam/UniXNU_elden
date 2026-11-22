"""Link real RTC/clock providers over a source-built pen kernel.

Replace only the existing platform, shared gate and recovery object. All other
core/archive inputs and original Apple code remain checked. No device operation.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defined(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','base-native','rtc-build','recovery-parent','prelink-tool','original-kernelcache','output'):
        parser.add_argument('--'+key,type=Path,required=True)
    a=parser.parse_args()
    for key in ('parent','base_native','rtc_build','recovery_parent','prelink_tool','original_kernelcache'):
        setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    prior=read(a.parent/'result.json');rtc=read(a.rtc_build/'result.json')
    for name in ('native','kernel'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Parent image changed')
    if not prior['passed'] or not rtc['compiled']:raise ValueError('Incomplete parent/RTC build')
    for name,row in rtc['objects'].items():
        if sha(a.rtc_build/(Path(name).stem+'.o'))!=row['object_sha256']:raise ValueError('RTC object changed: '+name)
    if sha(a.rtc_build/'libEldenRtc.a')!=rtc['archive_sha256']:raise ValueError('RTC archive changed')
    owner_row=rtc['objects'].get('Owner.c')
    if owner_row is None:raise ValueError('RTC build receipt has no Owner.o')
    rtc_members=subprocess.check_output(['ar','t',str(a.rtc_build/'libEldenRtc.a')],text=True).splitlines()
    if rtc_members.count('Owner.o')!=1:raise ValueError('RTC archive dropped owner member')
    member=subprocess.check_output(['ar','p',str(a.rtc_build/'libEldenRtc.a'),'Owner.o'])
    if hashlib.sha256(member).hexdigest()!=owner_row['object_sha256']:raise ValueError('Archived Owner.o bytes differ from build receipt')
    if any(k.endswith('/__mod_init_func') for k in sections(a.rtc_build/'Owner.o')):raise ValueError('Automatic owner initialization')
    command=read(a.parent/'link-command.json');inherited={}
    for arg in command:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inherited[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inherited[line]=sha(Path(line))
    def run(cmd,name,cwd=None):
        with (out/(name+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(name+' failed; original logs retained')
    def replace_archive(suffix,replacements,label):
        arguments=[s for s in command if s.startswith('-Wl,-force_load,') and s.endswith('/'+suffix)]
        if len(arguments)!=1:raise ValueError('Unique archive required: '+suffix)
        old=Path(arguments[0].split(',',2)[2]);names=[n for n in subprocess.check_output(['ar','t',str(old)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
        if len(set(names))!=len(names) or not set(replacements)<=set(names):raise ValueError('Archive membership differs')
        folder=out/label;folder.mkdir();records={}
        for name in names:
            if Path(name).name!=name:raise ValueError('Unexpected member name')
            data=subprocess.check_output(['ar','p',str(old),name]);before=hashlib.sha256(data).hexdigest()
            if name in replacements:
                new,expected=replacements[name];original=expected.read_bytes()
                if data!=original and data!=original+b'\n'*(-len(original)%8):raise ValueError('Original member identity changed: '+name)
                data=new.read_bytes()
            (folder/name).write_bytes(data);records[name]={'before':before,'after':hashlib.sha256(data).hexdigest()}
        archive=folder/suffix;run(['ar','rcs',archive,*names],label+'-archive',folder)
        for name in names:
            data=(folder/name).read_bytes();stored=subprocess.check_output(['ar','p',str(archive),name])
            if stored!=data and stored!=data+b'\n'*(-len(data)%8):raise ValueError('New archive readback mismatch')
        command[command.index(arguments[0])]='-Wl,-force_load,'+str(archive)
        return records
    cap=read(a.base_native/'capture.json');results=read(a.base_native/'build/results.json')
    platform_jobs=[(i,j) for i,j in enumerate(cap['jobs']) if j['file'].endswith('/ArmDTPlatform.cpp')]
    if len(platform_jobs)!=1:raise ValueError('Unique captured platform required')
    i,job=platform_jobs[0];old_platform=Path(results[str(i)]['output'])
    if sha(old_platform)!=results[str(i)]['sha256']:raise ValueError('Platform parent changed')
    new_platform=a.rtc_build/'ArmDTPlatform.o'
    additions=defined(new_platform)-defined(old_platform)
    expected={'_armdt_platform_clock_floor','_armdt_platform_clock_register',
              '__ZN13ArmDTPlatform15getGMTTimeOfDayEv','__ZN13ArmDTPlatform15setGMTTimeOfDayEl'}
    if additions!=expected or defined(old_platform)-defined(new_platform):raise ValueError('Unexpected platform export delta')
    platform_members=replace_archive('libArmDTPlatform.a',{'ArmDTPlatform.o':(new_platform,old_platform)},'platform')
    gates=[(n,Path(n)) for n in command if Path(n).name=='TransactionGate.o']
    if len(gates)!=1:raise ValueError('Exactly one directly linked shared gate required')
    gate_arg,old_gate=gates[0];new_gate=a.rtc_build/'TransactionGate.o'
    if defined(old_gate)!=defined(new_gate):raise ValueError('Shared gate public interface changed')
    command[command.index(gate_arg)]=str(new_gate)
    recovery=read(a.recovery_parent/'result.json')['native_objects']['ShengUfs']
    source=a.recovery_parent/'source/EldenSdamRecovery.inc'
    if sha(source)!=recovery['recovery_sha256']:raise ValueError('Recovery source changed')
    text=source.read_text();anchor='  elden_ufs_wait_shutdown();'
    if text.count(anchor)!=1:raise ValueError('Recovery order changed')
    new_source=out/'EldenSdamRecovery.inc'
    new_source.write_text('extern "C" int elden_rtc_stop_for_reboot(void);\n'+text.replace(anchor,
        '  int rtcStop=elden_rtc_stop_for_reboot();\n'
        '  eldenSdamLog(rtcStop?"\\nELDEN_RTC stop unconfirmed; maps retained to reset\\n":"\\nELDEN_RTC stopped; maps retained\\n");\n'+anchor))
    overlay=read(a.recovery_parent/'ShengUfs.overlay.json');changed=[]
    def visit(nodes):
        for node in nodes:
            if node.get('type')=='file' and node.get('name','').endswith('/EldenSdamRecovery.inc'):
                if sha(Path(node['external-contents']))!=recovery['recovery_sha256']:raise ValueError('Recovery overlay differs')
                node['external-contents']=str(new_source);changed.append(node['name'])
            if 'contents' in node:visit(node['contents'])
    visit(overlay['roots'])
    if not changed:raise ValueError('Actual recovery mapping absent')
    vfs=out/'ShengUfs.overlay.json';vfs.write_text(json.dumps(overlay,indent=2)+'\n')
    compile_cmd=list(recovery['command']);compile_cmd[compile_cmd.index('-ivfsoverlay')+1]=str(vfs)
    new_ufs=out/'ShengUfs.o';compile_cmd[compile_cmd.index('-o')+1]=str(new_ufs)
    run(compile_cmd,'recovery-compile',recovery['cwd']);old_ufs=a.recovery_parent/'ShengUfs.o'
    if sha(old_ufs)!=recovery['object_sha256']:raise ValueError('Recovery parent object changed')
    if defined(new_ufs)!=defined(old_ufs) or set(global_symbols(new_ufs))-set(global_symbols(old_ufs))!={'_elden_rtc_stop_for_reboot'}:
        raise ValueError('Unexpected recovery interface delta')
    if sections(new_ufs).get('__DATA/__mod_init_func')!=sections(old_ufs).get('__DATA/__mod_init_func'):raise ValueError('UFS initializer bytes changed')
    recovery_members=replace_archive('libShengUfs.a',{'ShengUfs.o':(new_ufs,old_ufs)},'ufs')
    command[command.index('-o')+1]=str(out/'native.macho');command+=['-Wl,-force_load,'+str(a.rtc_build/'libEldenRtc.a')]
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link',out)
    for path,digest in inherited.items():
        if sha(Path(path))!=digest:raise ValueError('Original parent input changed')
    owner_exports={'_elden_rtc_owner_init','_elden_rtc_owner_bind_nvmem','_elden_rtc_owner_bind_alarm',
        '_elden_rtc_owner_restore','_elden_rtc_owner_learn','_elden_rtc_owner_alarm_set','_elden_rtc_owner_alarm_read',
        '_elden_rtc_owner_alarm_ack','_elden_rtc_owner_stop','_elden_rtc_owner_retire','_elden_rtc_owner_status'}
    if defined(out/'native.macho')&owner_exports!=owner_exports:raise ValueError('Native link dropped owner entries')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS image differs')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
             '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:prelink+=['--root',name]
    for name in plan['early_security_initializers']:prelink+=['--early-security-init',name]
    run(prelink,'prelink',out);metadata=read(out/'kernel.json')
    count=verify_original_executables(out/'kernel.macho',metadata,original['original_executable_sha256'])
    result={'passed':True,'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),
            'original_regions_preserved':count,'platform_members':platform_members,'ufs_members':recovery_members,
            'platform_added_exports':sorted(additions),'new_exports':sorted(defined(out/'native.macho')-defined(a.parent/'native.macho')),
            'shared_gate_replaced_once':True,'rtc_before_ufs_shutdown':True,'recovery_object_sha256':sha(new_ufs),
            'recovery_compile_command':compile_cmd,'rtc_report_sha256':sha(a.rtc_build/'result.json'),
            'owner_object_sha256':owner_row['object_sha256'],'rtc_archive_members':rtc_members,
            'owner_exports':sorted(owner_exports),'provider_bound':False,'nvmem_provider_installed':False,
            'alarm_provider_installed':False,'owner_auto_started':False,
            'inherited_inputs':inherited,'parent_kernel_sha256':prior['kernel_sha256'],'hardware':False,'boot_packaged':False,
            'legacy_flags_unchanged':{n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: real RTC/platform/shared-gate/recovery integrated over pen kernel; original Apple regions preserved')
if __name__=='__main__':main()
