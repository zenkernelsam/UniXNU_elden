"""Compile both result-bearing HID delivery units in the verified v91 context.

Object/ABI qualification only; does not link, package or access a device.
"""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'drivers/EldenHidDelivery'))
from SourcePatch import INPUTS,derive
from replay_xnu_objects import global_symbols,sections

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,data):p.write_text(json.dumps(data,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e!=0}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('native-parent','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.native_parent.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('New output directory required')
    cap=read(base/'capture.json');prior=read(base/'build/results.json')
    expected='2242750f53ea98118388131f2811c7921dd2d07183cc28bab8a08716822edbbf'
    if cap.get('portable_profile_sha256',cap.get('profile_sha256'))!=expected or (base/'build/capture.sha256').read_text().strip()!=sha(base/'capture.json'):
        raise ValueError('Wrong native parent')
    for item in cap['files'].values():
        if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed captured dependency: '+item['path'])
    out.mkdir(parents=True);(out/'source').mkdir();(out/'objects').mkdir();(out/'logs').mkdir()
    records={}
    for name,(_,expected_source) in INPUTS.items():
        found=[(i,j) for i,j in enumerate(cap['jobs']) if j['file'].endswith('/'+name)]
        if len(found)!=1:raise ValueError('Ambiguous native source: '+name)
        index,job=found[0];old=prior[str(index)];old_object=Path(old['output'])
        if not old['accepted'] or sha(old_object)!=old['sha256'] or cap['files'][job['file']]['sha256']!=expected_source:
            raise ValueError('Unqualified original object/source')
        source=out/'source'/name;source.write_bytes(derive(name))
        command=list(old['command']);vfs_index=command.index('-ivfsoverlay')+1
        overlay=read(Path(command[vfs_index]))
        if overlay.get('fallthrough') is not False:raise ValueError('Expected closed VFS')
        found=[x for x in overlay['roots'] if x['name']==old['source']]
        if len(found)!=1:raise ValueError('Ambiguous source provider')
        previous=found[0]['external-contents']
        if sha(Path(previous))!=expected_source:raise ValueError('Changed original source')
        for item in overlay['roots']:
            if item['external-contents']==previous:item['external-contents']=str(source)
        vfs=out/(name+'.overlay.json');write(vfs,overlay);command[vfs_index]=str(vfs)
        flags=job.get('objcopy',[])
        expected_flags=['--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeHidInput.cpp'] if name=='NativeHidInput.cpp' else []
        if flags!=expected_flags or old.get('objcopy',[])!=flags or old.get('postprocess',[]):
            raise ValueError('Unexpected parent postprocessing')
        target=out/'objects'/(name+'.o');raw=out/'objects'/(name+'.raw.o') if flags else target
        command[command.index('-o')+1]=str(raw)
        for flag in ('-dependency-file','-serialize-diagnostic-file'):
            if flag in command:command[command.index(flag)+1]=str(out/'logs'/(name+flag+'.out'))
        command+=['-target-feature','-neon','-target-feature','-fp-armv8']
        relative=Path(job['directory']).relative_to('/Users/ciscohe/Desktop/UniXNU_elden').as_posix()
        if relative.startswith('research/'):relative='records/'+relative[len('research/'):]
        cwd=base/'build/project'/relative
        if not cwd.is_dir():raise ValueError('Captured working directory missing')
        with (out/'logs'/(name+'.log')).open('x') as log:
            subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
        if flags:
            subprocess.run([str(a.llvm/'llvm-objcopy'),*flags,str(raw),str(target)],check=True)
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--disassemble','--reloc',str(target)],text=True)
        (out/'logs'/(name+'.asm')).write_text(asm)
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops):raise ValueError('Empty or non-scalar delivery unit')
        expected_exports=exports(old_object)
        if name=='EldenHidGlue.cpp':expected_exports=(expected_exports-{'_native_touch_set_sink'})|{'_elden_touch_set_sink_v2'}
        if exports(target)!=expected_exports:raise ValueError('Unexpected export change')
        before=sections(old_object);after=sections(target)
        for section in set(before)|set(after):
            if section.endswith('/__mod_init_func') and before.get(section)!=after.get(section):raise ValueError('Initializer changed')
        records[name]={'parent_job_index':index,'source_sha256':sha(source),'object_sha256':sha(target),'object':str(target),
            'scalar':True,'exports_checked':True,'command':command,'objcopy':flags,'cwd':str(cwd)}
    native=global_symbols(out/'objects/NativeHidInput.cpp.o');glue=global_symbols(out/'objects/EldenHidGlue.cpp.o')
    if '_native_touch_set_sink' in native or '_native_touch_set_sink' in glue:raise ValueError('Old void callback still referenced')
    if native['_elden_touch_set_sink_v2'][0]&0x0e or not glue['_elden_touch_set_sink_v2'][0]&0x0e:raise ValueError('Missing v2 consumer/provider pair')
    result={'parent_capture_sha256':sha(base/'capture.json'),'objects':records,'versioned_callback_pair':True,
        'kernel_linked':False,'boot_packaged':False,'hardware_tested':False,'device_operations':False}
    write(out/'result.json',result)
    print(json.dumps({'objects':2,'versioned_callback_pair':True,'kernel_linked':False,'hardware_tested':False}))
if __name__=='__main__':main()
