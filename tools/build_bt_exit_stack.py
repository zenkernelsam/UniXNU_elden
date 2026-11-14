"""Rebuild the exit observer over an output of build_clean, without private builds.

The parent kernel must match the published Bluetooth source contract. Only one
new core object is linked; all existing objects and source inputs stay intact.
"""
from pathlib import Path
import argparse,hashlib,io,json,os,re,subprocess,sys,tarfile
from prepare_bt_exit_stack_source import derive
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import native_identity,verify_original_executables
from audit_entry_relocation import entry
ROOT=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
def main():
    if sys.flags.optimize:raise ValueError('Assertions required')
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('clean-build','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.clean_build.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    contract=read(ROOT/'profiles/current-build/native-contract.json');sec,cmd,sym=native_identity(base/'linked/native.macho')
    if (sec,cmd,sym)!=(contract['sections'],contract['commands'],contract['symbols']):raise ValueError('Not the source-built Bluetooth baseline')
    units=read(base/'units/result.json');row=units['units']['kern_exec'];old=Path(row['object']);assert row['accepted'] and sha(old)==row['sha256']
    original_report=read(base/'linked/result.json');assert sha(base/'linked/kernel.macho')==original_report['kernel_sha256']
    command=list(row['command']);overlay=read(Path(command[command.index('-ivfsoverlay')+1]));assert overlay['fallthrough'] is False
    out.mkdir(parents=True);source=out/'EldenIconCpuCommon.inc';source.write_bytes(derive());changed=[];inherited_sources={}
    for r in overlay['roots']:
        f=Path(r['external-contents']);h=sha(f);inherited_sources[str(f)]=h
        if h=='beea14cef600c7607a9d381cde30647ec806ec580624c22c37ead94715144dec':r['external-contents']=str(source);changed.append(r['name'])
    assert changed
    vfs=out/'overlay.json';write(vfs,overlay);command[command.index('-ivfsoverlay')+1]=str(vfs)
    obj=out/'kern_exec.o';command[command.index('-o')+1]=str(obj)
    r=subprocess.run(command,cwd=base/'units/working/kern_exec',capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    assert exports(obj)==exports(old) and not any(n.endswith('/__mod_init_func') for n in sections(obj))
    asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(obj)],text=True);(out/'kern_exec.asm').write_text(asm)
    assert not re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M)
    assert 'ELDEN_BT_EXIT_STACK' in str(source.read_bytes())
    oldlist=(base/'linked/link.filelist').read_text().splitlines();assert oldlist.count(str(old))==1
    newlist=[str(obj) if n==str(old) else n for n in oldlist];(out/'link.filelist').write_text('\n'.join(newlist)+'\n')
    oldlink=read(base/'linked/link-command.json');inputs={str(Path(x)):sha(Path(x)) for x in oldlist}
    for arg in oldlink:
        f=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if Path(f).is_file() and f.endswith(('.o','.a')):inputs[f]=sha(Path(f))
    link=[x.replace(str(base/'linked/link.filelist'),str(out/'link.filelist')).replace(str(base/'linked/native.macho'),str(out/'native.macho')).replace(str(old),str(obj)) for x in oldlink];write(out/'link-command.json',link)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(link,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert exports(out/'native.macho')==exports(base/'linked/native.macho')
    for f,h in inputs.items():assert sha(Path(f))==h
    for f,h in inherited_sources.items():assert sha(Path(f))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256']
    kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(base/'dependencies/XnuPrelink'),'archive',pre['commit']]))) as archive:archive.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as f:subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    oldentry=entry((base/'linked/kernel.macho').read_bytes());newentry=entry((out/'kernel.macho').read_bytes());assert oldentry[2]==newentry[2] and newentry[1]-newentry[0]==oldentry[1]-oldentry[0]==7124
    source_inputs={str(f.relative_to(ROOT)):sha(f) for f in (ROOT/'drivers/EldenBluetooth/diagnostics/ExitStack.inc',ROOT/'tools/prepare_bt_exit_stack_source.py',Path(__file__).resolve())}
    report={'profile':'bt-exit-stack','kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
      'source_sha256':sha(source),'source_inputs':source_inputs,'changed_core_objects':1,'expected_exports_preserved':True,'new_object_scalar':True,'original_apple_executables_hashed':30,
      'entry_return_target':hex(newentry[1]),'parent_kernel_sha256':original_report['kernel_sha256'],'parent_source_build':str(base),
      'object':{'path':str(obj),'sha256':sha(obj),'command':command,'changed_virtual_bindings':changed},'inherited_inputs':inputs,'inherited_sources':inherited_sources,
      'device_operation':False,'hardware_tested':False}
    write(out/'result.json',report);print(json.dumps({k:v for k,v in report.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
