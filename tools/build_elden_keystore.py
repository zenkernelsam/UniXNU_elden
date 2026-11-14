"""Build software key operations from source over a qualified clean Bluetooth build.

No v99 paths or historical compiled objects are inputs. The optional exit-stack
parent is built from the same clean output by build_bt_exit_stack.py.
"""
from pathlib import Path
import argparse,hashlib,io,json,os,re,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import native_identity,verify_original_executables
from audit_entry_relocation import entry
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'drivers/EldenKeyStore'))
from SourcePatch import service,glue
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
def main():
    if sys.flags.optimize:raise ValueError('Assertions required')
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('clean-build','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--exit-stack',type=Path)
    a=p.parse_args();base=a.clean_build.resolve();out=a.output.resolve();parent=a.exit_stack.resolve() if a.exit_stack else base/'linked'
    if out.exists():raise ValueError('Fresh output required')
    contract=read(ROOT/'profiles/current-build/native-contract.json')
    assert native_identity(base/'linked/native.macho')==(contract['sections'],contract['commands'],contract['symbols'])
    if a.exit_stack:
        contract=read(ROOT/'profiles/bt-exit-stack/native-contract.json')
        assert native_identity(parent/'native.macho')==(contract['sections'],contract['commands'],contract['symbols'])
        assert read(parent/'result.json')['parent_kernel_sha256']==read(base/'linked/result.json')['kernel_sha256']
    parent_report=read(parent/'result.json');assert sha(parent/'kernel.macho')==parent_report['kernel_sha256']
    cap=read(base/'native/capture.json');rows=read(base/'native/build/results.json')
    oldroot=read(ROOT/'profiles/v91/native-build/plan.json')['virtual_root']
    out.mkdir(parents=True);(out/'source').mkdir();objects={};records={};inherited={}
    for i,name,data in [(591,'NativeKeyStoreEvents.cpp',service()),(592,'EldenKeyEventsGlue.cpp',glue())]:
        row=rows[str(i)];old=Path(row['output']);assert row['accepted'] and sha(old)==row['sha256']
        source=out/'source'/name;source.write_bytes(data)
        command=list(row['command']);overlay=read(Path(command[command.index('-ivfsoverlay')+1]));assert overlay['fallthrough'] is False
        bindings=[]
        for r in overlay['roots']:
            f=Path(r['external-contents']);inherited[str(f)]=sha(f)
            if r['name']==row['source'] or r['name'].endswith('/'+name):
                # Exact baseline source; aliases may only bind identical bytes.
                assert sha(f)==cap['files'][row['source']]['sha256']
                r['external-contents']=str(source);bindings.append(r['name'])
        assert bindings
        vfs=out/(name+'.overlay.json');write(vfs,overlay);command[command.index('-ivfsoverlay')+1]=str(vfs)
        obj=out/(name+'.o');command[command.index('-o')+1]=str(obj)
        relative=cap['jobs'][i]['directory'][len(oldroot)+1:]
        assert cap['jobs'][i]['directory'].startswith(oldroot+'/')
        if relative.startswith('research/'):relative='records/'+relative[len('research/'):]
        cwd=base/'native/build/project'/relative
        r=subprocess.run(command,cwd=cwd,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
        if row['objcopy']:
            late=out/(name+'.late.o');subprocess.run([str(a.llvm/'llvm-objcopy'),*row['objcopy'],str(obj),str(late)],check=True);obj=late
        extra={'_native_aks_device_id','_native_aks_key_operations_enabled'} if i==592 else set()
        assert exports(obj)==exports(old)|extra,(name,exports(obj)^exports(old))
        assert not any(n.endswith('/__mod_init_func') for n in sections(obj))
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(obj)],text=True);(out/(name+'.asm')).write_text(asm)
        assert not re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M)
        objects['NativeKeyStoreEvents.late.o' if i==591 else 'EldenKeyEventsGlue.o']=obj
        records[name]={'object':str(obj),'sha256':sha(obj),'source_sha256':sha(source),'command':command,'bindings':bindings,'expected_exports':sorted(exports(obj))}
    oldar=base/'linked/archives/display/libNativeLegacyDisplay.late.a'
    folder=out/'display';folder.mkdir();names=[n for n in subprocess.check_output(['ar','t',oldar],text=True).splitlines() if not n.startswith('__.SYMDEF')]
    assert set(objects)<=set(names);member_hashes={}
    for n in names:
        data=objects[n].read_bytes() if n in objects else subprocess.check_output(['ar','p',oldar,n])
        (folder/n).write_bytes(data);member_hashes[n]=hashlib.sha256(data).hexdigest()
    archive=folder/oldar.name;subprocess.run(['ar','rcs',archive,*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    for n in names:
        data=subprocess.check_output(['ar','p',archive,n]);expected=(folder/n).read_bytes()
        assert data==expected or data==expected+b'\n'*((-len(expected))%8),(n,len(data),len(expected))
    link=read(parent/'link-command.json');assert sum(str(oldar) in v for v in link)==1
    inputs={str(parent/'link.filelist'):sha(parent/'link.filelist')}
    for f in (parent/'link.filelist').read_text().splitlines():inputs[f]=sha(Path(f))
    for arg in link:
        f=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if Path(f).is_file() and f.endswith(('.o','.a')):inputs[f]=sha(Path(f))
    link=[v.replace(str(parent/'native.macho'),str(out/'native.macho')).replace(str(oldar),str(archive)) for v in link]
    write(out/'link-command.json',link);(out/'link.filelist').write_bytes((parent/'link.filelist').read_bytes())
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(link,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert exports(out/'native.macho')==exports(parent/'native.macho')|{'_native_aks_device_id','_native_aks_key_operations_enabled'}
    for f,h in {**inputs,**inherited}.items():assert sha(Path(f))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256']
    kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(base/'dependencies/XnuPrelink'),'archive',pre['commit']]))) as ar:ar.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as f:subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    before=entry((parent/'kernel.macho').read_bytes());after=entry((out/'kernel.macho').read_bytes());assert before[2]==after[2] and before[1]-before[0]==after[1]-after[0]==7124
    source_inputs={str(f.relative_to(ROOT)):sha(f) for f in [ROOT/'drivers/EldenKeyStore/NativeKeyStoreEvents.upstream.cpp',ROOT/'drivers/EldenKeyStore/SourcePatch.py',ROOT/'profiles/v91/sources/tree/scripts/driver-patches/EldenKeyEventsGlue.cpp',Path(__file__).resolve()]}
    write(out/'result.json',{'profile':'elden-software-keystore','key_state':2,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'source_inputs':source_inputs,'changed_native_objects':2,'core_filelist_unchanged':True,'objects':records,'archive_members':member_hashes,'inherited_inputs':inputs,'inherited_sources':inherited,'original_apple_executables_hashed':30,'entry_return_target':hex(after[1]),'parent_kernel_sha256':parent_report['kernel_sha256'],'exit_stack':bool(a.exit_stack),'device_operation':False,'hardware_tested':False})
    print(json.dumps({'kernel_sha256':sha(out/'kernel.macho'),'objects':2,'apple_ranges':30}))
if __name__=='__main__':main()
