"""Link early-prepared listener/SSC runtime and original bounded recovery call."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defined(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','runtime-build','recovery-source','prelink-tool','original-kernelcache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('parent','runtime_build','recovery_source','prelink_tool','original_kernelcache'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');build=read(a.runtime_build/'result.json')
    if not prior['passed'] or not build['passed']:raise ValueError('Unqualified parent/runtime')
    for name in ('kernel','native'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Qualified parent changed')
    for name,row in build['objects'].items():
        if sha(a.runtime_build/name)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(a.runtime_build/name)):raise ValueError('Unqualified runtime object')
    command=read(a.parent/'link-command.json');inputs={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.a','.o'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(cmd,label,cwd=None):
        with (out/(label+'.log')).open('x') as log:r=subprocess.run(list(map(str,cmd)),cwd=cwd or out,stdout=log,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    sscargs=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/libEldenMotionSsc.a')]
    if len(sscargs)!=1:raise ValueError('One actual original SSC worker archive required')
    sscarchive=Path(sscargs[0].split(',',2)[2]);sscfolder=out/'EldenMotionSsc';sscfolder.mkdir()
    sscnames=subprocess.check_output(['ar','t',sscarchive],text=True).splitlines();sscnames=[n for n in sscnames if not n.startswith('__.SYMDEF')]
    sscmembers={}
    for name in sscnames:
        body=subprocess.check_output(['ar','p',sscarchive,name]);before=hashlib.sha256(body).hexdigest()
        if name=='NativeSsc.o':
            old=sscfolder/'old-NativeSsc.o';old.write_bytes(body);new=a.runtime_build/name
            if defined(old)-defined(new) or defined(new)-defined(old)!={'_elden_motion_native_ssc_observe'}:raise ValueError('Unexpected SSC API delta')
            body=new.read_bytes()
        (sscfolder/name).write_bytes(body);sscmembers[name]={'before':before,'after':hashlib.sha256(body).hexdigest()}
    if [n for n,row in sscmembers.items() if row['before']!=row['after']]!=['NativeSsc.o']:raise ValueError('Only original SSC native worker may change')
    newssc=sscfolder/'libEldenMotionSsc.a';run(['ar','rcs',newssc,*sscnames],'ssc-archive',sscfolder);command[command.index(sscargs[0])]='-Wl,-force_load,'+str(newssc)
    args=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/libEldenSensorMemory.a')]
    if len(args)!=1:raise ValueError('One actual sensor-memory binding archive required')
    original=Path(args[0].split(',',2)[2]);folder=out/'EldenSensorMemory';folder.mkdir()
    members=subprocess.check_output(['ar','t',original],text=True).splitlines();members=[n for n in members if not n.startswith('__.SYMDEF')]
    if len(members)!=len(set(members)) or 'MemoryBinding.o' not in members:raise ValueError('Actual memory archive inventory changed')
    memory={}
    for name in members:
        if Path(name).name!=name:raise ValueError('Unexpected archive member path')
        body=subprocess.check_output(['ar','p',original,name]);before=hashlib.sha256(body).hexdigest()
        if name=='MemoryBinding.o':
            saved=folder/'old-MemoryBinding.o';saved.write_bytes(body);new=a.runtime_build/name
            if defined(saved)-defined(new) or defined(new)-defined(saved)!={'_elden_sensor_memory_binding_open_prepared','_elden_sensor_memory_binding_status'}:raise ValueError('Unexpected memory API delta')
            body=new.read_bytes()
        (folder/name).write_bytes(body);memory[name]={'before':before,'after':hashlib.sha256(body).hexdigest()}
    library=folder/'libEldenSensorMemory.a';run(['ar','rcs',library,*members],'memory-archive',folder);command[command.index(args[0])]='-Wl,-force_load,'+str(library)
    oldsource=a.recovery_source/'EldenSdamRecovery.inc';text=oldsource.read_text();token='elden_motion_native_ssc_recovery_stop'
    if text.count(token)!=2:raise ValueError('Original bounded motion recovery wrapper changed')
    recovery=out/'EldenSdamRecovery.inc';recovery.write_text(text.replace(token,'elden_sensor_runtime_recovery_stop'),encoding='utf-8',newline='\n')
    overlay=read(a.recovery_source/'ShengUfs.overlay.json');count=[]
    def visit(nodes):
        for n in nodes:
            if n.get('type')=='file' and n.get('name','').endswith('/EldenSdamRecovery.inc'):
                if Path(n['external-contents']).read_bytes()!=oldsource.read_bytes():raise ValueError('Original recovery overlay identity differs')
                n['external-contents']=str(recovery);count.append(n['name'])
            if 'contents' in n:visit(n['contents'])
    visit(overlay['roots'])
    if not count:raise ValueError('Recovery overlay missing')
    vf=out/'ShengUfs.overlay.json';vf.write_text(json.dumps(overlay,indent=2)+'\n');compile=list(prior['recovery_compile_command'])
    if Path(compile[compile.index('-ivfsoverlay')+1]).read_bytes()!=(a.recovery_source/'ShengUfs.overlay.json').read_bytes():raise ValueError('Qualified recovery compile input differs')
    compile[compile.index('-ivfsoverlay')+1]=str(vf);compile[compile.index('-o')+1]=str(out/'ShengUfs.o')
    run(compile,'recovery-compile',prior['recovery_compile_cwd']);new=out/'ShengUfs.o';old=a.recovery_source/'ShengUfs.o'
    if sha(old)!=prior['recovery_object_sha256'] or defined(new)!=defined(old):raise ValueError('Original recovery defined API changed')
    before={n for n,v in global_symbols(old).items() if not v[0]&0xe};after={n for n,v in global_symbols(new).items() if not v[0]&0xe}
    if after-before!={'_elden_sensor_runtime_recovery_stop'} or before-after!={'_elden_motion_native_ssc_recovery_stop'}:raise ValueError('Recovery changed unrelated imports')
    targets=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/libShengUfs.a')]
    if len(targets)!=1:raise ValueError('One original UFS archive required')
    originalufs=Path(targets[0].split(',',2)[2]);ufsfolder=out/'ShengUfs';ufsfolder.mkdir();names=subprocess.check_output(['ar','t',originalufs],text=True).splitlines();names=[n for n in names if not n.startswith('__.SYMDEF')]
    ufs={};ufs_actual={}
    for name in names:
        body=subprocess.check_output(['ar','p',originalufs,name]);before=hashlib.sha256(body).hexdigest()
        if name=='ShengUfs.o':
            if before!=sha(old):raise ValueError('Actual original UFS recovery member differs')
            body=new.read_bytes()
        (ufsfolder/name).write_bytes(body);ufs[name]={'before':prior['ufs_members'][name]['before'],'after':hashlib.sha256(body).hexdigest()}
        ufs_actual[name]={'before':before,'after':hashlib.sha256(body).hexdigest()}
    ufsarchive=ufsfolder/'libShengUfs.a';run(['ar','rcs',ufsarchive,*names],'ufs-archive',ufsfolder);command[command.index(targets[0])]='-Wl,-force_load,'+str(ufsarchive)
    command.extend([str(a.runtime_build/'SensorRuntimeNative.o')]);command[command.index('-o')+1]=str(out/'native.macho')
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited artifact mutated')
    originalmeta=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=originalmeta['original_sha256']:raise ValueError('Original OS identity changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,'--slide='+str(originalmeta['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink.extend(['--root',n])
    for n in plan['early_security_initializers']:prelink.extend(['--early-security-init',n])
    run(prelink,'prelink');meta=read(out/'kernel.json');regions=verify_original_executables(out/'kernel.macho',meta,originalmeta['original_executable_sha256'])
    result={n:prior[n] for n in ('display_members','changed_transport_archives','cumulative_transport_archives','adsp_parent_member_fingerprints','single_original_ADSP_IPCRTR_QRTR_owner')}
    inherited=dict(prior['inherited_inputs']);inherited.update(inputs)
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':regions,'inherited_inputs':inherited,'ufs_members':ufs,'recovery_object_sha256':sha(new),
        'recovery_compile_command':compile,'recovery_compile_cwd':prior['recovery_compile_cwd'],'changed_memory_binding_archive':memory,
        'changed_SSC_archive':sscmembers,'actual_sensor_runtime_composition':True,
        'only_recovery_wrapper_replaced':True,'aggregate_recovery_bound_ms':5500,'runtime_build_sha256':sha(a.runtime_build/'result.json'),
        'real_owner_capture_calibration_SMMU_installed':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    result['changed_runtime_UFS_archive']=ufs_actual
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS full native sensor runtime/memory/SSC/recovery link, existing bound/other owners/Apple30 retained')
if __name__=='__main__':main()
