"""Prelink the original21H461 network family over the unchanged v107 native kernel."""
from pathlib import Path
import argparse,hashlib,io,json,shutil,subprocess,sys,tarfile
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Run without -O; validation assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','original-kernelcache','prelink-repository','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.original_kernelcache=a.original_kernelcache.resolve();out=a.output.resolve();base=a.baseline.resolve();pp=ROOT/'profiles/v108-network-family/plan.json';profile=json.loads(pp.read_text())
    if out.exists():raise ValueError('New build required')
    assert sha(base/'native.macho')==profile['baseline_native_sha256']
    assert sha(base/'kernel.macho')==profile['baseline_kernel_sha256']
    assert sha(a.original_kernelcache)==profile['original_kernel_sha256']
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];out.mkdir(parents=True);tool=out/'prelink-tool';tool.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(tool,filter='data')
    for name in ('native.macho','link.filelist'):shutil.copyfile(base/name,out/name)
    link=json.loads((base/'link-command.json').read_text())
    assert link.count(str(base/'native.macho'))==1
    link=[v.replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist')) for v in link]
    (out/'link-command.json').write_text(json.dumps(link,indent=2)+'\n')
    native=out/'native.macho';kernel=out/'kernel.macho'
    cmd=[sys.executable,str(tool/'emit_prelink.py'),'--kernel',str(native),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(kernel)]
    for name in pre['roots']+[profile['added_component']]:cmd+=['--root',name]
    for name in pre['early_security_initializers']:cmd+=['--early-security-init',name]
    (out/'prelink-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    old=json.loads((base/'kernel.json').read_text());report=json.loads((out/'kernel.json').read_text())
    assert set(report['components'])==set(old['components'])|{profile['added_component']}
    assert report['codeless_components']==old['codeless_components']
    expected=dict(pre['original_executable_sha256']);expected[profile['added_component']]=profile['executable_sha256']
    assert verify_original_executables(kernel,report,expected)==31
    assert sha(native)==profile['baseline_native_sha256']
    sys.path.insert(0,str(tool));from native_prelink import macho,backed_offset,unserialize_xml
    image=kernel.read_bytes();segments=macho(image)['segments'];info=next(s for s in segments if s['name']=='__PRELINK_INFO');at=backed_offset(segments,info['va'],info['filesize'],len(image))
    metadata=unserialize_xml(image[at:at+info['filesize']]);entries=[x for x in metadata['_PrelinkInfoDictionary'] if x['CFBundleIdentifier']==profile['added_component']];assert len(entries)==1
    assert entries[0]['IOKitPersonalities']==profile['personalities'] and entries[0]['OSBundleLibraries']==profile['bundle_libraries']
    result={'profile':'v108-network-family','profile_sha256':sha(pp),'baseline_kernel_sha256':profile['baseline_kernel_sha256'],'kernel_sha256':sha(kernel),'native_sha256':sha(native),'native_unchanged':True,'core_filelist_equal':True,'native_link_command_rebased_not_executed':True,'added_component':profile['added_component'],'original_apple_executables_hashed':31,'original_existing30_preserved':True,'network_personalities_preserved':True,'hardware_tested':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
