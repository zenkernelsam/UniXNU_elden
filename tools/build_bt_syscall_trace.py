"""Rebuild two exact XNU socket/control units over the native phase candidate."""
from pathlib import Path
import argparse,hashlib,io,json,os,re,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import native_identity,verify_original_executables
from audit_entry_relocation import entry
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'drivers/EldenBluetooth/diagnostics'))
from SyscallTrace import source as traced_source,HASHES
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
def vector_inventory(asm):
    result={};function=None
    for line in asm.splitlines():
        label=re.match(r'^[0-9a-f]+ <([^>]+)>:',line)
        if label:function=label[1]
        match=re.match(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.*)',line)
        if match and re.search(r'\b[qvdshb]\d+(?:\b|\.)',match[1]):
            # Stack frame offsets can change around added printf calls; record
            # the instruction/operand shape while preserving registers.
            shape=re.sub(r'#-?(?:0x[0-9a-f]+|[0-9]+)','@immediate',match[1])
            if function=='_ctl_ioctl':
                # The original ctl_info bcopy keeps the same12 vector ops;
                # printf changes its data-pointer allocation x19 -> x20 and
                # moves a former zero stack offset. Source edits are reversible.
                shape=re.sub(r'\[x(?:19|20)(?=[,\]])','[data',shape)
                shape=shape.replace('[sp]','[sp, @immediate]')
            result.setdefault(function,[]).append(shape)
    if None in result:raise ValueError('Unattributed vector instruction')
    return result
def main():
    if sys.flags.optimize:raise ValueError('Assertions required')
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('clean-build','transport-build','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.clean_build.resolve();parent=a.transport_build.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    contract=read(ROOT/'profiles/bt-transport-trace/native-contract.json')
    assert native_identity(parent/'native.macho')==(contract['sections'],contract['commands'],contract['symbols'])
    parent_report=read(parent/'result.json');assert sha(parent/'kernel.macho')==parent_report['kernel_sha256']
    cap=read(base/'xnu/capture.json');rows=read(base/'xnu/build/results.json');oldroot=read(ROOT/'profiles/v91/object-build/plan.json')['virtual_root']
    out.mkdir(parents=True);replacements={};records={};inherited={}
    for i,name in [(652,'uipc_syscalls.c'),(583,'kern_control.c')]:
        row=rows[str(i)];old=Path(row['output']);assert row['accepted'] and sha(old)==row['sha256']
        original=base/'source/xnu/bsd/kern'/name;data=traced_source(name,original.read_bytes())
        target=out/name;target.write_bytes(data);cmd=list(row['command']);overlay=read(Path(cmd[cmd.index('-ivfsoverlay')+1]));assert overlay['fallthrough'] is False
        changed=[]
        for r in overlay['roots']:
            f=Path(r['external-contents']);inherited[str(f)]=sha(f)
            if r['name'].endswith('/'+name):
                assert sha(f)==HASHES[name];r['external-contents']=str(target);changed.append(r['name'])
        assert changed
        ov=out/(name+'.overlay.json');write(ov,overlay);cmd[cmd.index('-ivfsoverlay')+1]=str(ov)
        obj=out/(name+'.o');cmd[cmd.index('-o')+1]=str(obj)
        directory=cap['jobs'][i]['directory'];assert directory.startswith(oldroot+'/');relative=directory[len(oldroot)+1:]
        if relative.startswith('research/'):relative='records/'+relative[len('research/'):]
        r=subprocess.run(cmd,cwd=base/'xnu/build/project'/relative,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise ValueError(name+' failed: '+r.stderr.decode(errors='replace')[-2000:])
        assert exports(obj)==exports(old) and not any(n.endswith('/__mod_init_func') for n in sections(obj))
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(obj)],text=True);(out/(name+'.asm')).write_text(asm)
        previous=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(old)],text=True)
        before_vectors=vector_inventory(previous);after_vectors=vector_inventory(asm)
        write(out/(name+'.vectors.json'),{'before':before_vectors,'after':after_vectors})
        assert before_vectors==after_vectors,'Changed vector instruction shape or newly vectorized helper'
        assert 'unsupported' not in r.stderr.decode(errors='replace').lower()
        replacements[str(old)]=str(obj);records[name]={'object':str(obj),'sha256':sha(obj),'source_sha256':sha(target),'command':cmd,'bindings':changed,'original_vector_shapes_preserved':True,'trace_helper_scalar':True}
    oldlist=(parent/'link.filelist').read_text().splitlines();assert all(oldlist.count(n)==1 for n in replacements)
    filelist=out/'link.filelist';filelist.write_text('\n'.join(replacements.get(n,n) for n in oldlist)+'\n')
    link=read(parent/'link-command.json');old_filelists=[x.split(',')[-1] for x in link if x.endswith('link.filelist')];assert len(old_filelists)==1
    inputs={n:sha(Path(n)) for n in oldlist}
    for arg in link:
        f=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if Path(f).is_file() and f.endswith(('.o','.a')):inputs[f]=sha(Path(f))
    cmd=[x.replace(old_filelists[0],str(filelist)).replace(str(parent/'native.macho'),str(out/'native.macho')) for x in link]
    write(out/'link-command.json',cmd);env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(cmd,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert exports(out/'native.macho')==exports(parent/'native.macho')
    for f,h in {**inputs,**inherited}.items():assert sha(Path(f))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256'];kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(base/'dependencies/XnuPrelink'),'archive',pre['commit']]))) as ar:ar.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as f:subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    before=entry((parent/'kernel.macho').read_bytes());after=entry((out/'kernel.macho').read_bytes());assert before[2]==after[2] and before[1]-before[0]==after[1]-after[0]==7124
    source_inputs={str(f.relative_to(ROOT)):sha(f) for f in [ROOT/'drivers/EldenBluetooth/diagnostics/SyscallTrace.py',ROOT/'drivers/EldenBluetooth/diagnostics/SyscallTrace.inc',Path(__file__).resolve()]}
    write(out/'result.json',{'profile':'bt-syscall-trace','key_state':2,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'source_inputs':source_inputs,'changed_core_objects':2,'objects':records,'inherited_inputs':inputs,'inherited_sources':inherited,'original_apple_executables_hashed':30,'entry_return_target':hex(after[1]),'parent_kernel_sha256':parent_report['kernel_sha256'],'device_operation':False,'hardware_tested':False})
    print(json.dumps({'kernel_sha256':sha(out/'kernel.macho'),'objects':2,'apple_ranges':30}))
if __name__=='__main__':main()
