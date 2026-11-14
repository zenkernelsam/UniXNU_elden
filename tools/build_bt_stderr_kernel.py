"""Add one bounded exact-daemon stderr observer to frozen v119; no device I/O."""
from pathlib import Path
import argparse,hashlib,io,json,os,re,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','core','compile-cwd','prelink-repository','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.baseline.resolve();out=a.output.resolve();profile=ROOT/'profiles/v121-bt-stderr';plan=read(profile/'plan.json')
    if out.exists():raise ValueError('Fresh output required')
    for n in ('kernel','native'):assert sha(base/(n+'.macho'))==plan['baseline_'+n+'_sha256']
    old=read(a.core/'build/results.json')['627'];assert old['accepted'] and sha(Path(old['output']))==plan['object_parent_sha256']
    command=list(old['command']);vfs=read(Path(command[command.index('-ivfsoverlay')+1]));assert vfs['fallthrough'] is False
    rows=[r for r in vfs['roots'] if r['name']==old['source']];assert len(rows)==1;parent=Path(rows[0]['external-contents']);assert sha(parent)==plan['source_parent_sha256']
    fragment=(ROOT/'drivers/EldenBluetooth/userland/StderrObserve.inc').read_bytes();assert hashlib.sha256(fragment).hexdigest()==plan['observer_sha256']
    data=parent.read_bytes();marker=b'static int\nwritev_internal(';assert data.count(marker)==1;data=data.replace(marker,fragment+b'\n'+marker)
    marker=b'\tif ((error = preparefilewrite(p, &fp, fd, flags & FOF_OFFSET, puguard))) {';assert data.count(marker)==1
    data=data.replace(marker,b'\telden_bt_stderr_observe(p,fd,uio);\n\n'+marker)
    out.mkdir(parents=True);source=out/'sys_generic.c';source.write_bytes(data);inputs={}
    for row in vfs['roots']:
        path=Path(row['external-contents'])
        if path.is_file():inputs[str(path)]=sha(path)
        if str(path)==str(parent):row['external-contents']=str(source)
    overlay=out/'overlay.json';write(overlay,vfs);command[command.index('-ivfsoverlay')+1]=str(overlay)
    obj=out/'sys_generic.o';command[command.index('-o')+1]=str(obj)
    command+=['-target-feature','-neon','-target-feature','-fp-armv8']
    r=subprocess.run(command,cwd=a.compile_cwd,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    assert exports(obj)==exports(Path(old['output']))
    assert not any(n.endswith('/__mod_init_func') for n in sections(obj))
    dis=[str(a.llvm/'llvm-objdump'),'--disassemble','--reloc']
    asm=subprocess.check_output(dis+[str(obj)],text=True);(out/'sys_generic.asm').write_text(asm)
    prior=subprocess.check_output(dis+[old['output']],text=True)
    def vectors(s):return re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.).*$',s,re.M)
    assert not vectors(asm)
    link=read(base/'link-command.json');files=(base/'link.filelist').read_text().splitlines();assert files.count(old['output'])==1
    inherited={str(Path(f)):sha(Path(f)) for f in files};files[files.index(old['output'])]=str(obj);(out/'link.filelist').write_text('\n'.join(files)+'\n')
    for arg in link:
        path=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if Path(path).is_file() and path.endswith(('.a','.o')):inherited[path]=sha(Path(path))
    link=[v.replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist')).replace(old['output'],str(obj)) for v in link];write(out/'link-command.json',link)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(link,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert exports(out/'native.macho')==exports(base/'native.macho')
    for path,h in inherited.items():assert sha(Path(path))==h
    for path,h in inputs.items():assert sha(Path(path))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256'];kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for v in pre['roots']:cmd+=['--root',v]
    for v in pre['early_security_initializers']:cmd+=['--early-security-init',v]
    with (out/'prelink.log').open('x') as f:subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    r={'profile':'v121-bt-stderr','profile_sha256':sha(profile/'plan.json'),'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
       'replacement':{'before':old['output'],'before_sha256':old['sha256'],'after':str(obj),'sha256':sha(obj),'source_sha256':sha(source),'command':command},
       'inherited_inputs':inherited,'compiler_inputs':inputs,'original_apple_executables_hashed':30,'hardware_tested':False}
    write(out/'result.json',r);print(json.dumps({k:v for k,v in r.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
