"""Link original OS17 motion HID library over the verified offline wake image.

No producer, ADSP/QRTR hardware, MobileGestalt or orientation/SPU startup is
installed. The real HID entrypoints remain opt-in to an admitted Gen4 producer.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','motion-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('parent','motion_build','prelink_tool','original_kernelcache'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);prior=read(a.parent/'result.json');built=read(a.motion_build/'result.json')
    if not prior['passed'] or not built['passed']:raise ValueError('Parent/motion incomplete')
    for n in ('kernel','native'):
        if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for n,row in built['objects'].items():
        if sha(a.motion_build/n)!=row['sha256']:raise ValueError('Motion object changed')
        if any(k.endswith('/__mod_init_func') for k in sections(a.motion_build/n)):raise ValueError('Automatic sensor initialization')
    library=a.motion_build/'libEldenMotionHid.a'
    if sha(library)!=built['archive_sha256']:raise ValueError('Motion archive changed')
    cmd=read(a.parent/'link-command.json');inputs={}
    for arg in cmd:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    cmd[cmd.index('-o')+1]=str(out/'native.macho');cmd+=['-Wl,-force_load,'+str(library)]
    def run(command,label):
        with (out/(label+'.log')).open('x') as f:r=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
    (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n');run(cmd,'native-link')
    for n,d in inputs.items():
        if sha(Path(n))!=d:raise ValueError('Inherited inputs changed')
    original=read(a.parent/'kernel.json')
    if sha(a.original_kernelcache)!=original['original_sha256']:raise ValueError('Original OS changed')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    prelink=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
        '--slide='+str(original['slide']),'--output',out/'kernel.macho']
    for n in plan['roots']:prelink+=['--root',n]
    for n in plan['early_security_initializers']:prelink+=['--early-security-init',n]
    run(prelink,'prelink');meta=read(out/'kernel.json')
    count=verify_original_executables(out/'kernel.macho',meta,original['original_executable_sha256'])
    r={'passed':True,'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),
        'parent_kernel_sha256':prior['kernel_sha256'],'original_regions_preserved':count,'inherited_inputs':inputs,
        'motion_build_report_sha256':sha(a.motion_build/'result.json'),'recovery_object_sha256':prior['recovery_object_sha256'],
        'recovery_compile_command':prior['recovery_compile_command'],'recovery_compile_cwd':prior['recovery_compile_cwd'],
        'actual_Gen4_producer_connected':False,'device_orientation_service':False,'native_hid_library_linked':True,
        'hardware':False,'boot_packaged':False,'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: actual OS17 motion HID link; all30 Apple code regions and existing native owners retained; no producer')
if __name__=='__main__':main()
