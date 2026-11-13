"""Build one bounded IOKit observer into the qualified v106 display archive."""
from pathlib import Path
import argparse,hashlib,json,io,os,re,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','glue-build','prelink-repository','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();base=a.baseline.resolve();profile=ROOT/'profiles/v107-iokit-busy';plan=read(profile/'plan.json')
    if out.exists():raise ValueError('New build required')
    for n in ('kernel','native'):
        if sha(base/(n+'.macho'))!=plan['baseline_'+n+'_sha256']:raise ValueError('Wrong baseline')
    source=profile/'sources/EldenDisplayGlue.cpp'
    if sha(source)!=plan['glue_source_sha256']:raise ValueError('Observer source differs')
    prior=read(a.glue_build/'native/build/results.json')['2'];cap=read(a.glue_build/'native/capture.json')
    old_source=cap['files'][prior['source']]
    if old_source['sha256']!=plan['glue_source_parent_sha256'] or sha(Path(prior['output']))!=prior['sha256']:raise ValueError('Wrong glue parent')
    out.mkdir(parents=True);command=list(prior['command']);pos=command.index('-ivfsoverlay')+1;overlay=read(Path(command[pos]))
    if overlay.get('fallthrough') is not False:raise ValueError('Expected closed source overlay')
    matches=[e for e in overlay['roots'] if e['name']==prior['source']]
    if len(matches)!=1 or sha(Path(matches[0]['external-contents']))!=plan['glue_source_parent_sha256']:raise ValueError('Source binding mismatch')
    old=matches[0]['external-contents']
    for e in overlay['roots']:
        if e['external-contents']==old:e['external-contents']=str(source)
    vfs=out/'source-overlay.json';write(vfs,overlay);command[pos]=str(vfs);obj=out/'EldenDisplayGlue.o';command[command.index('-o')+1]=str(obj)
    job=cap['jobs'][2];relative=job['directory'].split('/research/',1)[1];cwd=(a.glue_build/'native/build/project/records'/relative).resolve(strict=True)
    with (out/'compile.log').open('x') as log:subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
    if exports(obj)!=exports(Path(prior['output'])):raise ValueError('Glue exports changed')
    asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-d','--reloc',str(obj)],text=True);(out/'glue.asm').write_text(asm)
    if re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M):raise ValueError('Unexpected glue FP/SIMD')
    link=read(base/'link-command.json');display=[v for v in link if v.startswith('-Wl,-force_load,') and v.endswith('/libNativeLegacyDisplay.late.a')]
    if len(display)!=1:raise ValueError('Ambiguous display archive')
    oldarchive=Path(display[0].split(',',2)[2]);folder=out/'display';folder.mkdir();records={}
    names=[s for s in subprocess.check_output(['/usr/bin/ar','t',str(oldarchive)],text=True).splitlines() if not s.startswith('__.SYMDEF')]
    if names.count(plan['archive_member'])!=1 or len(names)!=len(set(names)):raise ValueError('Invalid archive members')
    for name in names:
        content=subprocess.check_output(['/usr/bin/ar','p',str(oldarchive),name]);before=hashlib.sha256(content).hexdigest()
        if name==plan['archive_member']:
            oldbytes=Path(prior['output']).read_bytes()
            if content!=oldbytes+b'\n'*(-len(oldbytes)%8):raise ValueError('Original glue object differs')
            content=obj.read_bytes()
        (folder/name).write_bytes(content);records[name]={'before':before,'after':hashlib.sha256(content).hexdigest()}
    archive=folder/'libNativeLegacyDisplay.late.a';subprocess.run(['/usr/bin/ar','rcs',str(archive),*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    for name in names:
        original=(folder/name).read_bytes();stored=subprocess.check_output(['/usr/bin/ar','p',str(archive),name]);assert stored==original+b'\n'*(-len(original)%8)
        records[name]['archived_after']=hashlib.sha256(stored).hexdigest()
    changed=[n for n,v in records.items() if v['before']!=v['archived_after']]
    if changed!=[plan['archive_member']]:raise ValueError('Other members changed')
    filelist=(base/'link.filelist').read_bytes();(out/'link.filelist').write_bytes(filelist)
    dependencies={p:sha(Path(p)) for p in filelist.decode().splitlines()}
    archives={}
    for value in link:
        if value.startswith('-Wl,-force_load,'):path=value.split(',',2)[2]
        elif value.endswith('.a'):path=value
        else:continue
        archives[path]=sha(Path(path))
    link=[v.replace(str(base/'link.filelist'),str(out/'link.filelist')).replace(str(base/'native.macho'),str(out/'native.macho')).replace(str(oldarchive),str(archive)) for v in link]
    write(out/'link-command.json',link);env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as log:subprocess.run(link,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    if exports(out/'native.macho')!=exports(base/'native.macho'):raise ValueError('Native exports differ')
    for path,digest in {**dependencies,**archives}.items():assert sha(Path(path))==digest
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256']
    kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']:cmd+=['--root',n]
    for n in pre['early_security_initializers']:cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])
    result={'profile':'v107-iokit-busy','profile_sha256':sha(profile/'plan.json'),'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'source_sha256':sha(source),'object_sha256':sha(obj),'changed_archive_members':changed,'members':records,
        'core_filelist_equal':True,'inherited_core_inputs':dependencies,'inherited_archives':archives,'original_apple_executables_hashed':30,'new_glue_scalar':True,'hardware_tested':False}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))

if __name__=='__main__':main()
