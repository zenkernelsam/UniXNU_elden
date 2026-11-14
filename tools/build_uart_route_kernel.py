"""Replace two UART objects over v111, keeping the shared SPMI gate intact."""
from pathlib import Path
import argparse,hashlib,io,json,os,re,shutil,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    if sys.flags.optimize:raise SystemExit('Assertions required')
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile',choices=('v112-uart-route','v113-uart-gic','v114-hci-length','v115-ibs-wake','v116-hci-reset','v117-hci-debug'),default='v112-uart-route')
    for n in ('baseline','uart-build','prelink-repository','original-kernelcache','llvm','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();base=a.baseline.resolve();old=a.uart_build.resolve();out=a.output.resolve();pp=ROOT/'profiles'/a.profile/'plan.json';plan=read(pp)
    if a.profile=='v113-uart-gic':
        dts=(ROOT/'soc/sm8750/dts/sm8750.dtsi').read_text();header=(pp.parent/'sources/EldenBluetoothGic.h').read_text()
        gic=int(re.search(r'#define ELDEN_BT_GICD_BASE (0x[0-9a-f]+)',header)[1],16)
        match=re.search(r'intc: interrupt-controller@([0-9a-f]+).*?reg = <0x0 (0x[0-9a-f]+)',dts,re.S)
        assert match and gic==int(match[1],16)==int(match[2],16)==0x16000000
    if out.exists():raise ValueError('Fresh output required')
    for n in ('kernel','native'):assert sha(base/(n+'.macho'))==plan['baseline_'+n+'_sha256']
    assert sha(old/'kernel.macho')=='0db8f52b3d85026f1a85a000ab665c47cf084a340e87c09f556900b5355ee848'
    out.mkdir(parents=True);src=out/'source';shutil.copytree(old/'source',src);prior=read(old/'object-audit.json');records={}
    for n,h in plan['sources'].items():
        p=pp.parent/'sources'/n;assert sha(p)==h;shutil.copyfile(p,src/n)
    units=plan.get('replacement_units',['UartPlatform','UartBackend'])
    assert units in (['UartPlatform','UartBackend'],['Wcn7861Hci'],['Wcn7861Startup'],['WcnPowerNative'])
    for n in units:
        command=list(prior[n]['command']);oldroot=str(old/'source');newroot=str(src);command=[x.replace(oldroot,newroot) for x in command];obj=out/(n+'.o');command[command.index('-o')+1]=str(obj);cwd=out
        if '-ivfsoverlay' in command:
            i=command.index('-ivfsoverlay')+1;ov=read(Path(command[i]));assert ov.get('fallthrough') is False
            for row in ov['roots']:
                row['name']=row['name'].replace(oldroot,newroot);row['external-contents']=row['external-contents'].replace(oldroot,newroot)
            known={r['name'] for r in ov['roots']}
            for f in src.iterdir():
                if f.is_file() and str(f) not in known:ov['roots'].append({'type':'file','name':str(f),'external-contents':str(f)})
            dirs=[v.split('=',1)[1] for v in command if v.startswith('-fdebug-compilation-dir=')];assert len(dirs)==1
            matches=[x for x in ov['roots'] if x['name']==dirs[0]+'/meta_features.h'];assert len(matches)==1;cwd=Path(matches[0]['external-contents']).parent
            overlay=out/(n+'-overlay.json');write(overlay,ov);command[i]=str(overlay)
        env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
        with (out/(n+'.log')).open('x') as log:subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(obj)],text=True);(out/(n+'.asm')).write_text(asm)
        assert not re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M)
        assert not any(n.endswith('/__mod_init_func') for n in sections(obj))
        exports={n for n,v in global_symbols(obj).items() if v[0]&0x0e};assert exports==set(prior[n]['exports'])
        records[n]={'sha256':sha(obj),'source_sha256':sha(src/(n+('.cpp' if n in ('UartBackend','WcnPowerNative') else '.c'))),'command':command}
    link=read(base/'link-command.json');paths=[v.split(',',2)[2] for v in link if v.startswith('-Wl,-force_load,') and v.endswith('/libEldenBluetooth.a')];assert len(paths)==1;ar=Path(paths[0]);folder=out/'bluetooth';folder.mkdir();members={};names=[n for n in subprocess.check_output(['/usr/bin/ar','t',str(ar)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
    assert len(set(names))==len(names)
    for n in names:
        data=subprocess.check_output(['/usr/bin/ar','p',str(ar),n]);members[n]={'before':hashlib.sha256(data).hexdigest()}
        if n in {u+'.o' for u in units}:data=(out/n).read_bytes()
        (folder/n).write_bytes(data)
    newar=folder/ar.name;subprocess.run(['/usr/bin/ar','rcs',str(newar),*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    for n in names:
        data=(folder/n).read_bytes();stored=subprocess.check_output(['/usr/bin/ar','p',str(newar),n]);assert stored==data+b'\n'*(-len(data)%8);members[n]['after']=hashlib.sha256(stored).hexdigest()
    assert {n for n,v in members.items() if v['before']!=v['after']}=={u+'.o' for u in units}
    inherited={p:sha(Path(p)) for p in (base/'link.filelist').read_text().splitlines()}
    for value in link:
        path=value.split(',',2)[2] if value.startswith('-Wl,-force_load,') else value
        if path.endswith(('.a','.o')) and Path(path).is_file():inherited[path]=sha(Path(path))
    shutil.copyfile(base/'link.filelist',out/'link.filelist');link=[v.replace(str(ar),str(newar)).replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist')) for v in link];write(out/'link-command.json',link)
    with (out/'link.log').open('x') as log:subprocess.run(link,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    for p,h in inherited.items():assert sha(Path(p))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256'];kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    result={'profile':a.profile,'profile_sha256':sha(pp),'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'objects':records,'members':members,'inherited_inputs':inherited,'core_filelist_equal':True,'original_apple_executables_hashed':30,'hardware_tested':False}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
