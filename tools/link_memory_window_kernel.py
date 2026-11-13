"""Link the three audited memory-window objects over the verified v108 kernel."""
import argparse,hashlib,io,json,os,subprocess,sys,tarfile
from pathlib import Path
from link_v91_kernel import verify_original_executables
from replay_xnu_objects import global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    if sys.flags.optimize:raise SystemExit('Validation requires assertions')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','components','core','rt-build','prelink-repository','original-kernelcache','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output.resolve();base=a.baseline.resolve();c=a.components.resolve();core=a.core.resolve();rt=a.rt_build.resolve();profile=ROOT/'profiles/v109-memory8/plan.json';plan=read(profile)
    if out.exists():raise ValueError('New output required')
    for n in ('native','kernel'):assert sha(base/(n+'.macho'))==plan['baseline_'+n+'_sha256']
    cr=read(c/'result.json');assert cr['bootstrap_pages']==24 and len(cr['assembly_changes'])==3
    records=read(core/'build/results.json');filelist=(base/'link.filelist').read_text().splitlines();replacements={}
    for index,name in ((266,'pmap'),(293,'arm_vm_init'),(304,'start')):
        old=rt/'objects/pmap.o' if index==266 else Path(records[str(index)]['output']);new=c/(name+'.o')
        assert sha(old)==cr['objects'][name]['before_sha256'] and sha(new)==cr['objects'][name]['sha256'] and filelist.count(str(old))==1
        replacements[str(old)]=str(new)
    out.mkdir(parents=True);inherited={p:sha(Path(p)) for p in filelist if p not in replacements}
    (out/'link.filelist').write_text('\n'.join(replacements.get(p,p) for p in filelist)+'\n')
    link=read(base/'link-command.json');assert link.count(str(base/'native.macho'))==1
    archives={}
    for item in link:
        path=item.split(',',2)[2] if item.startswith('-Wl,-force_load,') else item
        if path.endswith('.a') and Path(path).is_file():archives[path]=sha(Path(path))
    link=[v.replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist')) for v in link];write(out/'link-command.json',link)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as log:subprocess.run(link,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
    assert exports(out/'native.macho')==exports(base/'native.macho')
    for p,h in {**inherited,**archives}.items():assert sha(Path(p))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];np=read(ROOT/'profiles/v108-network-family/plan.json')
    assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256']
    kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']+[np['added_component']]:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    expected=dict(pre['original_executable_sha256']);expected[np['added_component']]=np['executable_sha256'];assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),expected)==31
    result={'profile':'v109-memory8','profile_sha256':sha(profile),'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),'changed_core_jobs':[266,293,304],'component_report_sha256':sha(c/'result.json'),'replacements':replacements,'inherited_core_inputs':inherited,'inherited_archives':archives,'original_apple_executables_hashed':31,'bootstrap_pages':24,'memory_window_bytes':0x200000000,'hardware_tested':False}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
