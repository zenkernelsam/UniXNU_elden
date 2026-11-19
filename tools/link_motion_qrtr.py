"""Link only original QRTR layout consumers and explicit SSC client provider."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
from replay_xnu_objects import sections, global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defs(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','qrtr-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('parent','qrtr_build','prelink_tool','original_kernelcache'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');build=read(a.qrtr_build/'result.json')
    if not prior['passed'] or not build['passed']:raise ValueError('Unqualified QRTR parent/component')
    for name in ('kernel','native'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Qualified parent image changed')
    for name,row in build['objects'].items():
        if sha(a.qrtr_build/name)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(a.qrtr_build/name)):raise ValueError('QRTR object changed/automatic startup')
    command=read(a.parent/'link-command.json');inputs={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    qualification=read(ROOT/'drivers/EldenMotion/adsp-validation.json')
    original_archive=next(Path(name) for name in inputs if Path(name).name=='libEldenAdspRuntime.a')
    parent_members={}
    for name in ('AdspProbe.o','ShengQrtr.o','QcomQrtr.o'):
        parent_members[name]=hashlib.sha256(subprocess.check_output(['ar','p',original_archive,name])).hexdigest()
        if parent_members[name]!=qualification['changed_transport_archives']['libEldenAdspRuntime.a'][name]['after']:raise ValueError('Qualified ADSP parent member differs')
    matches=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/libEldenAdspRuntime.a')]
    if len(matches)!=1:raise ValueError('One original ADSP/QRTR archive required')
    archive=Path(matches[0].split(',',2)[2]);folder=out/'EldenAdspRuntime';folder.mkdir()
    names=subprocess.check_output(['ar','t',archive],text=True).splitlines();names=[n for n in names if not n.startswith('__.SYMDEF')]
    if len(names)!=len(set(names)) or not {'AdspProbe.o','ShengQrtr.o','QcomQrtr.o'}<=set(names):raise ValueError('Original type-consumer members absent')
    added={'AdspProbe.o':set(),'ShengQrtr.o':{'_elden_qrtr_original_service'},
           'QcomQrtr.o':{'_qcom_qrtr_find_server','_qcom_qrtr_watch','_qcom_qrtr_forget_remote','_qcom_qrtr_frame',
                         '_qcom_qrtr_control_frame','_qcom_servloc_pfr_answer','_elden_qrtr_service'}}
    members={}
    for name in names:
        if Path(name).name!=name:raise ValueError('Unexpected archive member path')
        body=subprocess.check_output(['ar','p',archive,name]);before=hashlib.sha256(body).hexdigest()
        if name in added:
            saved=folder/('old-'+name);saved.write_bytes(body);new=a.qrtr_build/name
            if defs(saved)-defs(new) or defs(new)-defs(saved)!=added[name]:raise ValueError('Unexpected public API change: '+name)
            if sections(saved).get('__DATA/__mod_init_func')!=sections(new).get('__DATA/__mod_init_func'):raise ValueError('Initializer changed')
            body=new.read_bytes()
        (folder/name).write_bytes(body);members[name]={'before':before,'after':hashlib.sha256(body).hexdigest()}
    def run(cmd,label,cwd=None):
        with (out/(label+'.log')).open('x') as log:result=subprocess.run(list(map(str,cmd)),cwd=cwd or out,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:raise RuntimeError(label+' failed; original log retained')
    new=folder/'libEldenAdspRuntime.a';run(['ar','rcs',new,*names],'archive',folder)
    command[command.index(matches[0])]='-Wl,-force_load,'+str(new)
    command[command.index('-o')+1]=str(out/'native.macho');command.extend(str(a.qrtr_build/name) for name in ('QrtrPorts.o','QrtrTransport.o'))
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for path,digest in inputs.items():
        if sha(Path(path))!=digest:raise ValueError('Inherited artifact mutated')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
             '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:prelink.extend(['--root',name])
    for name in plan['early_security_initializers']:prelink.extend(['--early-security-init',name])
    run(prelink,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={name:prior[name] for name in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd')}
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':count,'inherited_inputs':inputs,
        'changed_transport_archives':{'libEldenAdspRuntime.a':members},'qrtr_build_report_sha256':sha(a.qrtr_build/'result.json'),
        'single_original_ADSP_IPCRTR_QRTR_owner':True,'actual_SSC_transport_adapter_linked':True,
        'actual_owner_receive_intents_retirement_capture_SMMU_installed':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    # Older component audits start from their original parents. Keep that
    # exact artifact SHA and compose only byte-matched declared deltas, so
    # every earlier archive/member check remains independently reproducible.
    history=read(a.parent/'result.json')['changed_transport_archives'];baseline={}
    for name,rows in history.items():
        baseline[name]={member:dict(row) for member,row in rows.items()}
    for name,row in members.items():
        if baseline['libEldenAdspRuntime.a'][name]['after']!=row['before']:raise ValueError('Unmatched cumulative transport member chain')
        baseline['libEldenAdspRuntime.a'][name]['after']=row['after']
    result['cumulative_transport_archives']=baseline
    result['adsp_parent_member_fingerprints']=parent_members
    for path,digest in prior.get('inherited_inputs',{}).items():
        if sha(Path(path))!=digest:raise ValueError('Earlier qualified inherited input changed')
        if path in result['inherited_inputs'] and result['inherited_inputs'][path]!=digest:raise ValueError('Inherited fingerprint collision')
        result['inherited_inputs'][path]=digest
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS original QRTR client/provider full link; exact3layout consumers, original other members/Apple30 retained')
if __name__=='__main__':main()
