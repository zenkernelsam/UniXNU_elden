"""Integrate one SPMI gate and three consumers over the frozen v110 kernel."""
from pathlib import Path
import argparse,hashlib,importlib.util,io,json,os,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    if sys.flags.optimize:raise SystemExit('Assertions required')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','components','prelink-repository','original-kernelcache','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();base=a.baseline.resolve();c=a.components.resolve();out=a.output.resolve();pp=ROOT/'profiles/v111-spmi-gate/plan.json';plan=read(pp)
    if out.exists():raise ValueError('Fresh output required')
    for n in ('kernel','native'):assert sha(base/(n+'.macho'))==plan['baseline_'+n+'_sha256']
    cr=read(c/'result.json');assert cr['baseline_sha256']==plan['baseline_kernel_sha256']
    patcher=ROOT/'drivers/EldenSpmi/SourcePatch.py';assert sha(patcher)==cr['patcher_sha256']==plan['patcher_sha256']
    sp=importlib.util.spec_from_file_location('spmi_patch',patcher);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
    sources={'Spmi.c':ROOT/'profiles/v91/sources/power/Spmi.c','BtPmic.c':base/'source/BtPmic.c','WcnPowerNative.cpp':base/'source/WcnPowerNative.cpp'}
    for n,p in sources.items():assert (c/'source'/n).read_bytes()==m.apply(n,p.read_bytes())
    for n in ('TransactionGate.c','TransactionGate.h'):assert (c/'source'/n).read_bytes()==(ROOT/'drivers/EldenSpmi'/n).read_bytes()
    for n in ('Spmi','BtPmic','WcnPowerNative','TransactionGate'):assert sha(c/(n+'.o'))==cr['components'][n]['object_sha256']
    link=read(base/'link-command.json');out.mkdir(parents=True);archive_records={};replace_paths={};inherited={}
    for arg in link:
        path=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if not path.endswith('.a') or not Path(path).is_file():continue
        old=Path(path);inherited[path]=sha(old)
        changes={'Spmi.o':c/'Spmi.o'} if old.name=='libEldenPmicClient.a' else {'BtPmic.o':c/'BtPmic.o','WcnPowerNative.o':c/'WcnPowerNative.o'} if old.name=='libEldenBluetooth.a' else {}
        if not changes:continue
        folder=out/old.stem;folder.mkdir();names=[n for n in subprocess.check_output(['/usr/bin/ar','t',str(old)],text=True).splitlines() if not n.startswith('__.SYMDEF')];assert len(set(names))==len(names) and set(changes)<=set(names)
        rows={}
        for n in names:
            prior=subprocess.check_output(['/usr/bin/ar','p',str(old),n]);data=changes[n].read_bytes() if n in changes else prior
            (folder/n).write_bytes(data);rows[n]={'before':hashlib.sha256(prior).hexdigest(),'source':hashlib.sha256(data).hexdigest(),'changed':n in changes}
        dest=folder/old.name;subprocess.run(['/usr/bin/ar','rcs',str(dest),*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
        for n in names:
            data=(folder/n).read_bytes();stored=subprocess.check_output(['/usr/bin/ar','p',str(dest),n]);assert stored==data+b'\n'*(-len(data)%8)
            rows[n]['after']=hashlib.sha256(stored).hexdigest()
            if n not in changes:assert rows[n]['after']==rows[n]['before']
        archive_records[old.name]={'before':str(old),'after':str(dest),'members':rows};replace_paths[path]=str(dest)
    assert set(archive_records)=={'libEldenPmicClient.a','libEldenBluetooth.a'}
    filelist=(base/'link.filelist').read_bytes();(out/'link.filelist').write_bytes(filelist)
    for name in filelist.decode().splitlines():inherited[name]=sha(Path(name))
    assert link.count(str(base/'native.macho'))==1
    new=[]
    for arg in link:
        value=arg.replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist'))
        for old,dest in replace_paths.items():value=value.replace(old,dest)
        new.append(value)
    new.append(str(c/'TransactionGate.o'));write(out/'link-command.json',new)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as log:subprocess.run(new,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
    gate={'_elden_spmi_transaction_acquire','_elden_spmi_transaction_release','_elden_spmi_transaction_state'}
    assert exports(out/'native.macho')==exports(base/'native.macho')|gate
    for p,h in inherited.items():assert sha(Path(p))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256'];kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    result={'profile':'v111-spmi-gate','profile_sha256':sha(pp),'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'components_sha256':sha(c/'result.json'),'components':cr,'core_filelist_equal':True,'archives':archive_records,'inherited_inputs':inherited,'original_apple_executables_hashed':30,'hardware_tested':False}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
