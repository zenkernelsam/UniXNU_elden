"""Link exact camera/CCI/native binding over current motion/wake/Actuator image.

Only new source-built camera objects are added. No automatic startup, sensor
power provider, CAMSS/DMA or OS17 camera service is introduced by this link.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections,global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','cci-build','core-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','cci_build','core_build','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');cci=read(a.cci_build/'result.json');core=read(a.core_build/'result.json')
    if not all(r['passed'] for r in (prior,cci,core)):raise ValueError('Component build incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image differs')
    objects=[(a.cci_build/'CameraCci.o',cci['object_sha256']),
        (a.cci_build/'NativeCci.o',cci['native_CCI_object_sha256']),
        (a.cci_build/'SensorBinding.o',cci['binding_object_sha256']),
        (a.core_build/'CameraCore.o',core['object_sha256'])]
    symbols=set()
    for path,d in objects:
        if sha(path)!=d:raise ValueError('Camera object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(path)):raise ValueError('Automatic camera initializer')
        definitions={n for n,v in global_symbols(path).items() if v[0]&0xe}
        if definitions&symbols:raise ValueError('Duplicate camera provider')
        symbols|=definitions
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
    lib=out/'libEldenCamera.a';run(['ar','rcs',lib,*[p for p,d in objects]],'camera-archive')
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(lib)]
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inputs.items():
        if sha(Path(n))!=d:raise ValueError('Inherited provider/input changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS image differs')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
        '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json')
    count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    r={'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':prior['kernel_sha256'],
       'camera_objects':{p.name:d for p,d in objects},'archive_sha256':sha(lib),'CCI_build_report_sha256':sha(a.cci_build/'result.json'),
       'core_build_report_sha256':sha(a.core_build/'result.json'),'inherited_inputs':inputs,'original_regions_preserved':count,
       'recovery_object_sha256':prior['recovery_object_sha256'],'recovery_compile_command':prior['recovery_compile_command'],'recovery_compile_cwd':prior['recovery_compile_cwd'],
       'native_CCI_sensor_control_linked':True,'automatic_camera_startup':False,'real_power_CSI_VFE_DMA_provider':False,'camera_frames_verified':False,
       'hardware':False,'boot_packaged':False,'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: camera core/native CCI/control binding linked; all30 Apple regions and original owners retained; no automatic camera startup')
if __name__=='__main__':main()
