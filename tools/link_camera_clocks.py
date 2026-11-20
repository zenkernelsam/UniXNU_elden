"""Link the actual camera clock/native board objects over shared-power image."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','clock-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('parent','clock_build','prelink_tool','original_kernelcache'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.clock_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Unqualified parent/camera clock build')
    for name in ('kernel','native'):
        if sha(a.parent/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Actual parent changed')
    command=read(a.parent/'link-command.json');inputs={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    for name,row in built['native_objects'].items():
        path=a.clock_build/name
        if sha(path)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(path)):raise ValueError('Object changed/auto-init')
        if any(Path(v).name==name for v in command):raise ValueError('Duplicate camera native object')
        command.append(str(path))
    command[command.index('-o')+1]=str(out/'native.macho')
    def run(cmd,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; original logs retained')
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n');run(command,'native-link')
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited input changed')
    previous=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=previous['original_sha256']:raise ValueError('Original OS image differs')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho',
        '--prelinked',a.original_kernelcache,'--slide='+str(previous['slide']),'--output',out/'kernel.macho']
    for name in plan['roots']:cmd.extend(['--root',name])
    for name in plan['early_security_initializers']:cmd.extend(['--early-security-init',name])
    run(cmd,'prelink');metadata=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',metadata,previous['original_executable_sha256'])
    inherited=dict(prior['inherited_inputs']);inherited.update(inputs)
    report=dict(prior);report.update({'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'parent_kernel_sha256':prior['kernel_sha256'],'inherited_inputs':inherited,'original_regions_preserved':count,
        'clock_build_sha256':sha(a.clock_build/'result.json'),'native_clock_board_objects':built['native_objects'],
        'actual_native_camera_clock_board':True,'camera_board_physical_owner_installed':False,'automatic_board_startup':False,
        'physical_camera_frames_OS17_service':False,'legacy_flags_unchanged':{n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS full native camera clock/board/power/CCI/CAMSS link; all existing archives/recovery andApple30retained')
if __name__=='__main__':main()
