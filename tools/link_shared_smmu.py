"""Link one shared early route/native provider and staged camera/ADSP memory."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','shared-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('parent','shared_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');build=read(a.shared_build/'result.json')
    if not prior['passed'] or not build['passed'] or build['parent_kernel_sha256']!=prior['kernel_sha256']:raise ValueError('Unqualified actual parent/build')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Actual parent changed')
    for name,row in build['objects'].items():
        if sha(a.shared_build/name)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(a.shared_build/name)):raise ValueError('New object changed/automatic initialization')
    command=read(a.parent/'link-command.json');inputs={};original_archives={};delta={}
    for arg in command:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.a','.o'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(cmd,label,cwd=None):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd or out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    for library,member,new,wanted in [('libNativeLegacyDisplay.late.a','EldenNvtDmaRoute.o','SharedTouchRoute.o',{'_elden_touch_route_expected'}),
            ('libEldenSensorMemory.a','NativeMemory.o','NativeMemory.o',{'_elden_sensor_memory_native_prepare','_elden_sensor_memory_native_activate'})]:
        args=[v for v in command if v.startswith('-Wl,-force_load,') and Path(v.split(',',2)[2]).name==library]
        if len(args)!=1:raise ValueError('One actual original archive required: '+library)
        archive=Path(args[0].split(',',2)[2]);folder=out/library.removesuffix('.a');folder.mkdir()
        names=[n for n in subprocess.check_output(['ar','t',archive],text=True).splitlines() if not n.startswith('__.SYMDEF')]
        if not names or len(names)!=len(set(names)) or member not in names:raise ValueError('Actual archive inventory changed')
        rows={}
        for name in names:
            if Path(name).name!=name:raise ValueError('Unowned archive member path')
            body=subprocess.check_output(['ar','p',archive,name]);before=hashlib.sha256(body).hexdigest()
            if name==member:
                old=folder/('old-'+name);old.write_bytes(body);obj=a.shared_build/new
                if exports(old)-exports(obj) or exports(obj)-exports(old)!=wanted:raise ValueError('Unexpected original native API delta: '+member)
                body=obj.read_bytes()
            (folder/name).write_bytes(body);rows[name]={'before':before,'after':hashlib.sha256(body).hexdigest()}
        if [n for n,row in rows.items() if row['before']!=row['after']]!=[member]:raise ValueError('Only exact original member may change')
        target=folder/library;run(['ar','rcs',target,*names],library+'-archive',folder);command[command.index(args[0])]='-Wl,-force_load,'+str(target)
        delta[library]={'before_path':str(archive),'after_path':str(target),'original_sha256':sha(archive),'replacement_sha256':sha(target),'members':rows,'changed':[member]}
        original_archives[library]=str(archive)
    direct={}
    for name,wanted in [('NativeCameraDma.o',{'_elden_camera_dma_prepare_batch','_elden_camera_dma_activate'}),('SensorRuntimeNative.o',{'_elden_sensor_runtime_adopt'})]:
        found=[v for v in command if Path(v).name==name]
        if len(found)!=1:raise ValueError('One original direct object required: '+name)
        old=Path(found[0]);new=a.shared_build/name
        if exports(old)-exports(new) or exports(new)-exports(old)!=wanted:raise ValueError('Unexpected by-value memory consumer delta: '+name)
        command[command.index(found[0])]=str(new);direct[name]={'before_path':str(old),'after_path':str(new),'before':sha(old),'after':sha(new)}
    for name in ('Shared.o','SharedBootRoute.o','SharedPeripheral.o','SharedNative.o'):
        if any(Path(v).name==name for v in command):raise ValueError('Duplicate shared native object')
        command.append(str(a.shared_build/name))
    command[command.index('-o')+1]=str(out/'native.macho');(out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Peer/inherited input mutated')
    metadata=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=metadata['original_sha256']:raise ValueError('Original OS17 kernel changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,'--slide='+str(metadata['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:cmd+=['--root',n]
    for n in plan['early_security_initializers']:cmd+=['--early-security-init',n]
    run(cmd,'prelink');meta=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',meta,metadata['original_executable_sha256'])
    report=dict(prior);inherited=dict(prior['inherited_inputs']);inherited.update(inputs)
    # Old component metadata keeps the historical delta; the exact current
    # archive replacement is separate so auditors never whitelist loose names.
    report.update({'passed':True,'parent_kernel_sha256':prior['kernel_sha256'],'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'original_regions_preserved':count,'inherited_inputs':inherited,'shared_build_sha256':sha(a.shared_build/'result.json'),
        'actual_shared_SMMU_native_staged_peripherals':True,'shared_archive_deltas':delta,'shared_direct_deltas':direct,
        'shared_added_objects':{n:build['objects'][n]['sha256'] for n in ('Shared.o','SharedBootRoute.o','SharedPeripheral.o','SharedNative.o')},
        'shared_native_build_path':str(a.shared_build),'shared_source_records':build['source_records'],
        'shared_input_source_parent_commit':'ba26ee40d0184d293240249f12c54d1ad4c3a979',
        'shared_physical_authority_installed':False,'automatic_shared_startup':False,'boot_packaged':False,'hardware':False,
        'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS full sharedSMMU/staged-memory link; only2exact archive members+2direct layout owners changed, recovery/Apple30 held')
if __name__=='__main__':main()
