"""Rebuild frozen board archives against the private CPU8 headers/records."""
from pathlib import Path
import hashlib,json,os,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-full-20261111';tree=p/'tree';old=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
groups={
 'EldenAdspRuntime':r/'out/battery-telemetry-v35-20261108/EldenAdspRuntime/build.json',
 'EldenPmicClient':r/'out/battery-telemetry-v35-20261108/EldenPmicClient/build.json',
 'EldenBacklight':r/'out/backlight-runtime-v13-20261109/build.json',
 'EldenPmicTransport':r/'out/night-cores-20261108/EldenPmicTransport/build.json'}
catalog=[]
for f in (r/'out').rglob('build.json'):
    if f.stat().st_size>2_000_000:continue
    try:d=json.loads(f.read_text())
    except (ValueError,UnicodeError):continue
    if isinstance(d.get('commands'),list) and isinstance(d.get('objects'),dict):catalog.append((f,d))
def output(c):return Path(c[c.index('-o')+1]).name if '-o' in c else None
def source(c):
    inputs=[Path(x) for x in c if x.endswith(('.c','.cpp','.S','.s')) and Path(x).is_file()]
    assert len(inputs)==1,inputs
    return inputs[0]
def original_command(name,h,d):
    raw=name.replace('.late.o','.o')
    matches=[c for c in d['commands'] if isinstance(c,list) and output(c)==raw]
    if matches:return matches[0]
    for _,other in catalog:
        if other['objects'].get(name)==h:
            matches=[c for c in other['commands'] if isinstance(c,list) and output(c)==raw]
            if matches:return matches[0]
    raise RuntimeError('No original compile command for '+name)
cwd=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT'
assert (cwd/'IOPlatformExpert.cpo.json').exists(),'Fresh CPU8 compile records required'
report={'groups':{},'reused_old_objects':False,'hardware_tested':False}
for group,meta in groups.items():
    d=json.loads(meta.read_text());old_dir=Path(d['archive']).parent
    assert hashlib.sha256(Path(d['archive']).read_bytes()).hexdigest()==d['archive_sha256']
    dest=p/'archives'/group;dest.mkdir(parents=True,exist_ok=True);objects=[];rows=[]
    for name,h in d['objects'].items():
        assert hashlib.sha256((old_dir/name).read_bytes()).hexdigest()==h
        if name=='original-bindings.o':
            src=old_dir/'original-bindings.S';assert src.is_file()
            command=['/usr/bin/clang','-target','arm64-apple-ios17.0','-c',str(src),'-o',str(dest/name)]
        else:
            command=original_command(name,h,d);src=source(command)
            # Preserve every recorded source switch and immutable ABI include;
            # remap only the old generated kernel tree and output destinations.
            command=[x.replace(str(old),str(tree)) for x in command]
            raw=name.replace('.late.o','.o');command[command.index('-o')+1]=str(dest/raw)
            cleaned=[];i=0
            while i<len(command):
                if command[i] in ('-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
                if command[i] in ('-MD','-MMD','-MP'):i+=1;continue
                cleaned.append(command[i]);i+=1
            command=cleaned
        with (dest/(name+'.log')).open('w') as log:
            subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
        if '.late.o' in name:
            subprocess.run([llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func',
                            '--globalize-symbol=__GLOBAL__sub_I_'+src.name,dest/name.replace('.late.o','.o'),dest/name],check=True)
        obj=dest/name;objects.append(obj)
        rows.append({'name':name,'source':str(src),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),
                     'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'command':command})
    archive=dest/Path(d['archive']).name
    subprocess.run(['/usr/bin/ar','rcs',str(archive),*map(str,objects)],check=True)
    report['groups'][group]={'archive':str(archive),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                            'original_build_record':str(meta),'objects':rows}
    (p/'external-build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(group,len(objects),'objects rebuilt',flush=True)
print('Frozen source archives rebuilt with private CPU8 headers.',flush=True)
