"""Compile the coherent native4K bootstrap pool update; no boot image or device I/O."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
from memory_window_patch import derive,HASHES
from replay_xnu_objects import global_symbols,sections
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    if sys.flags.optimize:raise SystemExit('Validation requires assertions')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('core','rt-build','llvm','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output.resolve();core=a.core.resolve();rt=a.rt_build.resolve()
    if out.exists():raise ValueError('New output required')
    cap=read(core/'capture.json');records=read(core/'build/results.json');plan=read(ROOT/'profiles/v109-memory8/plan.json')
    assert sha(rt/'kernel.macho')==plan['rt_kernel_sha256']
    baseov=read(Path(records['293']['command'][records['293']['command'].index('-ivfsoverlay')+1]))
    assert baseov.get('fallthrough') is False
    out.mkdir(parents=True);src=out/'source';src.mkdir();derived={}
    suffixes={'pmap.h':'/arm/pmap/pmap.h','assym.s':'/assym.s','arm_vm_init.c':'/arm64/arm_vm_init.c'}
    for name,suffix in suffixes.items():
        paths={x['external-contents'] for x in baseov['roots'] if x['name'].endswith(suffix)}
        assert len(paths)==1;original=Path(paths.pop());assert sha(original)==HASHES[name]
        target=src/name;target.write_bytes(derive(name,original.read_bytes()));derived[str(original)]=str(target)
    start=Path(cap['files'][cap['jobs'][304]['file']]['path']);assert sha(start)==plan['start_source_sha256']
    text=start.read_bytes();assert text.count(b'#include "assym.s"')==1
    assembly=src/'start.s';assembly.write_bytes(text)
    result={'profile':'v109-memory8','complete_kernel':False,'boot_packaged':False,'hardware_tested':False,'objects':{},'sources':{p.name:sha(p) for p in src.iterdir()},'bootstrap_pages':24,'intended_window_bytes':0x200000000}
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    for index,name in ((266,'pmap'),(293,'arm_vm_init'),(304,'start')):
        prior=records[str(index)];assert prior['accepted'] and not prior.get('postprocess') and not prior.get('objcopy')
        command=list(prior['command']);oldobj=Path(prior['output'])
        assert sha(oldobj)==prior['sha256']
        if index==266:
            # Keep the already tested managed-RT change; do not regress to v91.
            oldobj=rt/'objects/pmap.o';rr=read(rt/'result.json');assert sha(oldobj)==rr['pmap_object_sha256']
            overlay=read(rt/'source-overlay.json')
        else:overlay=read(Path(command[command.index('-ivfsoverlay')+1])) if index!=304 else json.loads(json.dumps(baseov))
        if overlay:
            assert overlay.get('fallthrough') is False
            replacements=0
            for entry in overlay['roots']:
                if entry['external-contents'] in derived:entry['external-contents']=derived[entry['external-contents']];replacements+=1
            assert replacements>=4
            ov=out/(name+'-overlay.json');write(ov,overlay)
            if '-ivfsoverlay' in command:command[command.index('-ivfsoverlay')+1]=str(ov)
            else:command+=['-ivfsoverlay',str(ov)]
        obj=out/(name+'.o');command[command.index('-o')+1]=str(obj)
        for flag in ('-dependency-file','-serialize-diagnostic-file'):
            if flag in command:command[command.index(flag)+1]=str(out/(name+flag+'.out'))
        cwd=core/'build/project/records'/cap['jobs'][index]['directory'].split('/research/',1)[1]
        write(out/(name+'-command.json'),command)
        with (out/(name+'.log')).open('x') as log:subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        if 'macro redefined' in (out/(name+'.log')).read_text():raise ValueError('Conflicting generated header')
        def bindings(p):return {n:(v[0],v[1],v[3]) for n,v in global_symbols(p).items()}
        assert bindings(oldobj)==bindings(obj)
        dis=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-d',str(obj)],text=True);oldasm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-d',str(oldobj)],text=True)
        (out/(name+'.asm')).write_text(dis)
        def vectors(t):return re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.).*)$',t,re.M)
        assert vectors(oldasm)==vectors(dis)
        if index==304:
            def words(t):return re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+(.+)$',t,re.M)
            before,after=words(oldasm),words(dis);assert len(before)==len(after)
            changes=[{'before':x,'after':y} for x,y in zip(before,after) if x!=y]
            # Two pool-bound additions (16->24 pages), one zero-loop count.
            assert len(changes)==3 and all('#0x18' in x['after'][1] for x in changes)
            result['assembly_changes']=changes
        result['objects'][name]={'sha256':sha(obj),'before_sha256':sha(oldobj),'bindings_equal':True,'existing_vectors_unchanged':True,'sections':sections(obj)}
    write(out/'result.json',result);print(json.dumps({k:v for k,v in result.items() if k!='objects'},indent=2))
if __name__=='__main__':main()
