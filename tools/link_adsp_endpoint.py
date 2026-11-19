"""Link original ADSP/GLINK epoch-close extensions and concrete sensor endpoint.

Replace only the actual two type-layout consumers, preserving other archive
members/core/recovery and the existing charger/QRTR/audio entrypoints.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections,global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defs(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','channel-build','endpoint-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('parent','channel_build','endpoint_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');channel=read(a.channel_build/'result.json');endpoint=read(a.endpoint_build/'result.json')
    if not prior['passed'] or not channel['passed'] or not endpoint['passed']:raise ValueError('Parent/transport component incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Qualified parent image changed')
    for folder,record in ((a.channel_build,channel),(a.endpoint_build,endpoint)):
        for n,row in record['objects'].items():
            if sha(folder/n)!=row['sha256']:raise ValueError('Native transport object changed')
            if any(k.endswith('/__mod_init_func') for k in sections(folder/n)):raise ValueError('Automatic transport initializer')
    command=read(a.parent/'link-command.json');inputs={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(cmd,label,cwd=None):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd or out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    replaced={}
    targets=[('libEldenAdspRuntime.a','AdspProbe.o',a.channel_build/'AdspProbe.o',
              {'_elden_adsp_channel_epoch','_elden_adsp_channel_status','_elden_adsp_channel_close'}),
             ('libEldenPmicTransport.a','QcomGlink.o',a.channel_build/'QcomGlink.o',
              {'_qcom_glink_stop','_elden_glink_close_request','_elden_glink_close_status'})]
    for suffix,member,new,added in targets:
        matches=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/'+suffix)]
        if len(matches)!=1:raise ValueError('One actual original transport archive required')
        old=Path(matches[0].split(',',2)[2]);folder=out/suffix.removeprefix('lib').removesuffix('.a');folder.mkdir()
        names=subprocess.check_output(['ar','t',old],text=True).splitlines();names=[n for n in names if not n.startswith('__.SYMDEF')]
        if len(names)!=len(set(names)) or member not in names:raise ValueError('Transport member inventory differs')
        members={}
        for name in names:
            if Path(name).name!=name:raise ValueError('Unexpected archive path')
            b=subprocess.check_output(['ar','p',old,name]);before=hashlib.sha256(b).hexdigest()
            if name==member:
                saved=folder/('old-'+name);saved.write_bytes(b)
                if defs(saved)-defs(new) or defs(new)-defs(saved)!=added:raise ValueError('Unexpected public transport API delta: '+name)
                if sections(saved).get('__DATA/__mod_init_func')!=sections(new).get('__DATA/__mod_init_func'):raise ValueError('Transport initializers changed')
                b=new.read_bytes()
            (folder/name).write_bytes(b);members[name]={'before':before,'after':hashlib.sha256(b).hexdigest()}
        library=folder/suffix;run(['ar','rcs',library,*names],suffix+'-archive',folder)
        replaced[suffix]=members;command[command.index(matches[0])]='-Wl,-force_load,'+str(library)
    command[command.index('-o')+1]=str(out/'native.macho');command+=[str(a.endpoint_build/'AdspEndpoint.o')]
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for path,d in inputs.items():
        if sha(Path(path))!=d:raise ValueError('Inherited object/archive mutated')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    result={k:prior[k] for k in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd')}
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':count,'inherited_inputs':inputs,'changed_transport_archives':replaced,
        'channel_build_report_sha256':sha(a.channel_build/'result.json'),'endpoint_build_report_sha256':sha(a.endpoint_build/'result.json'),
        'single_original_ADSP_registry_and_GLlNK_owner':True,'concrete_sensor_endpoint_linked':True,'actual_retirement_capture_SMMU_provider_installed':False,
        'hardware':False,'boot_packaged':False,'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: original ADSP/GLINK registry close and sensor endpoint full link; only2type consumers replaced, Apple30/other owners retained')
if __name__=='__main__':main()
