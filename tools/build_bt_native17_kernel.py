"""Replace only two runtime core objects over verified v117; no device I/O."""
from pathlib import Path
import argparse, hashlib, io, json, os, re, shutil, subprocess, sys, tarfile
from replay_xnu_objects import global_symbols, sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
def main():
    if sys.flags.optimize:raise ValueError('Assertions required')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',choices=('v118-native17','v119-bt-exit','v120-bt-xpc'),default='v118-native17')
    for n in ('baseline','core-build','compile-cwd','runtime','prelink-repository','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.baseline.resolve();core=a.core_build.resolve();runtime=a.runtime.resolve();out=a.output.resolve()
    profile=ROOT/'profiles'/a.profile;plan=read(profile/'plan.json');rr=read(runtime/'report.json')
    if out.exists():raise ValueError('Fresh output required')
    for n in ('kernel','native'):assert sha(base/(n+'.macho'))==plan['baseline_'+n+'_sha256']
    assert sha(core/'kernel.macho')==plan['core_build_kernel_sha256']
    assert sha(runtime/'EldenBtRuntimePayload.h')==plan['runtime_header_sha256']
    assert sha(runtime/'EldenBtTextGuard.h')==plan['guard_sha256']
    assert sha(runtime/'BlueTool')==plan['runtime_helper_sha256'] and sha(runtime/'runtime.trustcache')==plan['runtime_trust_sha256']
    assert rr['table_count']==(31 if a.profile=='v120-bt-xpc' else 22) and rr['original_bluetoothd_cdhash']=='fe7bc182386a2766c7574f8b48412ff9df113564'
    out.mkdir(parents=True);src=out/'source';src.mkdir();objects=out/'objects';objects.mkdir();records={};input_hashes={}
    prior=read(core/'object-audit.json')
    for name in ('EldenBtRuntimePayload.h','EldenBtTextGuard.h'):shutil.copyfile(runtime/name,src/name)
    if a.profile in ('v119-bt-exit','v120-bt-xpc'):
        observer=ROOT/'profiles/v91/sources/xnu/EldenIconCpuCommon.inc'
        assert sha(observer)==plan['observer_parent_sha256']
        body=observer.read_bytes();marker=b'    if (!elden_icon_cpu_process_selected(p)) return 0;'
        assert body.count(marker)==1
        addition=(profile/'observer.inc').read_bytes();assert sha(profile/'observer.inc')==plan['observer_addition_sha256']
        body=body.replace(marker,addition+b'\n'+marker)
        if a.profile=='v120-bt-xpc':
            marker=b'    if(code==ELDEN_ICON_GLYPH_TRACE_CODE){'
            addition=(profile/'marker.inc').read_bytes();assert sha(profile/'marker.inc')==plan['marker_sha256'] and body.count(marker)==1
            body=body.replace(marker,addition+b'\n'+marker)
        (src/'EldenIconCpuCommon.inc').write_bytes(body)
    for name in ('kern_exec','ubc_subr'):
        old=prior[name];assert sha(Path(old['file']))==old['sha256']
        command=list(old['command']);overlay=read(Path(command[command.index('-ivfsoverlay')+1]));assert overlay['fallthrough'] is False
        original=core/'source'/(name+'.c');assert sha(original)==plan['core_source_sha256'][name]
        body=original.read_bytes()
        if name=='ubc_subr':
            before=b'blob->csb_end_offset - relative < 4 || !elden_bt_stream_identity'
            assert body.count(before)==1
            body=body.replace(before,b'blob->csb_end_offset - relative < f->length || !elden_bt_stream_identity')
        (src/(name+'.c')).write_bytes(body)
        replacements={str(core/'source'/(name+'.c')):src/(name+'.c'),
                      str(core/'source/EldenBtRuntimePayload.h'):src/'EldenBtRuntimePayload.h',
                      str(core/'source/EldenBtTextGuard.h'):src/'EldenBtTextGuard.h'}
        replaced=set()
        for row in overlay['roots']:
            oldpath=row['external-contents']
            if Path(oldpath).is_file():input_hashes[oldpath]=sha(Path(oldpath))
            if oldpath in replacements:
                row['external-contents']=str(replacements[oldpath]);replaced.add(oldpath)
            if a.profile in ('v119-bt-exit','v120-bt-xpc') and name=='kern_exec' and oldpath.endswith('/EldenIconCpuCommon.inc'):
                assert sha(Path(oldpath))==plan['observer_parent_sha256']
                row['external-contents']=str(src/'EldenIconCpuCommon.inc')
        assert str(original) in replaced and len(replaced)==3
        # The virtual source include paths resolve through the unchanged closed
        # overlay; no extra directory or fallback headers are introduced.
        vf=out/(name+'-overlay.json');write(vf,overlay);command[command.index('-ivfsoverlay')+1]=str(vf)
        obj=objects/(name+'.o');command[command.index('-o')+1]=str(obj)
        assert a.compile_cwd.is_dir() and (a.compile_cwd/'meta_features.h').is_file()
        r=subprocess.run(command,cwd=a.compile_cwd,capture_output=True)
        (out/(name+'-compile.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
        assert exports(obj)==set(old['exports'])
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--disassemble','--reloc',str(obj)],text=True)
        (out/(name+'.asm')).write_text(asm)
        assert not re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M)
        assert not any(n.endswith('/__mod_init_func') for n in sections(obj))
        baseline_object=Path(read(base/'result.json')['core_replacements'][name]['after']) if a.profile in ('v119-bt-exit','v120-bt-xpc') else Path(old['file'])
        records[name]={'before':str(baseline_object),'before_sha256':sha(baseline_object),'after':str(obj),'sha256':sha(obj),
                       'source_sha256':sha(src/(name+'.c')),'command':command,'exports':sorted(exports(obj))}
    filelist=(base/'link.filelist').read_text().splitlines();newlist=list(filelist)
    for name,row in records.items():
        assert newlist.count(row['before'])==1
        newlist[newlist.index(row['before'])]=row['after']
    (out/'link.filelist').write_text('\n'.join(newlist)+'\n')
    link=read(base/'link-command.json');inherited={}
    for arg in link:
        path=arg.split(',',2)[2] if arg.startswith('-Wl,-force_load,') else arg
        if Path(path).is_file() and path.endswith(('.a','.o')):inherited[path]=sha(Path(path))
    for path in filelist:inherited[path]=sha(Path(path))
    command=[v.replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(base/'link.filelist'),str(out/'link.filelist')) for v in link]
    for row in records.values():command=[v.replace(row['before'],row['after']) for v in command]
    write(out/'link-command.json',command)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(command,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert exports(out/'native.macho')==exports(base/'native.macho')
    for path,h in inherited.items():assert sha(Path(path))==h
    for path,h in input_hashes.items():assert sha(Path(path))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256']
    kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as tar:tar.extractall(kit,filter='data')
    command=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for v in pre['roots']:command+=['--root',v]
    for v in pre['early_security_initializers']:command+=['--early-security-init',v]
    with (out/'prelink.log').open('x') as f:subprocess.run(command,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])==30
    result={'profile':a.profile,'profile_sha256':sha(profile/'plan.json'),'kernel_sha256':sha(out/'kernel.macho'),
            'native_sha256':sha(out/'native.macho'),'baseline_kernel_sha256':plan['baseline_kernel_sha256'],
            'core_replacements':records,'inherited_inputs':inherited,'compiler_inputs':input_hashes,
            'original_apple_executables_hashed':30,'hardware_tested':False,'runtime':rr}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
