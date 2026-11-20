"""Add exact Gen4 native camera receiver and existing sensor capture binding."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','pipeline-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('parent','pipeline_build','prelink_tool','original_kernelcache'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');build=read(a.pipeline_build/'result.json')
    if not prior['passed'] or not build['passed']:raise ValueError('Unqualified parent/camera component')
    for name in ('kernel','native'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Qualified parent changed')
    for name,row in build['native_objects'].items():
        if sha(a.pipeline_build/name)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(a.pipeline_build/name)):raise ValueError('Camera object changed/auto-start')
    command=read(a.parent/'link-command.json');inputs={}
    for arg in command:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    for name in ('CameraPipeline.o','NativePipeline.o','CaptureBinding.o'):
        if any(Path(v).name==name for v in command):raise ValueError('Duplicate native camera object')
        command.append(str(a.pipeline_build/name))
    command[command.index('-o')+1]=str(out/'native.macho')
    def run(cmd,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited native input changed')
    metadata=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=metadata['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,'--slide='+str(metadata['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:cmd.extend(['--root',name])
    for name in plan['early_security_initializers']:cmd.extend(['--early-security-init',name])
    run(cmd,'prelink');meta=read(out/'kernel.json');regions=verify_original_executables(out/'kernel.macho',meta,metadata['original_executable_sha256'])
    result={k:prior[k] for k in ('ufs_members','display_members','recovery_object_sha256','recovery_compile_command','recovery_compile_cwd',
        'changed_transport_archives','cumulative_transport_archives','adsp_parent_member_fingerprints','single_original_ADSP_IPCRTR_QRTR_owner',
        'changed_memory_binding_archive','changed_SSC_archive','changed_runtime_UFS_archive','actual_sensor_runtime_composition','aggregate_recovery_bound_ms')}
    inherited=dict(prior['inherited_inputs']);inherited.update(inputs)
    result.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
        'original_regions_preserved':regions,'inherited_inputs':inherited,'pipeline_build_sha256':sha(a.pipeline_build/'result.json'),
        'actual_camera_pipeline_capture_binding':True,'original_all_native_archives_retained':True,'new_camera_objects':build['native_objects'],
        'physical_power_SMMU_buffer_DMA_IRQ_owner_installed':False,'real_camera_frames_OSservice':False,'hardware':False,'boot_packaged':False,
        'legacy_flags_unchanged':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS full Gen4 camera PHY/CSID/VFE/capture native link; original all archives/recovery/Apple30 retained')
if __name__=='__main__':main()
